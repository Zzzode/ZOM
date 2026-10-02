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

/// \brief Materializes the two's complement bit pattern of a canonical integer.
///
/// Negative values are encoded as their two's complement representation within
/// the carrier width. The value is rejected when its magnitude exceeds the
/// carrier width or when the magnitude is zero for a negative sign.
zc::Maybe<uint64_t> signExtendedBits(const checker::signature::CanonicalInteger& integer,
                                     IntegerBitWidth width) noexcept {
  const auto magnitude = integer.magnitude.asPtr();
  if (magnitude.size() > sizeof(uint64_t)) { return zc::none; }
  uint64_t bits = 0;
  for (const auto byte : magnitude) { bits = (bits << 8) | static_cast<uint64_t>(byte); }
  const uint32_t bitCount = bitCountFor(width);
  if (integer.sign == checker::signature::IntegerSign::Negative) {
    if (bits == 0) { return zc::none; }
    if (bitCount < 64) {
      const uint64_t limit = (static_cast<uint64_t>(1) << bitCount);
      if (bits > limit) { return zc::none; }
      bits = (limit - bits) & (limit - 1);
    } else {
      bits = 0 - bits;
    }
  } else {
    if (bitCount < 64) {
      const uint64_t limit = (static_cast<uint64_t>(1) << bitCount);
      if (bits >= limit) { return zc::none; }
    }
  }
  return bits;
}

/// \brief Resolves the integer carrier for a semantic type owned by the store.
zc::Maybe<ValueType> integerCarrierFor(identity::SemanticTypeId type,
                                       const type::SemanticTypeStore& semanticTypes) {
  auto lookup = semanticTypes.get(type);
  if (!lookup.is<type::SemanticTypeLookup>()) { return zc::none; }
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_IF_SOME(primitive, data.primitiveKind()) {
    ZC_IF_SOME(width, integerWidthFor(primitive)) { return ValueType::integer(width); }
  }
  return zc::none;
}

/// \brief Resolves the i1 carrier for a Bool semantic type owned by the store.
///
/// The boolean discriminant of the conditional lowers to a one-bit integer
/// carrier. A non-Bool type fails closed.
zc::Maybe<ValueType> boolCarrierFor(identity::SemanticTypeId type,
                                    const type::SemanticTypeStore& semanticTypes) {
  auto lookup = semanticTypes.get(type);
  if (!lookup.is<type::SemanticTypeLookup>()) { return zc::none; }
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_IF_SOME(primitive, data.primitiveKind()) {
    if (primitive == type::semantic::PrimitiveKind::Bool) {
      return ValueType::integer(IntegerBitWidth::Bit1);
    }
  }
  return zc::none;
}

/// \brief Resolves the i32 carrier for a nominal (enum) semantic type.
///
/// Enum variants lower to integer discriminants; the default representation
/// is i32. A non-nominal type fails closed. Struct types are nominal but
/// never appear as integer constants in the admitted lowering shapes, so the
/// integer-value check in lirOperandFor rejects them downstream.
zc::Maybe<ValueType> enumCarrierFor(identity::SemanticTypeId type,
                                    const type::SemanticTypeStore& semanticTypes) {
  auto lookup = semanticTypes.get(type);
  if (!lookup.is<type::SemanticTypeLookup>()) { return zc::none; }
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  if (!data.is<type::semantic::NominalTypeData>()) { return zc::none; }
  return ValueType::integer(IntegerBitWidth::Bit32);
}

/// \brief Selects the entry symbol for a caller function.
///
/// A parameter-free caller folds to the reserved no-argument `zom.module_init`
/// entry the runtime `_start` calls, so the module runs natively. A caller with
/// leading parameters keeps the index-independent `zom.caller` symbol and stays
/// object-only, since `_start` cannot bind arguments to it.
zc::String moduleEntrySymbol(size_t callerParameterCount) {
  return zc::heapString(callerParameterCount == 0 ? "zom.module_init" : "zom.caller");
}

/// \brief Resolves the opaque-pointer carrier for a shared reference type.
///
/// The implicit `this` receiver of a shared-receiver method is a const
/// reference; its physical carrier is a target pointer in address space zero.
/// Mutable references and every non-reference type fail closed in this slice.
zc::Maybe<bool> receiverReferenceIsMutable(identity::SemanticTypeId type,
                                           const type::SemanticTypeStore& semanticTypes) {
  auto lookup = semanticTypes.get(type);
  if (!lookup.is<type::SemanticTypeLookup>()) { return zc::none; }
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  if (!data.is<type::semantic::ReferenceTypeData>()) { return zc::none; }
  return data.get<type::semantic::ReferenceTypeData>().mutability ==
         type::semantic::Mutability::Mutable;
}

zc::Maybe<ValueType> receiverPointerCarrier(identity::SemanticTypeId type,
                                            const type::SemanticTypeStore& semanticTypes) {
  if (receiverReferenceIsMutable(type, semanticTypes) == zc::none) { return zc::none; }
  return ValueType::pointer(0);
}

/// \brief Resolves the zero-sized Unit carrier for a Unit-result method.
zc::Maybe<ValueType> unitCarrierFor(identity::SemanticTypeId type,
                                    const type::SemanticTypeStore& semanticTypes) {
  auto lookup = semanticTypes.get(type);
  if (!lookup.is<type::SemanticTypeLookup>()) { return zc::none; }
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_IF_SOME(primitive, data.primitiveKind()) {
    if (primitive == type::semantic::PrimitiveKind::Unit) { return ValueType::unit(); }
  }
  return zc::none;
}

/// \brief Resolves the opaque-pointer carrier for a Str result type.
///
/// A string literal return lowers to the address of a null-terminated global
/// string constant; its physical carrier is a target pointer in address space
/// zero. Every non-Str primitive fails closed in this slice.
zc::Maybe<ValueType> stringCarrierFor(identity::SemanticTypeId type,
                                      const type::SemanticTypeStore& semanticTypes) {
  auto lookup = semanticTypes.get(type);
  if (!lookup.is<type::SemanticTypeLookup>()) { return zc::none; }
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_IF_SOME(primitive, data.primitiveKind()) {
    if (primitive == type::semantic::PrimitiveKind::Str) { return ValueType::pointer(0); }
  }
  return zc::none;
}

/// \brief Maps a MIR relational operator to its LIR comparison operator.
ComparisonOp lirComparisonOpFor(mir::MirComparisonOperator op) noexcept {
  switch (op) {
    case mir::MirComparisonOperator::Eq:
      return ComparisonOp::Eq;
    case mir::MirComparisonOperator::Ne:
      return ComparisonOp::Ne;
    case mir::MirComparisonOperator::Lt:
      return ComparisonOp::Lt;
    case mir::MirComparisonOperator::Le:
      return ComparisonOp::Le;
    case mir::MirComparisonOperator::Gt:
      return ComparisonOp::Gt;
    case mir::MirComparisonOperator::Ge:
      return ComparisonOp::Ge;
  }
  return ComparisonOp::Eq;
}

/// \brief Maps a MIR arithmetic operator to its LIR arithmetic operator.
/// \return The LIR operator, or none for exponentiation, which has no integer
/// machine operation and stays outside the admitted lowering shapes.
zc::Maybe<ArithmeticOp> lirArithmeticOpFor(mir::MirArithmeticOperator op) noexcept {
  switch (op) {
    case mir::MirArithmeticOperator::Add:
      return ArithmeticOp::Add;
    case mir::MirArithmeticOperator::Sub:
      return ArithmeticOp::Sub;
    case mir::MirArithmeticOperator::Mul:
      return ArithmeticOp::Mul;
    case mir::MirArithmeticOperator::Div:
      return ArithmeticOp::Div;
    case mir::MirArithmeticOperator::Rem:
      return ArithmeticOp::Rem;
    case mir::MirArithmeticOperator::Pow:
      return zc::none;
    case mir::MirArithmeticOperator::Shl:
      return ArithmeticOp::Shl;
    case mir::MirArithmeticOperator::Shr:
      return ArithmeticOp::Shr;
    case mir::MirArithmeticOperator::UShr:
      return ArithmeticOp::UShr;
    case mir::MirArithmeticOperator::BitAnd:
      return ArithmeticOp::BitAnd;
    case mir::MirArithmeticOperator::BitOr:
      return ArithmeticOp::BitOr;
    case mir::MirArithmeticOperator::BitXor:
      return ArithmeticOp::BitXor;
  }
  return zc::none;
}

/// \brief Lowers a MIR operand (integer constant or parameter place-use) to a
/// LIR operand of the given integer carrier.
/// \return The operand, or none for a non-integer constant or a projected place.
zc::Maybe<Operand> lirOperandFor(const mir::MirOperand& operand, ValueType carrier) {
  if (operand.kind() == mir::MirOperandKind::Constant) {
    const auto boolean = operand.constantValue().value.booleanValue();
    if (boolean != zc::none) {
      if (carrier.kind() != ValueTypeKind::Integer ||
          carrier.integerWidth() != IntegerBitWidth::Bit1) {
        return zc::none;
      }
      auto constant = IntegerConstant::from(carrier, ZC_ASSERT_NONNULL(boolean) ? 1 : 0);
      if (constant == zc::none) { return zc::none; }
      return Operand::constant(ZC_REQUIRE_NONNULL(constant));
    }
    const auto integer = operand.constantValue().value.integerValue();
    if (integer == zc::none) { return zc::none; }
    // Prefer zero extension for non-negative values; fall back to two's
    // complement sign extension for negative constants such as the -1 used
    // by bitwise-not desugaring.
    auto bits = zeroExtendedBits(ZC_REQUIRE_NONNULL(integer), carrier.integerWidth());
    if (bits == zc::none) {
      bits = signExtendedBits(ZC_REQUIRE_NONNULL(integer), carrier.integerWidth());
    }
    if (bits == zc::none) { return zc::none; }
    auto constant = IntegerConstant::from(carrier, ZC_REQUIRE_NONNULL(bits));
    if (constant == zc::none) { return zc::none; }
    return Operand::constant(ZC_REQUIRE_NONNULL(constant));
  }
  if (operand.place().projections().size() != 0) { return zc::none; }
  return Operand::localUse(operand.place().local().ordinal());
}

}  // namespace

zc::Maybe<Module> MirToLirLowering::lowerScalarInitializer(
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

  auto lirConstant = IntegerConstant::from(carrierValue, ZC_REQUIRE_NONNULL(bits));
  if (lirConstant == zc::none) { return zc::none; }

  // Build the single entry block returning the constant.
  auto entryId = LirBlockId::fromOrdinal(1);
  if (entryId == zc::none) { return zc::none; }
  zc::Vector<BasicBlock> blocks;
  blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(entryId),
                        Terminator::returnInteger(ZC_REQUIRE_NONNULL(lirConstant))));

  // A module initializer has no user symbol name in this slice; use the stable
  // reserved ASCII runtime symbol for the module initializer entry.
  zc::Vector<Function> functions;
  functions.add(
      Function(function.owner, zc::heapString("zom.module_init"), carrierValue, zc::mv(blocks)));
  return Module(zc::mv(functions));
}

zc::Maybe<Module> MirToLirLowering::lowerScalarReturn(
    const mir::MirFunction& function, const type::SemanticTypeStore& semanticTypes) {
  // Admit only the verified scalar string-return shape: a standalone function
  // with no locals, one block, no statements, and a Return of a string constant.
  // This mirrors the structural facts that mir::validScalarReturnFunction
  // checked; we re-check them here so lowering is total over its declared input
  // and fail-closed on anything else.
  if (function.kind != mir::MirFunctionKind::Function || function.sourceScopes.size() != 1 ||
      function.locals.size() != 0 || function.blocks.size() != 1) {
    return zc::none;
  }

  const auto& block = function.blocks[0];
  if (block.statements.size() != 0 || block.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }

  const auto& returnValue = block.terminator.returnValue().value;
  if (returnValue == zc::none) { return zc::none; }

  zc::Maybe<zc::ArrayPtr<const uint8_t>> stringBytes;
  ZC_IF_SOME(value, returnValue) {
    if (value.kind() != mir::MirOperandKind::Constant) { return zc::none; }
    const auto& constant = value.constantValue();
    if (constant.type != function.resultType) { return zc::none; }
    stringBytes = constant.value.stringValue();
  }
  if (stringBytes == zc::none) { return zc::none; }

  // Resolve the opaque-pointer carrier for the Str result type.
  auto carrier = stringCarrierFor(function.resultType, semanticTypes);
  if (carrier == zc::none) { return zc::none; }
  const auto carrierValue = ZC_REQUIRE_NONNULL(carrier);

  // Copy the canonical UTF-8 bytes into the LIR string constant.
  const auto bytes = ZC_REQUIRE_NONNULL(stringBytes);
  zc::Vector<uint8_t> lirBytes;
  lirBytes.reserve(bytes.size());
  for (const auto byte : bytes) { lirBytes.add(byte); }

  auto lirConstant = StringConstant::from(carrierValue, zc::mv(lirBytes));
  if (lirConstant == zc::none) { return zc::none; }

  // Build the single entry block returning the string constant.
  auto entryId = LirBlockId::fromOrdinal(1);
  if (entryId == zc::none) { return zc::none; }
  zc::Vector<BasicBlock> blocks;
  blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(entryId),
                        Terminator::returnString(ZC_REQUIRE_NONNULL(zc::mv(lirConstant)))));

  // A zero-parameter caller folds to the reserved no-argument `zom.module_init`
  // entry the runtime `_start` calls, so the module runs natively.
  zc::Vector<Function> functions;
  functions.add(Function(function.owner, moduleEntrySymbol(0), carrierValue, zc::mv(blocks)));
  return Module(zc::mv(functions));
}

zc::Maybe<Module> MirToLirLowering::lowerAggregateFieldInitializer(
    const mir::MirFunction& function, const type::SemanticTypeStore& semanticTypes) {
  // Admit only the verified struct-local field-return shape. This mirrors the
  // structural facts that mir::validLocalAggregateFieldReturnFunction already
  // checked; we re-check them here so lowering is total over its declared input
  // and fail-closed on anything else.
  if (function.kind != mir::MirFunctionKind::Function || function.sourceScopes.size() != 1 ||
      function.locals.size() != 1 || function.blocks.size() != 1) {
    return zc::none;
  }

  const auto& local = function.locals[0];
  const auto& block = function.blocks[0];
  if (local.kind != mir::MirLocalKind::UserLocal || block.statements.size() != 2 ||
      block.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      block.statements[1].kind() != mir::MirStatementKind::Assign) {
    return zc::none;
  }

  // The assignment must construct the whole struct local from a nominal
  // aggregate whose every element is a constant.
  const auto& assignment = block.statements[1].assignmentValue();
  if (assignment.destination.local() != local.id ||
      assignment.destination.projections().size() != 0 ||
      assignment.value.kind() != mir::MirRvalueKind::NominalAggregate) {
    return zc::none;
  }
  const auto& aggregate = assignment.value.nominalAggregateValue();

  // The return must be a copy of exactly one field projection of the struct
  // local; that field selects which constant element becomes the result.
  if (block.terminator.kind() != mir::MirTerminatorKind::Return) { return zc::none; }
  const auto& returnValue = block.terminator.returnValue().value;
  if (returnValue == zc::none) { return zc::none; }
  zc::Maybe<identity::DefId> selectedField;
  ZC_IF_SOME(value, returnValue) {
    if (value.kind() == mir::MirOperandKind::Constant || value.place().local() != local.id ||
        value.place().projections().size() != 1) {
      return zc::none;
    }
    const auto& projection = value.place().projections()[0];
    if (projection.kind() != mir::MirProjectionKind::Field ||
        projection.resultType() != function.resultType) {
      return zc::none;
    }
    selectedField = projection.fieldValue().field;
  }
  if (selectedField == zc::none) { return zc::none; }
  const auto field = ZC_REQUIRE_NONNULL(selectedField);

  // Resolve the selected element's constant value. The field read is folded at
  // lowering time, so no struct is materialized in LIR.
  zc::Maybe<const mir::MirOperand&> selectedOperand;
  for (const auto& element : aggregate.elements) {
    if (element.field == field) { selectedOperand = element.operand; }
  }
  if (selectedOperand == zc::none) { return zc::none; }
  const auto& operand = ZC_REQUIRE_NONNULL(selectedOperand);
  if (operand.kind() != mir::MirOperandKind::Constant) { return zc::none; }
  const auto& constant = operand.constantValue();
  if (constant.type != function.resultType) { return zc::none; }

  // Resolve the integer carrier and materialize the folded field value.
  auto carrier = integerCarrierFor(function.resultType, semanticTypes);
  if (carrier == zc::none) { return zc::none; }
  const auto carrierValue = ZC_REQUIRE_NONNULL(carrier);

  const auto integer = constant.value.integerValue();
  if (integer == zc::none) { return zc::none; }
  auto bits = zeroExtendedBits(ZC_REQUIRE_NONNULL(integer), carrierValue.integerWidth());
  if (bits == zc::none) { return zc::none; }

  auto lirConstant = IntegerConstant::from(carrierValue, ZC_REQUIRE_NONNULL(bits));
  if (lirConstant == zc::none) { return zc::none; }

  // Build the single entry block returning the folded field value.
  auto entryId = LirBlockId::fromOrdinal(1);
  if (entryId == zc::none) { return zc::none; }
  zc::Vector<BasicBlock> blocks;
  blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(entryId),
                        Terminator::returnInteger(ZC_REQUIRE_NONNULL(lirConstant))));

  // Reuse the stable reserved module-initializer entry symbol so the folded
  // result slots into the existing translate/emit/execute path.
  zc::Vector<Function> functions;
  functions.add(
      Function(function.owner, zc::heapString("zom.module_init"), carrierValue, zc::mv(blocks)));
  return Module(zc::mv(functions));
}

zc::Maybe<Module> MirToLirLowering::lowerAggregateReturn(
    const mir::MirFunction& function, const type::SemanticTypeStore& semanticTypes) {
  // Admit only the verified whole-struct constant-return shape. This mirrors the
  // structural facts mir::validLocalAggregateReturnFunction checked; we re-check
  // them here so lowering is total over its declared input and fail-closed
  // otherwise.
  if (function.kind != mir::MirFunctionKind::Function || function.sourceScopes.size() != 1 ||
      function.locals.size() != 1 || function.blocks.size() != 1) {
    return zc::none;
  }

  const auto& local = function.locals[0];
  const auto& block = function.blocks[0];
  if (local.kind != mir::MirLocalKind::UserLocal || block.statements.size() != 2 ||
      block.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      block.statements[1].kind() != mir::MirStatementKind::Assign) {
    return zc::none;
  }

  // The assignment must construct the whole struct local from a nominal aggregate.
  const auto& assignment = block.statements[1].assignmentValue();
  if (assignment.destination.local() != local.id ||
      assignment.destination.projections().size() != 0 ||
      assignment.value.kind() != mir::MirRvalueKind::NominalAggregate) {
    return zc::none;
  }
  const auto& aggregate = assignment.value.nominalAggregateValue();

  // The return must copy the whole struct local: a place-use of the local with no
  // projections. A field projection here is the field-return shape lowered
  // elsewhere, so it is rejected.
  if (block.terminator.kind() != mir::MirTerminatorKind::Return) { return zc::none; }
  const auto& returnValue = block.terminator.returnValue().value;
  if (returnValue == zc::none) { return zc::none; }
  ZC_IF_SOME(value, returnValue) {
    if (value.kind() == mir::MirOperandKind::Constant || value.place().local() != local.id ||
        value.place().projections().size() != 0) {
      return zc::none;
    }
  }

  // Every element must be a constant of an integer carrier; lower each to a slot
  // in MIR element order. That order is the source struct-literal property order
  // (the checker records aggregate elements by source index and the HIR and MIR
  // builders copy it unchanged); it is NOT the nominal type's declared field
  // order, which is destroyed by the digest sort in the signature facts, and it
  // makes no claim about the target ABI struct layout.
  if (aggregate.elements.size() == 0) { return zc::none; }
  // Defense in depth: a repeated property name is a checker error and must not
  // reach lowering with two slots for one field.
  for (size_t left = 0; left < aggregate.elements.size(); ++left) {
    for (size_t right = left + 1; right < aggregate.elements.size(); ++right) {
      if (aggregate.elements[left].field == aggregate.elements[right].field) { return zc::none; }
    }
  }
  zc::Vector<IntegerConstant> slots(aggregate.elements.size());
  for (const auto& element : aggregate.elements) {
    if (element.operand.kind() != mir::MirOperandKind::Constant) { return zc::none; }
    const auto& constant = element.operand.constantValue();
    auto carrier = integerCarrierFor(constant.type, semanticTypes);
    if (carrier == zc::none) { return zc::none; }
    const auto carrierValue = ZC_REQUIRE_NONNULL(carrier);
    const auto integer = constant.value.integerValue();
    if (integer == zc::none) { return zc::none; }
    auto bits = zeroExtendedBits(ZC_REQUIRE_NONNULL(integer), carrierValue.integerWidth());
    if (bits == zc::none) { return zc::none; }
    auto slot = IntegerConstant::from(carrierValue, ZC_REQUIRE_NONNULL(bits));
    if (slot == zc::none) { return zc::none; }
    slots.add(ZC_REQUIRE_NONNULL(slot));
  }

  // The function return carrier is the first slot's carrier as a transitional
  // placeholder; the translator derives the real literal-struct return type from
  // the slot carriers, so this only satisfies the integer entry check.
  const auto placeholderCarrier = slots[0].carrier();

  auto terminator = Terminator::returnAggregate(zc::mv(slots));
  if (terminator == zc::none) { return zc::none; }

  auto entryId = LirBlockId::fromOrdinal(1);
  if (entryId == zc::none) { return zc::none; }
  zc::Vector<BasicBlock> blocks;
  blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(entryId), ZC_REQUIRE_NONNULL(zc::mv(terminator))));

  zc::Vector<Function> functions;
  functions.add(Function(function.owner, zc::heapString("zom.module_init"), placeholderCarrier,
                         zc::mv(blocks)));
  return Module(zc::mv(functions));
}

zc::Maybe<Module> MirToLirLowering::lowerConditionalReturn(
    const mir::MirFunction& function, const type::SemanticTypeStore& semanticTypes) {
  // Admit only the verified four-block boolean-conditional return shape. This
  // re-checks the structure that mir::validConditionalReturnFunction validated
  // so lowering is total over its declared input and fail-closed on anything
  // else. The function has N parameters (a boolean discriminant plus optional
  // integer value parameters an arm may return) and one result local.
  if (function.kind != mir::MirFunctionKind::Function || function.sourceScopes.size() != 1 ||
      function.locals.size() < 2 || function.blocks.size() != 4) {
    return zc::none;
  }

  const size_t parameterCount = function.locals.size() - 1;
  const auto& result = function.locals[parameterCount];
  if (result.kind != mir::MirLocalKind::FunctionResult || result.type != function.resultType) {
    return zc::none;
  }
  for (size_t i = 0; i < parameterCount; ++i) {
    if (function.locals[i].kind != mir::MirLocalKind::Parameter) { return zc::none; }
  }

  auto resultCarrier = integerCarrierFor(function.resultType, semanticTypes);
  if (resultCarrier == zc::none) { return zc::none; }
  const auto resultCarrierValue = ZC_REQUIRE_NONNULL(resultCarrier);

  const uint32_t resultOrdinal = result.id.ordinal();

  const auto& entry = function.blocks[0];
  const auto& thenBlock = function.blocks[1];
  const auto& elseBlock = function.blocks[2];
  const auto& joinBlock = function.blocks[3];

  // Entry: StorageLive(result); SwitchInt(bool cond param) -> then/else.
  if (entry.statements.size() != 1 ||
      entry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != result.id ||
      entry.terminator.kind() != mir::MirTerminatorKind::SwitchInt) {
    return zc::none;
  }
  const auto& switchInt = entry.terminator.switchIntValue();
  if (switchInt.discriminant.kind() != mir::MirOperandKind::Copy ||
      switchInt.discriminant.place().projections().size() != 0 || switchInt.arms.size() != 2) {
    return zc::none;
  }
  // The discriminant is one of the boolean parameters; find it and check carrier.
  const uint32_t conditionOrdinal = switchInt.discriminant.place().local().ordinal();
  bool conditionIsParameter = false;
  for (size_t i = 0; i < parameterCount; ++i) {
    if (function.locals[i].id == switchInt.discriminant.place().local()) {
      conditionIsParameter = true;
      if (boolCarrierFor(function.locals[i].type, semanticTypes) == zc::none) { return zc::none; }
    }
  }
  if (!conditionIsParameter) { return zc::none; }
  // The true arm targets the then block; the false arm and default target the
  // else block, matching the verified diamond.
  const auto trueTargetOrdinal = switchInt.arms[0].target.ordinal();
  const auto falseTargetOrdinal = switchInt.arms[1].target.ordinal();
  if (trueTargetOrdinal != thenBlock.id.ordinal() || falseTargetOrdinal != elseBlock.id.ordinal() ||
      switchInt.defaultTarget != elseBlock.id) {
    return zc::none;
  }

  // Each arm assigns the result local (a constant or a parameter place-use) then
  // jumps to the join.
  auto lowerArm = [&](const mir::MirBasicBlock& branch, zc::Vector<Statement>& out) -> bool {
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
    if (operand.kind() == mir::MirOperandKind::Constant &&
        operand.constantValue().type != function.resultType) {
      return false;
    }
    // A parameter place-use arm must reference an integer parameter of the
    // result type (a projected place is out of the slice).
    if (operand.kind() != mir::MirOperandKind::Constant &&
        (operand.place().projections().size() != 0 ||
         operand.place().rootType() != function.resultType)) {
      return false;
    }
    auto lowered = lirOperandFor(operand, resultCarrierValue);
    if (lowered == zc::none) { return false; }
    out.add(Statement::assign(resultOrdinal, ZC_REQUIRE_NONNULL(lowered)));
    return true;
  };

  zc::Vector<Statement> thenStatements;
  zc::Vector<Statement> elseStatements;
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

  zc::Vector<BasicBlock> blocks;
  {
    zc::Vector<Statement> none;
    blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(entryId), zc::mv(none),
                          Terminator::condBranch(conditionOrdinal, ZC_REQUIRE_NONNULL(thenId),
                                                 ZC_REQUIRE_NONNULL(elseId))));
  }
  blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(thenId), zc::mv(thenStatements),
                        Terminator::gotoBlock(ZC_REQUIRE_NONNULL(joinId))));
  blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(elseId), zc::mv(elseStatements),
                        Terminator::gotoBlock(ZC_REQUIRE_NONNULL(joinId))));
  {
    zc::Vector<Statement> none;
    blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(joinId), zc::mv(none),
                          Terminator::returnLocal(resultOrdinal)));
  }

  // Declare every parameter local: the boolean discriminant as its i1 carrier,
  // integer value parameters as their integer carriers.
  zc::Vector<Local> parameters;
  for (size_t i = 0; i < parameterCount; ++i) {
    const auto& parameterLocal = function.locals[i];
    zc::Maybe<ValueType> carrier = boolCarrierFor(parameterLocal.type, semanticTypes);
    if (carrier == zc::none) { carrier = integerCarrierFor(parameterLocal.type, semanticTypes); }
    if (carrier == zc::none) { return zc::none; }
    parameters.add(Local(parameterLocal.id.ordinal(), ZC_REQUIRE_NONNULL(carrier)));
  }
  zc::Vector<Local> locals;
  locals.add(Local(resultOrdinal, resultCarrierValue));

  zc::Vector<Function> functions;
  functions.add(Function(function.owner, zc::heapString("zom.conditional"), resultCarrierValue,
                         zc::mv(parameters), zc::mv(locals), zc::mv(blocks)));
  return Module(zc::mv(functions));
}

zc::Maybe<Module> MirToLirLowering::lowerLoopReturn(const mir::MirFunction& function,
                                                    const type::SemanticTypeStore& semanticTypes) {
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
  auto exitConstant = IntegerConstant::from(resultCarrierValue, ZC_REQUIRE_NONNULL(bits));
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

  zc::Vector<BasicBlock> blocks;
  {
    zc::Vector<Statement> none;
    blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(entryId), zc::mv(none),
                          Terminator::gotoBlock(ZC_REQUIRE_NONNULL(headerId))));
  }
  {
    zc::Vector<Statement> none;
    blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(headerId), zc::mv(none),
                          Terminator::condBranch(parameterOrdinal, ZC_REQUIRE_NONNULL(bodyId),
                                                 ZC_REQUIRE_NONNULL(exitId))));
  }
  {
    zc::Vector<Statement> none;
    blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(bodyId), zc::mv(none),
                          Terminator::gotoBlock(ZC_REQUIRE_NONNULL(headerId))));
  }
  {
    zc::Vector<Statement> exitStatements;
    exitStatements.add(
        Statement::assign(resultOrdinal, Operand::constant(ZC_REQUIRE_NONNULL(exitConstant))));
    blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(exitId), zc::mv(exitStatements),
                          Terminator::returnLocal(resultOrdinal)));
  }

  zc::Vector<Local> parameters;
  parameters.add(Local(parameterOrdinal, ZC_REQUIRE_NONNULL(parameterCarrier)));
  zc::Vector<Local> locals;
  locals.add(Local(resultOrdinal, resultCarrierValue));

  zc::Vector<Function> functions;
  functions.add(Function(function.owner, zc::heapString("zom.loop"), resultCarrierValue,
                         zc::mv(parameters), zc::mv(locals), zc::mv(blocks)));
  return Module(zc::mv(functions));
}

zc::Maybe<Module> MirToLirLowering::lowerLoopBodyReturn(
    const mir::MirFunction& function, const type::SemanticTypeStore& semanticTypes) {
  // Admit only the verified reducible four-block while-loop body return shape,
  // re-checking the structure that mir::validLoopBodyReturnFunction validated:
  // a parameter-local prefix, one integer user local brought to life by
  // StorageLive plus an initializing Assign of a constant, then a four-block
  // loop whose body overwrites the local and whose exit returns it. A trailing
  // break sends the body to the exit; a trailing continue or a write-only body
  // jumps back to the header.
  if (function.kind != mir::MirFunctionKind::Function || function.sourceScopes.size() != 1 ||
      function.blocks.size() != 4 || function.locals.size() < 2) {
    return zc::none;
  }

  size_t parameterCount = 0;
  while (parameterCount < function.locals.size() &&
         function.locals[parameterCount].kind == mir::MirLocalKind::Parameter) {
    ++parameterCount;
  }
  if (function.locals.size() - parameterCount != 1) { return zc::none; }
  const auto& localDecl = function.locals[parameterCount];
  if (localDecl.kind != mir::MirLocalKind::UserLocal) { return zc::none; }

  auto resultCarrier = integerCarrierFor(function.resultType, semanticTypes);
  if (resultCarrier == zc::none) { return zc::none; }
  const auto resultCarrierValue = ZC_REQUIRE_NONNULL(resultCarrier);
  if (resultCarrierValue.kind() != ValueTypeKind::Integer) { return zc::none; }

  const auto& entry = function.blocks[0];
  const auto& header = function.blocks[1];
  const auto& body = function.blocks[2];
  const auto& exit = function.blocks[3];

  // Entry: StorageLive(local); Assign(local = constant, Initialize); Goto(header).
  if (entry.statements.size() != 2 ||
      entry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != localDecl.id ||
      entry.statements[1].kind() != mir::MirStatementKind::Assign ||
      entry.terminator.kind() != mir::MirTerminatorKind::Goto ||
      entry.terminator.gotoValue().target != header.id) {
    return zc::none;
  }
  const auto& initAssign = entry.statements[1].assignmentValue();
  if (initAssign.destination.local() != localDecl.id ||
      initAssign.destination.projections().size() != 0 ||
      initAssign.initialization != mir::MirInitializationKind::Initialize ||
      initAssign.value.kind() != mir::MirRvalueKind::Use) {
    return zc::none;
  }
  const auto& initOperand = initAssign.value.useValue().operand;
  if (initOperand.kind() != mir::MirOperandKind::Constant ||
      initOperand.constantValue().type != localDecl.type) {
    return zc::none;
  }
  auto initLowered = lirOperandFor(initOperand, resultCarrierValue);
  if (initLowered == zc::none) { return zc::none; }

  // Header: no statements; SwitchInt(bool param) [true -> body], default = exit.
  if (header.statements.size() != 0 ||
      header.terminator.kind() != mir::MirTerminatorKind::SwitchInt) {
    return zc::none;
  }
  const auto& switchInt = header.terminator.switchIntValue();
  if (switchInt.discriminant.kind() != mir::MirOperandKind::Copy ||
      switchInt.discriminant.place().projections().size() != 0 || switchInt.arms.size() != 1 ||
      switchInt.arms[0].target != body.id || switchInt.defaultTarget != exit.id) {
    return zc::none;
  }
  const auto conditionLocalId = switchInt.discriminant.place().local();

  // Identify the condition parameter and verify every carrier: the condition
  // is Bit1, every other parameter and the user local share the result carrier.
  uint32_t conditionOrdinal = 0;
  bool conditionFound = false;
  for (size_t i = 0; i < parameterCount; ++i) {
    const auto& param = function.locals[i];
    if (param.id == conditionLocalId) {
      auto boolCarrier = boolCarrierFor(param.type, semanticTypes);
      if (boolCarrier == zc::none) { return zc::none; }
      conditionOrdinal = param.id.ordinal();
      conditionFound = true;
    } else {
      auto carrier = integerCarrierFor(param.type, semanticTypes);
      if (carrier == zc::none || ZC_REQUIRE_NONNULL(carrier) != resultCarrierValue) {
        return zc::none;
      }
    }
  }
  if (!conditionFound) { return zc::none; }
  {
    auto carrier = integerCarrierFor(localDecl.type, semanticTypes);
    if (carrier == zc::none || ZC_REQUIRE_NONNULL(carrier) != resultCarrierValue) {
      return zc::none;
    }
  }

  // Body: one or more Overwrite assigns of the local, then Goto(exit) for a
  // trailing break or Goto(header) back-edge for continue/fallthrough.
  if (body.statements.size() < 1 || body.terminator.kind() != mir::MirTerminatorKind::Goto) {
    return zc::none;
  }
  const bool breaksToExit = body.terminator.gotoValue().target == exit.id;
  if (!breaksToExit && body.terminator.gotoValue().target != header.id) { return zc::none; }

  auto leafOperand = [&](const mir::MirOperand& operand) -> zc::Maybe<Operand> {
    if (operand.kind() == mir::MirOperandKind::Constant) {
      return lirOperandFor(operand, resultCarrierValue);
    }
    if (operand.place().projections().size() != 0) { return zc::none; }
    const uint32_t ordinal = operand.place().local().ordinal();
    bool declared = false;
    for (const auto& local : function.locals) {
      if (local.id.ordinal() == ordinal) {
        declared = true;
        break;
      }
    }
    if (!declared) { return zc::none; }
    return Operand::localUse(ordinal);
  };

  const uint32_t destinationOrdinal = localDecl.id.ordinal();
  zc::Vector<Statement> bodyStatements;
  for (const auto& statement : body.statements) {
    if (statement.kind() != mir::MirStatementKind::Assign) { return zc::none; }
    const auto& assignment = statement.assignmentValue();
    if (assignment.destination.local() != localDecl.id ||
        assignment.destination.projections().size() != 0 ||
        assignment.initialization != mir::MirInitializationKind::Overwrite) {
      return zc::none;
    }
    if (assignment.value.kind() == mir::MirRvalueKind::Use) {
      auto lowered = leafOperand(assignment.value.useValue().operand);
      if (lowered == zc::none) { return zc::none; }
      bodyStatements.add(Statement::assign(destinationOrdinal, ZC_REQUIRE_NONNULL(lowered)));
    } else if (assignment.value.kind() == mir::MirRvalueKind::Arithmetic) {
      const auto& arithmetic = assignment.value.arithmeticValue();
      auto op = lirArithmeticOpFor(arithmetic.op);
      if (op == zc::none) { return zc::none; }
      if (arithmetic.resultType != localDecl.type) { return zc::none; }
      auto left = leafOperand(arithmetic.left);
      auto right = leafOperand(arithmetic.right);
      if (left == zc::none || right == zc::none) { return zc::none; }
      bodyStatements.add(Statement::arithmetic(destinationOrdinal, ZC_REQUIRE_NONNULL(op),
                                               ZC_REQUIRE_NONNULL(left),
                                               ZC_REQUIRE_NONNULL(right)));
    } else if (assignment.value.kind() == mir::MirRvalueKind::Comparison) {
      const auto& comparison = assignment.value.comparisonValue();
      auto op = lirComparisonOpFor(comparison.op);
      if (comparison.resultType != localDecl.type) { return zc::none; }
      auto left = leafOperand(comparison.left);
      auto right = leafOperand(comparison.right);
      if (left == zc::none || right == zc::none) { return zc::none; }
      bodyStatements.add(Statement::compare(destinationOrdinal, op, ZC_REQUIRE_NONNULL(left),
                                            ZC_REQUIRE_NONNULL(right)));
    } else {
      return zc::none;
    }
  }

  // Exit: no statements; Return(place-use local).
  if (exit.statements.size() != 0 || exit.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& returnValue = exit.terminator.returnValue().value;
  if (returnValue == zc::none) { return zc::none; }
  bool returnsLocal = false;
  ZC_IF_SOME(value, returnValue) {
    returnsLocal = value.kind() != mir::MirOperandKind::Constant &&
                   value.place().local() == localDecl.id && value.place().projections().size() == 0;
  }
  if (!returnsLocal) { return zc::none; }

  auto entryId = LirBlockId::fromOrdinal(1);
  auto headerId = LirBlockId::fromOrdinal(2);
  auto bodyId = LirBlockId::fromOrdinal(3);
  auto exitId = LirBlockId::fromOrdinal(4);
  if (entryId == zc::none || headerId == zc::none || bodyId == zc::none || exitId == zc::none) {
    return zc::none;
  }

  zc::Vector<BasicBlock> blocks;
  {
    zc::Vector<Statement> entryStatements;
    entryStatements.add(Statement::assign(destinationOrdinal, ZC_REQUIRE_NONNULL(initLowered)));
    blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(entryId), zc::mv(entryStatements),
                          Terminator::gotoBlock(ZC_REQUIRE_NONNULL(headerId))));
  }
  {
    zc::Vector<Statement> none;
    blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(headerId), zc::mv(none),
                          Terminator::condBranch(conditionOrdinal, ZC_REQUIRE_NONNULL(bodyId),
                                                 ZC_REQUIRE_NONNULL(exitId))));
  }
  {
    blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(bodyId), zc::mv(bodyStatements),
                          Terminator::gotoBlock(breaksToExit ? ZC_REQUIRE_NONNULL(exitId)
                                                             : ZC_REQUIRE_NONNULL(headerId))));
  }
  {
    zc::Vector<Statement> none;
    blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(exitId), zc::mv(none),
                          Terminator::returnLocal(destinationOrdinal)));
  }

  zc::Vector<Local> parameters;
  for (size_t i = 0; i < parameterCount; ++i) {
    const auto& param = function.locals[i];
    if (param.id == conditionLocalId) {
      auto carrier = boolCarrierFor(param.type, semanticTypes);
      parameters.add(Local(param.id.ordinal(), ZC_REQUIRE_NONNULL(carrier)));
    } else {
      parameters.add(Local(param.id.ordinal(), resultCarrierValue));
    }
  }
  zc::Vector<Local> locals;
  locals.add(Local(destinationOrdinal, resultCarrierValue));

  // A parameter-free loop body folds to the reserved no-argument
  // `zom.module_init` entry the runtime `_start` calls; a parameterized body
  // keeps an index-independent reserved symbol and stays object-only.
  zc::String symbol = zc::heapString(parameterCount == 0 ? "zom.module_init" : "zom.loop_body");
  zc::Vector<Function> functions;
  functions.add(Function(function.owner, zc::mv(symbol), resultCarrierValue, zc::mv(parameters),
                         zc::mv(locals), zc::mv(blocks)));
  return Module(zc::mv(functions));
}

zc::Maybe<Module> MirToLirLowering::lowerForLoopReturn(
    const mir::MirFunction& function, const type::SemanticTypeStore& semanticTypes) {
  // Admit only the verified reducible four-block for-loop return shape,
  // re-checking the structure that mir::validForLoopReturnFunction validated:
  // a parameter-local prefix, one integer user local (the loop variable)
  // brought to life by StorageLive plus an initializing Assign of a constant,
  // a boolean temporary (the comparison result), and an integer
  // function-result local; a four-block loop whose header computes the
  // comparison temp and switches on it, whose body overwrites the loop
  // variable with an arithmetic rvalue, and whose exit assigns the result a
  // constant and returns it.
  if (function.kind != mir::MirFunctionKind::Function || function.sourceScopes.size() != 1 ||
      function.blocks.size() != 4 || function.locals.size() < 3) {
    return zc::none;
  }

  size_t parameterCount = 0;
  while (parameterCount < function.locals.size() &&
         function.locals[parameterCount].kind == mir::MirLocalKind::Parameter) {
    ++parameterCount;
  }
  if (function.locals.size() - parameterCount != 3) { return zc::none; }
  const auto& localDecl = function.locals[parameterCount];
  const auto& tempDecl = function.locals[parameterCount + 1];
  const auto& resultDecl = function.locals[parameterCount + 2];
  if (localDecl.kind != mir::MirLocalKind::UserLocal ||
      tempDecl.kind != mir::MirLocalKind::Temporary ||
      resultDecl.kind != mir::MirLocalKind::FunctionResult ||
      resultDecl.type != function.resultType) {
    return zc::none;
  }

  auto resultCarrier = integerCarrierFor(function.resultType, semanticTypes);
  if (resultCarrier == zc::none) { return zc::none; }
  const auto resultCarrierValue = ZC_REQUIRE_NONNULL(resultCarrier);
  if (resultCarrierValue.kind() != ValueTypeKind::Integer) { return zc::none; }
  auto tempCarrier = boolCarrierFor(tempDecl.type, semanticTypes);
  if (tempCarrier == zc::none) { return zc::none; }
  const auto tempCarrierValue = ZC_REQUIRE_NONNULL(tempCarrier);
  {
    auto carrier = integerCarrierFor(localDecl.type, semanticTypes);
    if (carrier == zc::none || ZC_REQUIRE_NONNULL(carrier) != resultCarrierValue) {
      return zc::none;
    }
  }
  for (size_t i = 0; i < parameterCount; ++i) {
    auto carrier = integerCarrierFor(function.locals[i].type, semanticTypes);
    if (carrier == zc::none || ZC_REQUIRE_NONNULL(carrier) != resultCarrierValue) {
      return zc::none;
    }
  }

  const auto& entry = function.blocks[0];
  const auto& header = function.blocks[1];
  const auto& body = function.blocks[2];
  const auto& exit = function.blocks[3];

  // Entry: StorageLive(result); StorageLive(local); StorageLive(temp);
  // Assign(local = constant, Initialize);
  // Assign(temp = Comparison(op, copy(local), constant), Initialize);
  // Goto(header).
  if (entry.statements.size() != 5 ||
      entry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != resultDecl.id ||
      entry.statements[1].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[1].storageLocal() != localDecl.id ||
      entry.statements[2].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[2].storageLocal() != tempDecl.id ||
      entry.statements[3].kind() != mir::MirStatementKind::Assign ||
      entry.statements[4].kind() != mir::MirStatementKind::Assign ||
      entry.terminator.kind() != mir::MirTerminatorKind::Goto ||
      entry.terminator.gotoValue().target != header.id) {
    return zc::none;
  }
  const auto& initAssign = entry.statements[3].assignmentValue();
  if (initAssign.destination.local() != localDecl.id ||
      initAssign.destination.projections().size() != 0 ||
      initAssign.initialization != mir::MirInitializationKind::Initialize ||
      initAssign.value.kind() != mir::MirRvalueKind::Use) {
    return zc::none;
  }
  const auto& initOperand = initAssign.value.useValue().operand;
  if (initOperand.kind() != mir::MirOperandKind::Constant ||
      initOperand.constantValue().type != localDecl.type) {
    return zc::none;
  }
  auto initLowered = lirOperandFor(initOperand, resultCarrierValue);
  if (initLowered == zc::none) { return zc::none; }
  // Entry condition comparison.
  const auto& entryCondAssign = entry.statements[4].assignmentValue();
  if (entryCondAssign.destination.local() != tempDecl.id ||
      entryCondAssign.destination.projections().size() != 0 ||
      entryCondAssign.initialization != mir::MirInitializationKind::Initialize ||
      entryCondAssign.value.kind() != mir::MirRvalueKind::Comparison) {
    return zc::none;
  }
  const auto& entryComparison = entryCondAssign.value.comparisonValue();
  auto cmpOp = lirComparisonOpFor(entryComparison.op);
  if (entryComparison.resultType != tempDecl.type) { return zc::none; }
  if (entryComparison.left.kind() != mir::MirOperandKind::Copy ||
      entryComparison.left.place().local() != localDecl.id ||
      entryComparison.left.place().projections().size() != 0) {
    return zc::none;
  }
  if (entryComparison.right.kind() != mir::MirOperandKind::Constant ||
      entryComparison.right.constantValue().type != localDecl.type) {
    return zc::none;
  }
  auto condRightLowered = lirOperandFor(entryComparison.right, resultCarrierValue);
  if (condRightLowered == zc::none) { return zc::none; }

  // Header: SwitchInt(copy(temp)) [true -> body], default = exit.
  if (header.statements.size() != 0 ||
      header.terminator.kind() != mir::MirTerminatorKind::SwitchInt) {
    return zc::none;
  }
  const auto& switchInt = header.terminator.switchIntValue();
  if (switchInt.discriminant.kind() != mir::MirOperandKind::Copy ||
      switchInt.discriminant.place().local() != tempDecl.id ||
      switchInt.discriminant.place().projections().size() != 0 || switchInt.arms.size() != 1 ||
      switchInt.arms[0].target != body.id || switchInt.defaultTarget != exit.id) {
    return zc::none;
  }

  // Body: Assign(local = Arithmetic(op, copy(local), constant), Overwrite);
  // Assign(temp = Comparison(op, copy(local), constant), Overwrite);
  // Goto(exit) for a trailing break or Goto(header) back-edge for
  // continue/fallthrough.
  if (body.statements.size() != 2 || body.statements[0].kind() != mir::MirStatementKind::Assign ||
      body.statements[1].kind() != mir::MirStatementKind::Assign ||
      body.terminator.kind() != mir::MirTerminatorKind::Goto) {
    return zc::none;
  }
  const bool breaksToExit = body.terminator.gotoValue().target == exit.id;
  if (!breaksToExit && body.terminator.gotoValue().target != header.id) { return zc::none; }
  const auto& updateAssign = body.statements[0].assignmentValue();
  if (updateAssign.destination.local() != localDecl.id ||
      updateAssign.destination.projections().size() != 0 ||
      updateAssign.initialization != mir::MirInitializationKind::Overwrite ||
      updateAssign.value.kind() != mir::MirRvalueKind::Arithmetic) {
    return zc::none;
  }
  const auto& arithmetic = updateAssign.value.arithmeticValue();
  auto arithOp = lirArithmeticOpFor(arithmetic.op);
  if (arithOp == zc::none) { return zc::none; }
  if (arithmetic.resultType != localDecl.type) { return zc::none; }
  if (arithmetic.left.kind() != mir::MirOperandKind::Copy ||
      arithmetic.left.place().local() != localDecl.id ||
      arithmetic.left.place().projections().size() != 0) {
    return zc::none;
  }
  if (arithmetic.right.kind() != mir::MirOperandKind::Constant ||
      arithmetic.right.constantValue().type != localDecl.type) {
    return zc::none;
  }
  auto arithRightLowered = lirOperandFor(arithmetic.right, resultCarrierValue);
  if (arithRightLowered == zc::none) { return zc::none; }
  // Body condition recompute.
  const auto& bodyCondAssign = body.statements[1].assignmentValue();
  if (bodyCondAssign.destination.local() != tempDecl.id ||
      bodyCondAssign.destination.projections().size() != 0 ||
      bodyCondAssign.initialization != mir::MirInitializationKind::Overwrite ||
      bodyCondAssign.value.kind() != mir::MirRvalueKind::Comparison) {
    return zc::none;
  }
  const auto& bodyComparison = bodyCondAssign.value.comparisonValue();
  if (bodyComparison.op != entryComparison.op || bodyComparison.resultType != tempDecl.type) {
    return zc::none;
  }
  if (bodyComparison.left.kind() != mir::MirOperandKind::Copy ||
      bodyComparison.left.place().local() != localDecl.id ||
      bodyComparison.left.place().projections().size() != 0) {
    return zc::none;
  }
  if (bodyComparison.right.kind() != mir::MirOperandKind::Constant ||
      bodyComparison.right.constantValue().type != localDecl.type) {
    return zc::none;
  }

  // Exit: Assign(result = constant, Initialize); Return(place-use result).
  if (exit.statements.size() != 1 || exit.statements[0].kind() != mir::MirStatementKind::Assign ||
      exit.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& resultAssign = exit.statements[0].assignmentValue();
  if (resultAssign.destination.local() != resultDecl.id ||
      resultAssign.destination.projections().size() != 0 ||
      resultAssign.initialization != mir::MirInitializationKind::Initialize ||
      resultAssign.value.kind() != mir::MirRvalueKind::Use) {
    return zc::none;
  }
  const auto& resultOperand = resultAssign.value.useValue().operand;
  if (resultOperand.kind() != mir::MirOperandKind::Constant ||
      resultOperand.constantValue().type != function.resultType) {
    return zc::none;
  }
  auto resultLowered = lirOperandFor(resultOperand, resultCarrierValue);
  if (resultLowered == zc::none) { return zc::none; }
  const auto& returnValue = exit.terminator.returnValue().value;
  if (returnValue == zc::none) { return zc::none; }
  bool returnsResult = false;
  ZC_IF_SOME(value, returnValue) {
    returnsResult = value.kind() != mir::MirOperandKind::Constant &&
                    value.place().local() == resultDecl.id &&
                    value.place().projections().size() == 0;
  }
  if (!returnsResult) { return zc::none; }

  const uint32_t localOrdinal = localDecl.id.ordinal();
  const uint32_t tempOrdinal = tempDecl.id.ordinal();
  const uint32_t resultOrdinal = resultDecl.id.ordinal();

  auto entryId = LirBlockId::fromOrdinal(1);
  auto headerId = LirBlockId::fromOrdinal(2);
  auto bodyId = LirBlockId::fromOrdinal(3);
  auto exitId = LirBlockId::fromOrdinal(4);
  if (entryId == zc::none || headerId == zc::none || bodyId == zc::none || exitId == zc::none) {
    return zc::none;
  }

  zc::Vector<BasicBlock> blocks;
  {
    zc::Vector<Statement> entryStatements;
    entryStatements.add(Statement::assign(localOrdinal, ZC_REQUIRE_NONNULL(initLowered)));
    entryStatements.add(Statement::compare(tempOrdinal, cmpOp, Operand::localUse(localOrdinal),
                                           ZC_REQUIRE_NONNULL(condRightLowered)));
    blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(entryId), zc::mv(entryStatements),
                          Terminator::gotoBlock(ZC_REQUIRE_NONNULL(headerId))));
  }
  {
    blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(headerId), zc::Vector<Statement>{},
                          Terminator::condBranch(tempOrdinal, ZC_REQUIRE_NONNULL(bodyId),
                                                 ZC_REQUIRE_NONNULL(exitId))));
  }
  {
    zc::Vector<Statement> bodyStatements;
    bodyStatements.add(Statement::arithmetic(localOrdinal, ZC_REQUIRE_NONNULL(arithOp),
                                             Operand::localUse(localOrdinal),
                                             ZC_REQUIRE_NONNULL(arithRightLowered)));
    bodyStatements.add(Statement::compare(tempOrdinal, cmpOp, Operand::localUse(localOrdinal),
                                          ZC_REQUIRE_NONNULL(condRightLowered)));
    blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(bodyId), zc::mv(bodyStatements),
                          Terminator::gotoBlock(breaksToExit ? ZC_REQUIRE_NONNULL(exitId)
                                                             : ZC_REQUIRE_NONNULL(headerId))));
  }
  {
    zc::Vector<Statement> exitStatements;
    exitStatements.add(Statement::assign(resultOrdinal, ZC_REQUIRE_NONNULL(resultLowered)));
    blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(exitId), zc::mv(exitStatements),
                          Terminator::returnLocal(resultOrdinal)));
  }

  zc::Vector<Local> parameters;
  for (size_t i = 0; i < parameterCount; ++i) {
    parameters.add(Local(function.locals[i].id.ordinal(), resultCarrierValue));
  }
  zc::Vector<Local> locals;
  locals.add(Local(localOrdinal, resultCarrierValue));
  locals.add(Local(tempOrdinal, tempCarrierValue));
  locals.add(Local(resultOrdinal, resultCarrierValue));

  // A parameter-free for-loop folds to the reserved no-argument
  // `zom.module_init` entry the runtime `_start` calls; a parameterized body
  // keeps an index-independent reserved symbol and stays object-only.
  zc::String symbol = zc::heapString(parameterCount == 0 ? "zom.module_init" : "zom.for_loop");
  zc::Vector<Function> functions;
  functions.add(Function(function.owner, zc::mv(symbol), resultCarrierValue, zc::mv(parameters),
                         zc::mv(locals), zc::mv(blocks)));
  return Module(zc::mv(functions));
}

zc::Maybe<Module> MirToLirLowering::lowerForLoopAccumulatorReturn(
    const mir::MirFunction& function, const type::SemanticTypeStore& semanticTypes) {
  // Admit only the verified reducible for-loop accumulator return shape,
  // re-checking the structure that mir::validForLoopAccumulatorReturnFunction
  // validated: a parameter-local prefix, N integer user locals (the
  // accumulators), one integer user local (the loop variable), a boolean
  // temporary (the comparison result), and an integer function-result local;
  // a four-block loop (or five-block when an if-guarded break is present)
  // whose entry initializes the accumulators and loop variable and computes
  // the first comparison, whose header switches on the temp, whose optional
  // guard block evaluates the break condition, whose body accumulates into
  // the accumulators, updates the loop variable, and re-computes the
  // comparison, and whose exit copies the first accumulator into the result
  // and returns it.
  if (function.kind != mir::MirFunctionKind::Function || function.sourceScopes.size() != 1 ||
      (function.blocks.size() != 4 && function.blocks.size() != 5) || function.locals.size() < 4) {
    return zc::none;
  }
  const bool hasGuard = function.blocks.size() == 5;

  size_t parameterCount = 0;
  while (parameterCount < function.locals.size() &&
         function.locals[parameterCount].kind == mir::MirLocalKind::Parameter) {
    ++parameterCount;
  }
  // N accumulators + init + temp + [breakTemp] + result = N + 3 (or N + 4
  // with a guard block) non-parameter locals.
  const size_t nonParamCount = function.locals.size() - parameterCount;
  const size_t expectedNonParam = hasGuard ? 5 : 4;
  if (nonParamCount < expectedNonParam) { return zc::none; }
  const size_t accCount = nonParamCount - (hasGuard ? 4 : 3);
  const auto& localDecl = function.locals[parameterCount + accCount];
  const auto& tempDecl = function.locals[parameterCount + accCount + 1];
  const auto& resultDecl = function.locals[parameterCount + accCount + (hasGuard ? 3 : 2)];
  if (localDecl.kind != mir::MirLocalKind::UserLocal ||
      tempDecl.kind != mir::MirLocalKind::Temporary ||
      resultDecl.kind != mir::MirLocalKind::FunctionResult ||
      resultDecl.type != function.resultType) {
    return zc::none;
  }
  if (hasGuard) {
    const auto& breakTempDecl = function.locals[parameterCount + accCount + 2];
    if (breakTempDecl.kind != mir::MirLocalKind::Temporary) { return zc::none; }
  }
  // Validate all accumulator locals.
  for (size_t k = 0; k < accCount; ++k) {
    if (function.locals[parameterCount + k].kind != mir::MirLocalKind::UserLocal) {
      return zc::none;
    }
  }

  auto resultCarrier = integerCarrierFor(function.resultType, semanticTypes);
  if (resultCarrier == zc::none) { return zc::none; }
  const auto resultCarrierValue = ZC_REQUIRE_NONNULL(resultCarrier);
  if (resultCarrierValue.kind() != ValueTypeKind::Integer) { return zc::none; }
  auto tempCarrier = boolCarrierFor(tempDecl.type, semanticTypes);
  if (tempCarrier == zc::none) { return zc::none; }
  const auto tempCarrierValue = ZC_REQUIRE_NONNULL(tempCarrier);
  for (size_t k = 0; k < accCount; ++k) {
    auto carrier = integerCarrierFor(function.locals[parameterCount + k].type, semanticTypes);
    if (carrier == zc::none || ZC_REQUIRE_NONNULL(carrier) != resultCarrierValue) {
      return zc::none;
    }
  }
  {
    auto carrier = integerCarrierFor(localDecl.type, semanticTypes);
    if (carrier == zc::none || ZC_REQUIRE_NONNULL(carrier) != resultCarrierValue) {
      return zc::none;
    }
  }
  for (size_t i = 0; i < parameterCount; ++i) {
    auto carrier = integerCarrierFor(function.locals[i].type, semanticTypes);
    if (carrier == zc::none || ZC_REQUIRE_NONNULL(carrier) != resultCarrierValue) {
      return zc::none;
    }
  }

  const auto& entry = function.blocks[0];
  const auto& header = function.blocks[1];
  const auto& body = function.blocks[hasGuard ? 3 : 2];
  const auto& exit = function.blocks[hasGuard ? 4 : 3];

  // Entry: StorageLive(result); StorageLive(acc_k)...; StorageLive(local);
  // StorageLive(temp); [StorageLive(breakTemp);]
  // Assign(acc_k = constant, Initialize)...;
  // Assign(local = constant, Initialize);
  // Assign(temp = Comparison(op, copy(local), constant), Initialize);
  // Goto(header).
  const size_t expectedEntrySize = (hasGuard ? 6 : 5) + 2 * accCount;
  if (entry.statements.size() != expectedEntrySize) { return zc::none; }
  if (entry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != resultDecl.id) {
    return zc::none;
  }
  for (size_t k = 0; k < accCount; ++k) {
    if (entry.statements[1 + k].kind() != mir::MirStatementKind::StorageLive ||
        entry.statements[1 + k].storageLocal() != function.locals[parameterCount + k].id) {
      return zc::none;
    }
  }
  const size_t initStorageLiveIndex = 1 + accCount;
  const size_t tempStorageLiveIndex = 2 + accCount;
  if (entry.statements[initStorageLiveIndex].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[initStorageLiveIndex].storageLocal() != localDecl.id ||
      entry.statements[tempStorageLiveIndex].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[tempStorageLiveIndex].storageLocal() != tempDecl.id) {
    return zc::none;
  }
  if (hasGuard) {
    const size_t breakStorageLiveIndex = 3 + accCount;
    const auto& breakTempDecl = function.locals[parameterCount + accCount + 2];
    if (entry.statements[breakStorageLiveIndex].kind() != mir::MirStatementKind::StorageLive ||
        entry.statements[breakStorageLiveIndex].storageLocal() != breakTempDecl.id) {
      return zc::none;
    }
  }
  // The break-condition StorageLive shifts all subsequent entry statements.
  const size_t entryOffset = hasGuard ? 1 : 0;
  // Entry accumulator inits.
  zc::Vector<zc::Maybe<Operand>> accInitLowered;
  for (size_t k = 0; k < accCount; ++k) {
    const size_t assignIndex = 3 + accCount + entryOffset + k;
    if (entry.statements[assignIndex].kind() != mir::MirStatementKind::Assign) { return zc::none; }
    const auto& accInitAssign = entry.statements[assignIndex].assignmentValue();
    if (accInitAssign.destination.local() != function.locals[parameterCount + k].id ||
        accInitAssign.destination.projections().size() != 0 ||
        accInitAssign.initialization != mir::MirInitializationKind::Initialize ||
        accInitAssign.value.kind() != mir::MirRvalueKind::Use) {
      return zc::none;
    }
    const auto& accInitOperand = accInitAssign.value.useValue().operand;
    if (accInitOperand.kind() != mir::MirOperandKind::Constant ||
        accInitOperand.constantValue().type != function.locals[parameterCount + k].type) {
      return zc::none;
    }
    auto lowered = lirOperandFor(accInitOperand, resultCarrierValue);
    if (lowered == zc::none) { return zc::none; }
    accInitLowered.add(zc::mv(lowered));
  }
  // Entry loop-variable init.
  const size_t initAssignIndex = 3 + 2 * accCount + entryOffset;
  if (entry.statements[initAssignIndex].kind() != mir::MirStatementKind::Assign) {
    return zc::none;
  }
  const auto& initAssign = entry.statements[initAssignIndex].assignmentValue();
  if (initAssign.destination.local() != localDecl.id ||
      initAssign.destination.projections().size() != 0 ||
      initAssign.initialization != mir::MirInitializationKind::Initialize ||
      initAssign.value.kind() != mir::MirRvalueKind::Use) {
    return zc::none;
  }
  const auto& initOperand = initAssign.value.useValue().operand;
  if (initOperand.kind() != mir::MirOperandKind::Constant ||
      initOperand.constantValue().type != localDecl.type) {
    return zc::none;
  }
  auto initLowered = lirOperandFor(initOperand, resultCarrierValue);
  if (initLowered == zc::none) { return zc::none; }
  // Entry condition comparison.
  const size_t entryCondIndex = 4 + 2 * accCount + entryOffset;
  if (entry.statements[entryCondIndex].kind() != mir::MirStatementKind::Assign) { return zc::none; }
  const auto& entryCondAssign = entry.statements[entryCondIndex].assignmentValue();
  if (entryCondAssign.destination.local() != tempDecl.id ||
      entryCondAssign.destination.projections().size() != 0 ||
      entryCondAssign.initialization != mir::MirInitializationKind::Initialize ||
      entryCondAssign.value.kind() != mir::MirRvalueKind::Comparison) {
    return zc::none;
  }
  const auto& entryComparison = entryCondAssign.value.comparisonValue();
  auto cmpOp = lirComparisonOpFor(entryComparison.op);
  if (entryComparison.resultType != tempDecl.type) { return zc::none; }
  if (entryComparison.left.kind() != mir::MirOperandKind::Copy ||
      entryComparison.left.place().local() != localDecl.id ||
      entryComparison.left.place().projections().size() != 0) {
    return zc::none;
  }
  if (entryComparison.right.kind() != mir::MirOperandKind::Constant ||
      entryComparison.right.constantValue().type != localDecl.type) {
    return zc::none;
  }
  auto condRightLowered = lirOperandFor(entryComparison.right, resultCarrierValue);
  if (condRightLowered == zc::none) { return zc::none; }
  if (entry.terminator.kind() != mir::MirTerminatorKind::Goto ||
      entry.terminator.gotoValue().target != header.id) {
    return zc::none;
  }

  // Header: SwitchInt(copy(temp)) [true -> guard-or-body], default = exit.
  if (header.statements.size() != 0 ||
      header.terminator.kind() != mir::MirTerminatorKind::SwitchInt) {
    return zc::none;
  }
  const auto& switchInt = header.terminator.switchIntValue();
  const auto headerTrueTarget = hasGuard ? function.blocks[2].id : body.id;
  if (switchInt.discriminant.kind() != mir::MirOperandKind::Copy ||
      switchInt.discriminant.place().local() != tempDecl.id ||
      switchInt.discriminant.place().projections().size() != 0 || switchInt.arms.size() != 1 ||
      switchInt.arms[0].target != headerTrueTarget || switchInt.defaultTarget != exit.id) {
    return zc::none;
  }

  // Guard: Assign(breakTemp = Comparison(op, copy(local), constant),
  // Overwrite); SwitchInt(copy(breakTemp)) [true -> exit], default = body.
  ComparisonOp breakCmpOp = ComparisonOp::Eq;
  zc::Maybe<Operand> breakCondRightLowered;
  uint32_t breakTempOrdinal = 0;
  if (hasGuard) {
    const auto& guard = function.blocks[2];
    if (guard.statements.size() != 1 ||
        guard.statements[0].kind() != mir::MirStatementKind::Assign ||
        guard.terminator.kind() != mir::MirTerminatorKind::SwitchInt) {
      return zc::none;
    }
    const auto& breakTempDecl = function.locals[parameterCount + accCount + 2];
    const auto& guardAssign = guard.statements[0].assignmentValue();
    if (guardAssign.destination.local() != breakTempDecl.id ||
        guardAssign.destination.projections().size() != 0 ||
        guardAssign.initialization != mir::MirInitializationKind::Overwrite ||
        guardAssign.value.kind() != mir::MirRvalueKind::Comparison) {
      return zc::none;
    }
    const auto& guardComparison = guardAssign.value.comparisonValue();
    breakCmpOp = lirComparisonOpFor(guardComparison.op);
    if (guardComparison.resultType != breakTempDecl.type) { return zc::none; }
    if (guardComparison.left.kind() != mir::MirOperandKind::Copy ||
        guardComparison.left.place().local() != localDecl.id ||
        guardComparison.left.place().projections().size() != 0) {
      return zc::none;
    }
    if (guardComparison.right.kind() != mir::MirOperandKind::Constant ||
        guardComparison.right.constantValue().type != localDecl.type) {
      return zc::none;
    }
    auto guardRightLowered = lirOperandFor(guardComparison.right, resultCarrierValue);
    if (guardRightLowered == zc::none) { return zc::none; }
    const auto& guardSwitch = guard.terminator.switchIntValue();
    if (guardSwitch.discriminant.kind() != mir::MirOperandKind::Copy ||
        guardSwitch.discriminant.place().local() != breakTempDecl.id ||
        guardSwitch.discriminant.place().projections().size() != 0 ||
        guardSwitch.arms.size() != 1 || guardSwitch.arms[0].target != exit.id ||
        guardSwitch.defaultTarget != body.id) {
      return zc::none;
    }
    breakCondRightLowered = zc::mv(guardRightLowered);
    breakTempOrdinal = breakTempDecl.id.ordinal();
  }

  // Body: Assign(acc_k = Arithmetic(op, copy(acc_k), copy(local)|constant),
  // Overwrite)...; Assign(local = Arithmetic(op, copy(local), constant),
  // Overwrite); Assign(temp = Comparison(op, copy(local), constant),
  // Overwrite); Goto(exit) for a trailing break or Goto(header) back-edge for
  // continue/fallthrough.
  const size_t expectedBodySize = 2 + accCount;
  if (body.statements.size() != expectedBodySize) { return zc::none; }
  for (size_t k = 0; k < accCount; ++k) {
    if (body.statements[k].kind() != mir::MirStatementKind::Assign) { return zc::none; }
  }
  if (body.statements[accCount].kind() != mir::MirStatementKind::Assign ||
      body.statements[accCount + 1].kind() != mir::MirStatementKind::Assign ||
      body.terminator.kind() != mir::MirTerminatorKind::Goto) {
    return zc::none;
  }
  const bool breaksToExit = body.terminator.gotoValue().target == exit.id;
  if (!breaksToExit && body.terminator.gotoValue().target != header.id) { return zc::none; }
  // Body accumulator arithmetic.
  zc::Vector<zc::Maybe<ArithmeticOp>> accArithOps;
  zc::Vector<bool> accArithRhsIsLiteral;
  zc::Vector<zc::Maybe<Operand>> accArithRhsLowered;
  for (size_t k = 0; k < accCount; ++k) {
    const auto& accArithAssign = body.statements[k].assignmentValue();
    if (accArithAssign.destination.local() != function.locals[parameterCount + k].id ||
        accArithAssign.destination.projections().size() != 0 ||
        accArithAssign.initialization != mir::MirInitializationKind::Overwrite ||
        accArithAssign.value.kind() != mir::MirRvalueKind::Arithmetic) {
      return zc::none;
    }
    const auto& accArithmetic = accArithAssign.value.arithmeticValue();
    auto accArithOp = lirArithmeticOpFor(accArithmetic.op);
    if (accArithOp == zc::none) { return zc::none; }
    if (accArithmetic.resultType != function.locals[parameterCount + k].type) { return zc::none; }
    if (accArithmetic.left.kind() != mir::MirOperandKind::Copy ||
        accArithmetic.left.place().local() != function.locals[parameterCount + k].id ||
        accArithmetic.left.place().projections().size() != 0) {
      return zc::none;
    }
    // The right operand is either a copy of the init local or a constant.
    const bool rhsIsLiteral = accArithmetic.right.kind() == mir::MirOperandKind::Constant;
    if (rhsIsLiteral) {
      if (accArithmetic.right.constantValue().type != function.locals[parameterCount + k].type) {
        return zc::none;
      }
      auto rhsLowered = lirOperandFor(accArithmetic.right, resultCarrierValue);
      if (rhsLowered == zc::none) { return zc::none; }
      accArithRhsLowered.add(zc::mv(rhsLowered));
    } else {
      if (accArithmetic.right.kind() != mir::MirOperandKind::Copy ||
          accArithmetic.right.place().local() != localDecl.id ||
          accArithmetic.right.place().projections().size() != 0) {
        return zc::none;
      }
    }
    accArithOps.add(zc::mv(accArithOp));
    accArithRhsIsLiteral.add(rhsIsLiteral);
  }
  // Body update arithmetic.
  const auto& updateAssign = body.statements[accCount].assignmentValue();
  if (updateAssign.destination.local() != localDecl.id ||
      updateAssign.destination.projections().size() != 0 ||
      updateAssign.initialization != mir::MirInitializationKind::Overwrite ||
      updateAssign.value.kind() != mir::MirRvalueKind::Arithmetic) {
    return zc::none;
  }
  const auto& arithmetic = updateAssign.value.arithmeticValue();
  auto arithOp = lirArithmeticOpFor(arithmetic.op);
  if (arithOp == zc::none) { return zc::none; }
  if (arithmetic.resultType != localDecl.type) { return zc::none; }
  if (arithmetic.left.kind() != mir::MirOperandKind::Copy ||
      arithmetic.left.place().local() != localDecl.id ||
      arithmetic.left.place().projections().size() != 0) {
    return zc::none;
  }
  if (arithmetic.right.kind() != mir::MirOperandKind::Constant ||
      arithmetic.right.constantValue().type != localDecl.type) {
    return zc::none;
  }
  auto arithRightLowered = lirOperandFor(arithmetic.right, resultCarrierValue);
  if (arithRightLowered == zc::none) { return zc::none; }
  // Body condition recompute.
  const auto& bodyCondAssign = body.statements[accCount + 1].assignmentValue();
  if (bodyCondAssign.destination.local() != tempDecl.id ||
      bodyCondAssign.destination.projections().size() != 0 ||
      bodyCondAssign.initialization != mir::MirInitializationKind::Overwrite ||
      bodyCondAssign.value.kind() != mir::MirRvalueKind::Comparison) {
    return zc::none;
  }
  const auto& bodyComparison = bodyCondAssign.value.comparisonValue();
  if (bodyComparison.op != entryComparison.op || bodyComparison.resultType != tempDecl.type) {
    return zc::none;
  }
  if (bodyComparison.left.kind() != mir::MirOperandKind::Copy ||
      bodyComparison.left.place().local() != localDecl.id ||
      bodyComparison.left.place().projections().size() != 0) {
    return zc::none;
  }
  if (bodyComparison.right.kind() != mir::MirOperandKind::Constant ||
      bodyComparison.right.constantValue().type != localDecl.type) {
    return zc::none;
  }

  // Exit: Assign(result = copy(acc_0), Initialize); Return(place-use result).
  if (exit.statements.size() != 1 || exit.statements[0].kind() != mir::MirStatementKind::Assign ||
      exit.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& resultAssign = exit.statements[0].assignmentValue();
  if (resultAssign.destination.local() != resultDecl.id ||
      resultAssign.destination.projections().size() != 0 ||
      resultAssign.initialization != mir::MirInitializationKind::Initialize ||
      resultAssign.value.kind() != mir::MirRvalueKind::Use) {
    return zc::none;
  }
  const auto& resultOperand = resultAssign.value.useValue().operand;
  if (resultOperand.kind() != mir::MirOperandKind::Copy ||
      resultOperand.place().local() != function.locals[parameterCount].id ||
      resultOperand.place().projections().size() != 0) {
    return zc::none;
  }
  const auto& returnValue = exit.terminator.returnValue().value;
  if (returnValue == zc::none) { return zc::none; }
  bool returnsResult = false;
  ZC_IF_SOME(value, returnValue) {
    returnsResult = value.kind() != mir::MirOperandKind::Constant &&
                    value.place().local() == resultDecl.id &&
                    value.place().projections().size() == 0;
  }
  if (!returnsResult) { return zc::none; }

  zc::Vector<uint32_t> accOrdinals;
  for (size_t k = 0; k < accCount; ++k) {
    accOrdinals.add(function.locals[parameterCount + k].id.ordinal());
  }
  const uint32_t localOrdinal = localDecl.id.ordinal();
  const uint32_t tempOrdinal = tempDecl.id.ordinal();
  const uint32_t resultOrdinal = resultDecl.id.ordinal();

  auto entryId = LirBlockId::fromOrdinal(1);
  auto headerId = LirBlockId::fromOrdinal(2);
  auto guardId = LirBlockId::fromOrdinal(3);
  auto bodyId = LirBlockId::fromOrdinal(hasGuard ? 4 : 3);
  auto exitId = LirBlockId::fromOrdinal(hasGuard ? 5 : 4);
  if (entryId == zc::none || headerId == zc::none || bodyId == zc::none || exitId == zc::none ||
      (hasGuard && guardId == zc::none)) {
    return zc::none;
  }

  zc::Vector<BasicBlock> blocks;
  {
    zc::Vector<Statement> entryStatements;
    for (size_t k = 0; k < accCount; ++k) {
      entryStatements.add(Statement::assign(accOrdinals[k], ZC_REQUIRE_NONNULL(accInitLowered[k])));
    }
    entryStatements.add(Statement::assign(localOrdinal, ZC_REQUIRE_NONNULL(initLowered)));
    entryStatements.add(Statement::compare(tempOrdinal, cmpOp, Operand::localUse(localOrdinal),
                                           ZC_REQUIRE_NONNULL(condRightLowered)));
    blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(entryId), zc::mv(entryStatements),
                          Terminator::gotoBlock(ZC_REQUIRE_NONNULL(headerId))));
  }
  {
    const auto headerTrueId = hasGuard ? ZC_REQUIRE_NONNULL(guardId) : ZC_REQUIRE_NONNULL(bodyId);
    blocks.add(
        BasicBlock(ZC_REQUIRE_NONNULL(headerId), zc::Vector<Statement>{},
                   Terminator::condBranch(tempOrdinal, headerTrueId, ZC_REQUIRE_NONNULL(exitId))));
  }
  if (hasGuard) {
    zc::Vector<Statement> guardStatements;
    guardStatements.add(Statement::compare(breakTempOrdinal, breakCmpOp,
                                           Operand::localUse(localOrdinal),
                                           ZC_REQUIRE_NONNULL(breakCondRightLowered)));
    blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(guardId), zc::mv(guardStatements),
                          Terminator::condBranch(breakTempOrdinal, ZC_REQUIRE_NONNULL(exitId),
                                                 ZC_REQUIRE_NONNULL(bodyId))));
  }
  {
    zc::Vector<Statement> bodyStatements;
    for (size_t k = 0; k < accCount; ++k) {
      Operand rhsOperand = accArithRhsIsLiteral[k] ? ZC_REQUIRE_NONNULL(accArithRhsLowered[k])
                                                   : Operand::localUse(localOrdinal);
      bodyStatements.add(Statement::arithmetic(accOrdinals[k], ZC_REQUIRE_NONNULL(accArithOps[k]),
                                               Operand::localUse(accOrdinals[k]),
                                               zc::mv(rhsOperand)));
    }
    bodyStatements.add(Statement::arithmetic(localOrdinal, ZC_REQUIRE_NONNULL(arithOp),
                                             Operand::localUse(localOrdinal),
                                             ZC_REQUIRE_NONNULL(arithRightLowered)));
    bodyStatements.add(Statement::compare(tempOrdinal, cmpOp, Operand::localUse(localOrdinal),
                                          ZC_REQUIRE_NONNULL(condRightLowered)));
    blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(bodyId), zc::mv(bodyStatements),
                          Terminator::gotoBlock(breaksToExit ? ZC_REQUIRE_NONNULL(exitId)
                                                             : ZC_REQUIRE_NONNULL(headerId))));
  }
  {
    zc::Vector<Statement> exitStatements;
    exitStatements.add(Statement::assign(resultOrdinal, Operand::localUse(accOrdinals[0])));
    blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(exitId), zc::mv(exitStatements),
                          Terminator::returnLocal(resultOrdinal)));
  }

  zc::Vector<Local> parameters;
  for (size_t i = 0; i < parameterCount; ++i) {
    parameters.add(Local(function.locals[i].id.ordinal(), resultCarrierValue));
  }
  zc::Vector<Local> locals;
  for (size_t k = 0; k < accCount; ++k) { locals.add(Local(accOrdinals[k], resultCarrierValue)); }
  locals.add(Local(localOrdinal, resultCarrierValue));
  locals.add(Local(tempOrdinal, tempCarrierValue));
  if (hasGuard) { locals.add(Local(breakTempOrdinal, tempCarrierValue)); }
  locals.add(Local(resultOrdinal, resultCarrierValue));

  // A parameter-free for-loop accumulator folds to the reserved no-argument
  // `zom.module_init` entry the runtime `_start` calls; a parameterized body
  // keeps an index-independent reserved symbol and stays object-only.
  zc::String symbol =
      zc::heapString(parameterCount == 0 ? "zom.module_init" : "zom.for_loop_accumulator");
  zc::Vector<Function> functions;
  functions.add(Function(function.owner, zc::mv(symbol), resultCarrierValue, zc::mv(parameters),
                         zc::mv(locals), zc::mv(blocks)));
  return Module(zc::mv(functions));
}

zc::Maybe<Module> MirToLirLowering::lowerEqualityConditionalReturn(
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
  const bool isLogicalCondition =
      tempAssign.value.kind() == mir::MirRvalueKind::Arithmetic &&
      (tempAssign.value.arithmeticValue().op == mir::MirArithmeticOperator::BitAnd ||
       tempAssign.value.arithmeticValue().op == mir::MirArithmeticOperator::BitOr);
  if (tempAssign.destination.local() != tempLocalDecl.id ||
      tempAssign.destination.projections().size() != 0 ||
      (tempAssign.value.kind() != mir::MirRvalueKind::Comparison &&
       tempAssign.value.kind() != mir::MirRvalueKind::Arithmetic)) {
    return zc::none;
  }
  // The condition operands must resolve to integer carriers (comparison) or
  // bool carriers (logical short-circuit). Both operands share the operand
  // type; use the result-less operand carrier from the operand types.
  const mir::MirOperand* conditionLeft = nullptr;
  const mir::MirOperand* conditionRight = nullptr;
  if (isLogicalCondition) {
    conditionLeft = &tempAssign.value.arithmeticValue().left;
    conditionRight = &tempAssign.value.arithmeticValue().right;
  } else {
    if (tempAssign.value.kind() != mir::MirRvalueKind::Comparison) { return zc::none; }
    conditionLeft = &tempAssign.value.comparisonValue().left;
    conditionRight = &tempAssign.value.comparisonValue().right;
  }
  auto operandCarrierFor = [&](const mir::MirOperand& operand) -> zc::Maybe<ValueType> {
    if (isLogicalCondition) {
      if (operand.kind() == mir::MirOperandKind::Constant) {
        return boolCarrierFor(operand.constantValue().type, semanticTypes);
      }
      return boolCarrierFor(operand.place().rootType(), semanticTypes);
    }
    if (operand.kind() == mir::MirOperandKind::Constant) {
      return integerCarrierFor(operand.constantValue().type, semanticTypes);
    }
    return integerCarrierFor(operand.place().rootType(), semanticTypes);
  };
  auto leftCarrier = operandCarrierFor(*conditionLeft);
  auto rightCarrier = operandCarrierFor(*conditionRight);
  if (leftCarrier == zc::none || rightCarrier == zc::none) { return zc::none; }
  auto lirLeft = lirOperandFor(*conditionLeft, ZC_REQUIRE_NONNULL(leftCarrier));
  auto lirRight = lirOperandFor(*conditionRight, ZC_REQUIRE_NONNULL(rightCarrier));
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
  auto lowerArm = [&](const mir::MirBasicBlock& branch, zc::Vector<Statement>& out) -> bool {
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
    out.add(Statement::assign(resultOrdinal, ZC_REQUIRE_NONNULL(lowered)));
    return true;
  };
  zc::Vector<Statement> thenStatements;
  zc::Vector<Statement> elseStatements;
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

  zc::Vector<BasicBlock> blocks;
  {
    zc::Vector<Statement> entryStatements;
    if (isLogicalCondition) {
      auto op = lirArithmeticOpFor(tempAssign.value.arithmeticValue().op);
      if (op == zc::none) { return zc::none; }
      entryStatements.add(Statement::arithmetic(tempOrdinal, ZC_REQUIRE_NONNULL(op),
                                                ZC_REQUIRE_NONNULL(lirLeft),
                                                ZC_REQUIRE_NONNULL(lirRight)));
    } else {
      entryStatements.add(
          Statement::compare(tempOrdinal, lirComparisonOpFor(tempAssign.value.comparisonValue().op),
                             ZC_REQUIRE_NONNULL(lirLeft), ZC_REQUIRE_NONNULL(lirRight)));
    }
    blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(entryId), zc::mv(entryStatements),
                          Terminator::condBranch(tempOrdinal, ZC_REQUIRE_NONNULL(thenId),
                                                 ZC_REQUIRE_NONNULL(elseId))));
  }
  blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(thenId), zc::mv(thenStatements),
                        Terminator::gotoBlock(ZC_REQUIRE_NONNULL(joinId))));
  blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(elseId), zc::mv(elseStatements),
                        Terminator::gotoBlock(ZC_REQUIRE_NONNULL(joinId))));
  {
    zc::Vector<Statement> none;
    blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(joinId), zc::mv(none),
                          Terminator::returnLocal(resultOrdinal)));
  }

  // Declare every parameter local plus the result and temp body locals.
  // Logical operator conditionals carry bool parameters; relational comparison
  // conditionals carry integer parameters.
  zc::Vector<Local> parameters;
  for (size_t i = 0; i < parameterCount; ++i) {
    zc::Maybe<ValueType> carrier;
    if (isLogicalCondition) {
      carrier = boolCarrierFor(function.locals[i].type, semanticTypes);
    } else {
      carrier = integerCarrierFor(function.locals[i].type, semanticTypes);
    }
    if (carrier == zc::none) { return zc::none; }
    parameters.add(Local(function.locals[i].id.ordinal(), ZC_REQUIRE_NONNULL(carrier)));
  }
  zc::Vector<Local> locals;
  locals.add(Local(resultOrdinal, resultCarrierValue));
  locals.add(Local(tempOrdinal, tempCarrierValue));

  zc::Vector<Function> functions;
  functions.add(Function(function.owner, zc::heapString("zom.conditional_cmp"), resultCarrierValue,
                         zc::mv(parameters), zc::mv(locals), zc::mv(blocks)));
  return Module(zc::mv(functions));
}

zc::Maybe<Module> MirToLirLowering::lowerLeadingLocalConditionalReturn(
    const mir::MirFunction& function, const type::SemanticTypeStore& semanticTypes) {
  // K leading scalar UserLocal declarations sit between the parameter prefix
  // and the function result / bool comparison temporary: the shape needs at
  // least one leading local plus those two trailing declarations, so three
  // locals is the floor.
  if (function.kind != mir::MirFunctionKind::Function || function.sourceScopes.size() != 1 ||
      function.blocks.size() != 4 || function.locals.size() < 3) {
    return zc::none;
  }
  size_t parameterCount = 0;
  while (parameterCount < function.locals.size() &&
         function.locals[parameterCount].kind == mir::MirLocalKind::Parameter) {
    ++parameterCount;
  }
  if (parameterCount + 2 >= function.locals.size()) return zc::none;
  const size_t leadingLocalCount = function.locals.size() - parameterCount - 2;
  const auto& resultLocalDecl = function.locals[parameterCount + leadingLocalCount];
  const auto& tempLocalDecl = function.locals[parameterCount + leadingLocalCount + 1];
  if (resultLocalDecl.kind != mir::MirLocalKind::FunctionResult ||
      resultLocalDecl.type != function.resultType ||
      tempLocalDecl.kind != mir::MirLocalKind::Temporary) {
    return zc::none;
  }
  for (size_t i = 0; i < parameterCount; ++i) {
    if (function.locals[i].kind != mir::MirLocalKind::Parameter) { return zc::none; }
  }
  for (size_t i = 0; i < leadingLocalCount; ++i) {
    if (function.locals[parameterCount + i].kind != mir::MirLocalKind::UserLocal) {
      return zc::none;
    }
  }

  auto resultCarrier = integerCarrierFor(function.resultType, semanticTypes);
  if (resultCarrier == zc::none) { return zc::none; }
  const auto resultCarrierValue = ZC_REQUIRE_NONNULL(resultCarrier);
  if (resultCarrierValue.kind() != ValueTypeKind::Integer ||
      resultCarrierValue.integerWidth() == IntegerBitWidth::Bit1) {
    return zc::none;
  }
  auto tempCarrier = boolCarrierFor(tempLocalDecl.type, semanticTypes);
  if (tempCarrier == zc::none) { return zc::none; }
  const auto tempCarrierValue = ZC_REQUIRE_NONNULL(tempCarrier);
  for (size_t i = 0; i < function.locals.size(); ++i) {
    if (i >= parameterCount && i < parameterCount + leadingLocalCount + 1) {
      auto carrier = integerCarrierFor(function.locals[i].type, semanticTypes);
      if (carrier == zc::none || ZC_REQUIRE_NONNULL(carrier) != resultCarrierValue) {
        return zc::none;
      }
    }
  }
  for (size_t i = 0; i < parameterCount; ++i) {
    auto carrier = integerCarrierFor(function.locals[i].type, semanticTypes);
    if (carrier == zc::none) { return zc::none; }
  }

  const uint32_t resultOrdinal = resultLocalDecl.id.ordinal();
  const uint32_t tempOrdinal = tempLocalDecl.id.ordinal();

  const auto& entry = function.blocks[0];
  const auto& thenBlock = function.blocks[1];
  const auto& elseBlock = function.blocks[2];
  const auto& joinBlock = function.blocks[3];

  // Entry: per leading local StorageLive plus initializing Assign (2K
  // statements), then StorageLive(result), StorageLive(temp),
  // Assign(temp = Comparison), SwitchInt(copy temp).
  if (entry.statements.size() != leadingLocalCount * 2 + 3 ||
      entry.terminator.kind() != mir::MirTerminatorKind::SwitchInt) {
    return zc::none;
  }
  zc::Vector<Statement> leadingInitializers;
  for (size_t i = 0; i < leadingLocalCount; ++i) {
    const auto& live = entry.statements[i * 2];
    const auto& assign = entry.statements[i * 2 + 1];
    const auto& localDecl = function.locals[parameterCount + i];
    if (live.kind() != mir::MirStatementKind::StorageLive || live.storageLocal() != localDecl.id ||
        assign.kind() != mir::MirStatementKind::Assign) {
      return zc::none;
    }
    const auto& assignment = assign.assignmentValue();
    if (assignment.destination.local() != localDecl.id ||
        assignment.destination.projections().size() != 0) {
      return zc::none;
    }
    if (assignment.value.kind() == mir::MirRvalueKind::Arithmetic) {
      const auto& arithmetic = assignment.value.arithmeticValue();
      auto op = lirArithmeticOpFor(arithmetic.op);
      if (op == zc::none) { return zc::none; }
      auto lirLeft = lirOperandFor(arithmetic.left, resultCarrierValue);
      auto lirRight = lirOperandFor(arithmetic.right, resultCarrierValue);
      if (lirLeft == zc::none || lirRight == zc::none) { return zc::none; }
      leadingInitializers.add(Statement::arithmetic(localDecl.id.ordinal(), ZC_REQUIRE_NONNULL(op),
                                                    ZC_REQUIRE_NONNULL(lirLeft),
                                                    ZC_REQUIRE_NONNULL(lirRight)));
    } else {
      if (assignment.value.kind() != mir::MirRvalueKind::Use) { return zc::none; }
      auto lowered = lirOperandFor(assignment.value.useValue().operand, resultCarrierValue);
      if (lowered == zc::none) { return zc::none; }
      leadingInitializers.add(
          Statement::assign(localDecl.id.ordinal(), ZC_REQUIRE_NONNULL(lowered)));
    }
  }
  if (entry.statements[leadingLocalCount * 2].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[leadingLocalCount * 2].storageLocal() != resultLocalDecl.id ||
      entry.statements[leadingLocalCount * 2 + 1].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[leadingLocalCount * 2 + 1].storageLocal() != tempLocalDecl.id ||
      entry.statements[leadingLocalCount * 2 + 2].kind() != mir::MirStatementKind::Assign) {
    return zc::none;
  }
  const auto& tempAssign = entry.statements[leadingLocalCount * 2 + 2].assignmentValue();
  if (tempAssign.destination.local() != tempLocalDecl.id ||
      tempAssign.destination.projections().size() != 0 ||
      tempAssign.value.kind() != mir::MirRvalueKind::Comparison) {
    return zc::none;
  }
  const auto& comparison = tempAssign.value.comparisonValue();
  auto operandCarrierFor = [&](const mir::MirOperand& operand) -> zc::Maybe<ValueType> {
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

  auto lowerArm = [&](const mir::MirBasicBlock& branch, zc::Vector<Statement>& out) -> bool {
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
    out.add(Statement::assign(resultOrdinal, ZC_REQUIRE_NONNULL(lowered)));
    return true;
  };
  zc::Vector<Statement> thenStatements;
  zc::Vector<Statement> elseStatements;
  if (!lowerArm(thenBlock, thenStatements) || !lowerArm(elseBlock, elseStatements)) {
    return zc::none;
  }

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

  zc::Vector<BasicBlock> blocks;
  {
    zc::Vector<Statement> entryStatements;
    for (size_t i = 0; i < leadingLocalCount; ++i) {
      entryStatements.add(zc::mv(leadingInitializers[i]));
    }
    entryStatements.add(Statement::compare(tempOrdinal, lirComparisonOpFor(comparison.op),
                                           ZC_REQUIRE_NONNULL(lirLeft),
                                           ZC_REQUIRE_NONNULL(lirRight)));
    blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(entryId), zc::mv(entryStatements),
                          Terminator::condBranch(tempOrdinal, ZC_REQUIRE_NONNULL(thenId),
                                                 ZC_REQUIRE_NONNULL(elseId))));
  }
  blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(thenId), zc::mv(thenStatements),
                        Terminator::gotoBlock(ZC_REQUIRE_NONNULL(joinId))));
  blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(elseId), zc::mv(elseStatements),
                        Terminator::gotoBlock(ZC_REQUIRE_NONNULL(joinId))));
  {
    zc::Vector<Statement> none;
    blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(joinId), zc::mv(none),
                          Terminator::returnLocal(resultOrdinal)));
  }

  zc::Vector<Local> parameters;
  for (size_t i = 0; i < parameterCount; ++i) {
    parameters.add(
        Local(function.locals[i].id.ordinal(),
              ZC_REQUIRE_NONNULL(integerCarrierFor(function.locals[i].type, semanticTypes))));
  }
  zc::Vector<Local> locals;
  for (size_t i = 0; i < leadingLocalCount; ++i) {
    locals.add(Local(function.locals[parameterCount + i].id.ordinal(), resultCarrierValue));
  }
  locals.add(Local(resultOrdinal, resultCarrierValue));
  locals.add(Local(tempOrdinal, tempCarrierValue));

  // The runtime `_start` calls the reserved `zom.module_init` symbol, so a
  // no-argument body takes that name and folds into the entry contract. A
  // parameterized body keeps a descriptive symbol and is driven through the
  // host entry path instead.
  zc::String symbol =
      zc::heapString(parameterCount == 0 ? "zom.module_init" : "zom.conditional_local_cmp");
  zc::Vector<Function> functions;
  functions.add(Function(function.owner, zc::mv(symbol), resultCarrierValue, zc::mv(parameters),
                         zc::mv(locals), zc::mv(blocks)));
  return Module(zc::mv(functions));
}

zc::Maybe<Module> MirToLirLowering::lowerTernaryLocalReturn(
    const mir::MirFunction& function, const type::SemanticTypeStore& semanticTypes) {
  // K leading scalar UserLocal declarations sit between the parameter prefix
  // and the ternary result UserLocal. A bool-literal condition adds one
  // Temporary local after the user locals. The shape needs at least the
  // ternary local itself, so one local is the floor.
  if (function.kind != mir::MirFunctionKind::Function || function.sourceScopes.size() != 1 ||
      function.blocks.size() != 4 || function.locals.size() < 2) {
    return zc::none;
  }
  size_t parameterCount = 0;
  while (parameterCount < function.locals.size() &&
         function.locals[parameterCount].kind == mir::MirLocalKind::Parameter) {
    ++parameterCount;
  }
  if (parameterCount >= function.locals.size()) return zc::none;
  // The last local may be a Temporary (bool-literal condition). The ternary
  // result is the last UserLocal before it.
  const bool hasConditionTemp =
      function.locals[function.locals.size() - 1].kind == mir::MirLocalKind::Temporary;
  const size_t userLocalCount =
      function.locals.size() - parameterCount - (hasConditionTemp ? 1 : 0);
  if (userLocalCount < 1) return zc::none;
  const size_t leadingLocalCount = userLocalCount - 1;
  const auto& ternaryLocalDecl = function.locals[parameterCount + leadingLocalCount];
  if (ternaryLocalDecl.kind != mir::MirLocalKind::UserLocal ||
      ternaryLocalDecl.type != function.resultType) {
    return zc::none;
  }
  for (size_t i = 0; i < leadingLocalCount; ++i) {
    if (function.locals[parameterCount + i].kind != mir::MirLocalKind::UserLocal) {
      return zc::none;
    }
  }
  const mir::MirLocalDeclaration* conditionTempDecl = nullptr;
  if (hasConditionTemp) { conditionTempDecl = &function.locals[function.locals.size() - 1]; }

  auto resultCarrier = integerCarrierFor(function.resultType, semanticTypes);
  if (resultCarrier == zc::none) { return zc::none; }
  const auto resultCarrierValue = ZC_REQUIRE_NONNULL(resultCarrier);
  if (resultCarrierValue.kind() != ValueTypeKind::Integer ||
      resultCarrierValue.integerWidth() == IntegerBitWidth::Bit1) {
    return zc::none;
  }

  const uint32_t ternaryOrdinal = ternaryLocalDecl.id.ordinal();

  const auto& entry = function.blocks[0];
  const auto& thenBlock = function.blocks[1];
  const auto& elseBlock = function.blocks[2];
  const auto& joinBlock = function.blocks[3];

  // Entry: per leading local StorageLive plus initializing Assign (2K
  // statements), then StorageLive(ternary), then optionally
  // StorageLive(temp) + Assign(temp = boolLiteral), then SwitchInt.
  const size_t expectedEntryStatements = leadingLocalCount * 2 + 1 + (hasConditionTemp ? 2 : 0);
  if (entry.statements.size() != expectedEntryStatements ||
      entry.terminator.kind() != mir::MirTerminatorKind::SwitchInt) {
    return zc::none;
  }
  zc::Vector<Operand> leadingInitializers;
  for (size_t i = 0; i < leadingLocalCount; ++i) {
    const auto& live = entry.statements[i * 2];
    const auto& assign = entry.statements[i * 2 + 1];
    const auto& localDecl = function.locals[parameterCount + i];
    if (live.kind() != mir::MirStatementKind::StorageLive || live.storageLocal() != localDecl.id ||
        assign.kind() != mir::MirStatementKind::Assign) {
      return zc::none;
    }
    const auto& assignment = assign.assignmentValue();
    if (assignment.destination.local() != localDecl.id ||
        assignment.destination.projections().size() != 0 ||
        assignment.value.kind() != mir::MirRvalueKind::Use) {
      return zc::none;
    }
    auto localCarrier = integerCarrierFor(localDecl.type, semanticTypes);
    if (localCarrier == zc::none) { localCarrier = boolCarrierFor(localDecl.type, semanticTypes); }
    if (localCarrier == zc::none) { return zc::none; }
    auto lowered =
        lirOperandFor(assignment.value.useValue().operand, ZC_REQUIRE_NONNULL(localCarrier));
    if (lowered == zc::none) { return zc::none; }
    leadingInitializers.add(ZC_REQUIRE_NONNULL(lowered));
  }
  if (entry.statements[leadingLocalCount * 2].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[leadingLocalCount * 2].storageLocal() != ternaryLocalDecl.id) {
    return zc::none;
  }
  // A bool-literal condition adds StorageLive(temp) + Assign(temp = literal).
  zc::Maybe<Operand> conditionTempInitializer;
  if (hasConditionTemp) {
    const auto& tempLive = entry.statements[leadingLocalCount * 2 + 1];
    const auto& tempAssign = entry.statements[leadingLocalCount * 2 + 2];
    if (tempLive.kind() != mir::MirStatementKind::StorageLive ||
        tempLive.storageLocal() != conditionTempDecl->id ||
        tempAssign.kind() != mir::MirStatementKind::Assign) {
      return zc::none;
    }
    const auto& assignment = tempAssign.assignmentValue();
    if (assignment.destination.local() != conditionTempDecl->id ||
        assignment.destination.projections().size() != 0 ||
        assignment.value.kind() != mir::MirRvalueKind::Use) {
      return zc::none;
    }
    auto tempCarrier = boolCarrierFor(conditionTempDecl->type, semanticTypes);
    if (tempCarrier == zc::none) { return zc::none; }
    auto lowered =
        lirOperandFor(assignment.value.useValue().operand, ZC_REQUIRE_NONNULL(tempCarrier));
    if (lowered == zc::none) { return zc::none; }
    conditionTempInitializer = zc::mv(lowered);
  }

  const auto& switchInt = entry.terminator.switchIntValue();
  if (switchInt.discriminant.kind() != mir::MirOperandKind::Copy ||
      switchInt.discriminant.place().projections().size() != 0 || switchInt.arms.size() != 2 ||
      switchInt.arms[0].target != thenBlock.id || switchInt.arms[1].target != elseBlock.id ||
      switchInt.defaultTarget != elseBlock.id) {
    return zc::none;
  }
  // The discriminant is a bool temporary, parameter, or earlier leading local.
  const uint32_t conditionOrdinal = switchInt.discriminant.place().local().ordinal();
  bool conditionIsBool = false;
  for (size_t i = 0; i < function.locals.size(); ++i) {
    if (function.locals[i].id.ordinal() == conditionOrdinal) {
      if (i == parameterCount + leadingLocalCount) return zc::none;
      auto carrier = boolCarrierFor(function.locals[i].type, semanticTypes);
      if (carrier == zc::none) { return zc::none; }
      conditionIsBool = true;
      break;
    }
  }
  if (!conditionIsBool) { return zc::none; }

  auto lowerArm = [&](const mir::MirBasicBlock& branch, zc::Vector<Statement>& out) -> bool {
    if (branch.statements.size() != 1 ||
        branch.statements[0].kind() != mir::MirStatementKind::Assign ||
        branch.terminator.kind() != mir::MirTerminatorKind::Goto ||
        branch.terminator.gotoValue().target != joinBlock.id) {
      return false;
    }
    const auto& assignment = branch.statements[0].assignmentValue();
    if (assignment.destination.local() != ternaryLocalDecl.id ||
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
    out.add(Statement::assign(ternaryOrdinal, ZC_REQUIRE_NONNULL(lowered)));
    return true;
  };
  zc::Vector<Statement> thenStatements;
  zc::Vector<Statement> elseStatements;
  if (!lowerArm(thenBlock, thenStatements) || !lowerArm(elseBlock, elseStatements)) {
    return zc::none;
  }

  if (joinBlock.statements.size() != 0 ||
      joinBlock.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& returnValue = joinBlock.terminator.returnValue().value;
  if (returnValue == zc::none) { return zc::none; }
  bool returnsTernary = false;
  ZC_IF_SOME(value, returnValue) {
    returnsTernary = value.kind() != mir::MirOperandKind::Constant &&
                     value.place().local() == ternaryLocalDecl.id &&
                     value.place().projections().size() == 0;
  }
  if (!returnsTernary) { return zc::none; }

  auto entryId = LirBlockId::fromOrdinal(1);
  auto thenId = LirBlockId::fromOrdinal(2);
  auto elseId = LirBlockId::fromOrdinal(3);
  auto joinId = LirBlockId::fromOrdinal(4);
  if (entryId == zc::none || thenId == zc::none || elseId == zc::none || joinId == zc::none) {
    return zc::none;
  }

  zc::Vector<BasicBlock> blocks;
  {
    zc::Vector<Statement> entryStatements;
    for (size_t i = 0; i < leadingLocalCount; ++i) {
      entryStatements.add(Statement::assign(function.locals[parameterCount + i].id.ordinal(),
                                            zc::mv(leadingInitializers[i])));
    }
    if (hasConditionTemp) {
      entryStatements.add(Statement::assign(conditionTempDecl->id.ordinal(),
                                            ZC_REQUIRE_NONNULL(conditionTempInitializer)));
    }
    blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(entryId), zc::mv(entryStatements),
                          Terminator::condBranch(conditionOrdinal, ZC_REQUIRE_NONNULL(thenId),
                                                 ZC_REQUIRE_NONNULL(elseId))));
  }
  blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(thenId), zc::mv(thenStatements),
                        Terminator::gotoBlock(ZC_REQUIRE_NONNULL(joinId))));
  blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(elseId), zc::mv(elseStatements),
                        Terminator::gotoBlock(ZC_REQUIRE_NONNULL(joinId))));
  {
    zc::Vector<Statement> none;
    blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(joinId), zc::mv(none),
                          Terminator::returnLocal(ternaryOrdinal)));
  }

  zc::Vector<Local> parameters;
  for (size_t i = 0; i < parameterCount; ++i) {
    const auto& parameterLocal = function.locals[i];
    zc::Maybe<ValueType> carrier = boolCarrierFor(parameterLocal.type, semanticTypes);
    if (carrier == zc::none) { carrier = integerCarrierFor(parameterLocal.type, semanticTypes); }
    if (carrier == zc::none) { return zc::none; }
    parameters.add(Local(parameterLocal.id.ordinal(), ZC_REQUIRE_NONNULL(carrier)));
  }
  zc::Vector<Local> locals;
  for (size_t i = 0; i < leadingLocalCount; ++i) {
    const auto& localDecl = function.locals[parameterCount + i];
    zc::Maybe<ValueType> carrier = integerCarrierFor(localDecl.type, semanticTypes);
    if (carrier == zc::none) { carrier = boolCarrierFor(localDecl.type, semanticTypes); }
    if (carrier == zc::none) { return zc::none; }
    locals.add(Local(localDecl.id.ordinal(), ZC_REQUIRE_NONNULL(carrier)));
  }
  locals.add(Local(ternaryOrdinal, resultCarrierValue));
  if (hasConditionTemp) {
    auto tempCarrier = boolCarrierFor(conditionTempDecl->type, semanticTypes);
    if (tempCarrier == zc::none) { return zc::none; }
    locals.add(Local(conditionTempDecl->id.ordinal(), ZC_REQUIRE_NONNULL(tempCarrier)));
  }

  // The runtime `_start` calls the reserved `zom.module_init` symbol, so a
  // no-argument body takes that name and folds into the entry contract. A
  // parameterized body keeps a descriptive symbol and is driven through the
  // host entry path instead.
  zc::String symbol = zc::heapString(parameterCount == 0 ? "zom.module_init" : "zom.ternary");
  zc::Vector<Function> functions;
  functions.add(Function(function.owner, zc::mv(symbol), resultCarrierValue, zc::mv(parameters),
                         zc::mv(locals), zc::mv(blocks)));
  return Module(zc::mv(functions));
}

zc::Maybe<Module> MirToLirLowering::lowerArithmeticReturn(
    const mir::MirFunction& function, const type::SemanticTypeStore& semanticTypes) {
  // Admit only the verified one-block arithmetic shape: a leading run of
  // integer parameter locals followed by one or more body locals, each brought
  // to life by StorageLive plus an initializing Assign of a Use or Arithmetic
  // rvalue, and a place-copy return of the last local. Every carrier is one
  // equal integer width (Bit1 admitted for bool carriers).
  if (function.kind != mir::MirFunctionKind::Function || function.sourceScopes.size() != 1 ||
      function.blocks.size() != 1 || function.locals.size() < 1) {
    return zc::none;
  }

  size_t parameterCount = 0;
  while (parameterCount < function.locals.size() &&
         function.locals[parameterCount].kind == mir::MirLocalKind::Parameter) {
    ++parameterCount;
  }
  const size_t bodyLocalCount = function.locals.size() - parameterCount;
  if (bodyLocalCount == 0) { return zc::none; }
  for (size_t i = parameterCount; i < function.locals.size(); ++i) {
    const auto kind = function.locals[i].kind;
    if (kind != mir::MirLocalKind::UserLocal && kind != mir::MirLocalKind::Temporary &&
        kind != mir::MirLocalKind::FunctionResult) {
      return zc::none;
    }
  }

  auto resultCarrier = integerCarrierFor(function.resultType, semanticTypes);
  if (resultCarrier == zc::none) {
    resultCarrier = boolCarrierFor(function.resultType, semanticTypes);
  }
  if (resultCarrier == zc::none) {
    resultCarrier = enumCarrierFor(function.resultType, semanticTypes);
  }
  if (resultCarrier == zc::none) { return zc::none; }
  const auto resultCarrierValue = ZC_REQUIRE_NONNULL(resultCarrier);
  if (resultCarrierValue.kind() != ValueTypeKind::Integer) { return zc::none; }
  for (const auto& local : function.locals) {
    auto carrier = integerCarrierFor(local.type, semanticTypes);
    if (carrier == zc::none) { carrier = boolCarrierFor(local.type, semanticTypes); }
    if (carrier == zc::none) { carrier = enumCarrierFor(local.type, semanticTypes); }
    if (carrier == zc::none || ZC_REQUIRE_NONNULL(carrier) != resultCarrierValue) {
      return zc::none;
    }
  }

  const auto& block = function.blocks[0];
  if (block.statements.size() != bodyLocalCount * 2) { return zc::none; }

  auto leafOperand = [&](const mir::MirOperand& operand) -> zc::Maybe<Operand> {
    if (operand.kind() == mir::MirOperandKind::Constant) {
      return lirOperandFor(operand, resultCarrierValue);
    }
    if (operand.place().projections().size() != 0) { return zc::none; }
    const uint32_t ordinal = operand.place().local().ordinal();
    bool declared = false;
    for (const auto& local : function.locals) {
      if (local.id.ordinal() == ordinal) {
        declared = true;
        break;
      }
    }
    if (!declared) { return zc::none; }
    return Operand::localUse(ordinal);
  };

  zc::Vector<Statement> statements;
  for (size_t j = 0; j < bodyLocalCount; ++j) {
    const auto& localDecl = function.locals[parameterCount + j];
    const auto& liveStatement = block.statements[2 * j];
    const auto& assignStatement = block.statements[2 * j + 1];
    if (liveStatement.kind() != mir::MirStatementKind::StorageLive ||
        liveStatement.storageLocal() != localDecl.id ||
        assignStatement.kind() != mir::MirStatementKind::Assign) {
      return zc::none;
    }
    const auto& assignment = assignStatement.assignmentValue();
    if (assignment.destination.local() != localDecl.id ||
        assignment.destination.projections().size() != 0 ||
        assignment.initialization != mir::MirInitializationKind::Initialize) {
      return zc::none;
    }
    const uint32_t destinationOrdinal = localDecl.id.ordinal();
    if (assignment.value.kind() == mir::MirRvalueKind::Use) {
      auto lowered = leafOperand(assignment.value.useValue().operand);
      if (lowered == zc::none) { return zc::none; }
      statements.add(Statement::assign(destinationOrdinal, ZC_REQUIRE_NONNULL(lowered)));
    } else if (assignment.value.kind() == mir::MirRvalueKind::Arithmetic) {
      const auto& arithmetic = assignment.value.arithmeticValue();
      auto op = lirArithmeticOpFor(arithmetic.op);
      if (op == zc::none) { return zc::none; }
      if (arithmetic.resultType != localDecl.type) { return zc::none; }
      auto left = leafOperand(arithmetic.left);
      auto right = leafOperand(arithmetic.right);
      if (left == zc::none || right == zc::none) { return zc::none; }
      statements.add(Statement::arithmetic(destinationOrdinal, ZC_REQUIRE_NONNULL(op),
                                           ZC_REQUIRE_NONNULL(left), ZC_REQUIRE_NONNULL(right)));
    } else if (assignment.value.kind() == mir::MirRvalueKind::Comparison) {
      const auto& comparison = assignment.value.comparisonValue();
      auto op = lirComparisonOpFor(comparison.op);
      if (comparison.resultType != localDecl.type) { return zc::none; }
      auto left = leafOperand(comparison.left);
      auto right = leafOperand(comparison.right);
      if (left == zc::none || right == zc::none) { return zc::none; }
      statements.add(Statement::compare(destinationOrdinal, op, ZC_REQUIRE_NONNULL(left),
                                        ZC_REQUIRE_NONNULL(right)));
    } else {
      return zc::none;
    }
  }

  if (block.terminator.kind() != mir::MirTerminatorKind::Return) { return zc::none; }
  const auto& returnValue = block.terminator.returnValue().value;
  const auto& lastLocalDecl = function.locals[function.locals.size() - 1];
  if (returnValue == zc::none) { return zc::none; }
  bool returnsLastLocal = false;
  ZC_IF_SOME(value, returnValue) {
    returnsLastLocal = value.kind() != mir::MirOperandKind::Constant &&
                       value.place().local() == lastLocalDecl.id &&
                       value.place().projections().size() == 0;
  }
  if (!returnsLastLocal) { return zc::none; }

  auto entryId = LirBlockId::fromOrdinal(1);
  if (entryId == zc::none) { return zc::none; }
  zc::Vector<BasicBlock> blocks;
  blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(entryId), zc::mv(statements),
                        Terminator::returnLocal(lastLocalDecl.id.ordinal())));

  zc::Vector<Local> parameters;
  for (size_t i = 0; i < parameterCount; ++i) {
    parameters.add(Local(function.locals[i].id.ordinal(), resultCarrierValue));
  }
  zc::Vector<Local> locals;
  for (size_t i = parameterCount; i < function.locals.size(); ++i) {
    locals.add(Local(function.locals[i].id.ordinal(), resultCarrierValue));
  }

  // A parameter-free arithmetic body folds to the reserved no-argument
  // `zom.module_init` entry the runtime `_start` calls; a parameterized body
  // keeps an index-independent reserved symbol and stays object-only.
  zc::String symbol = zc::heapString(parameterCount == 0 ? "zom.module_init" : "zom.arithmetic");
  zc::Vector<Function> functions;
  functions.add(Function(function.owner, zc::mv(symbol), resultCarrierValue, zc::mv(parameters),
                         zc::mv(locals), zc::mv(blocks)));
  return Module(zc::mv(functions));
}

zc::Maybe<Module> MirToLirLowering::lowerScalarLocalOverwriteReturn(
    const mir::MirFunction& function, const type::SemanticTypeStore& semanticTypes) {
  // Admit only the verified one-block overwrite shape: a leading run of integer
  // parameter locals followed by exactly one user local, brought to life by
  // StorageLive plus an initializing Assign of a constant, then overwritten by
  // one or more Arithmetic or Comparison rvalues whose operands are constants
  // or place-uses of the parameter locals or the user local itself, and a
  // place-copy return of that local. Every carrier is one equal integer
  // width (Bit1 admitted for bool carriers).
  if (function.kind != mir::MirFunctionKind::Function || function.sourceScopes.size() != 1 ||
      function.blocks.size() != 1 || function.locals.size() < 1) {
    return zc::none;
  }

  size_t parameterCount = 0;
  while (parameterCount < function.locals.size() &&
         function.locals[parameterCount].kind == mir::MirLocalKind::Parameter) {
    ++parameterCount;
  }
  const size_t bodyLocalCount = function.locals.size() - parameterCount;
  if (bodyLocalCount != 1) { return zc::none; }
  const auto& localDecl = function.locals[parameterCount];
  if (localDecl.kind != mir::MirLocalKind::UserLocal) { return zc::none; }

  auto resultCarrier = integerCarrierFor(function.resultType, semanticTypes);
  if (resultCarrier == zc::none) {
    resultCarrier = boolCarrierFor(function.resultType, semanticTypes);
  }
  if (resultCarrier == zc::none) { return zc::none; }
  const auto resultCarrierValue = ZC_REQUIRE_NONNULL(resultCarrier);
  if (resultCarrierValue.kind() != ValueTypeKind::Integer) { return zc::none; }
  for (const auto& local : function.locals) {
    auto carrier = integerCarrierFor(local.type, semanticTypes);
    if (carrier == zc::none) { carrier = boolCarrierFor(local.type, semanticTypes); }
    if (carrier == zc::none || ZC_REQUIRE_NONNULL(carrier) != resultCarrierValue) {
      return zc::none;
    }
  }

  const auto& block = function.blocks[0];
  if (block.statements.size() < 3) { return zc::none; }

  const auto& liveStatement = block.statements[0];
  const auto& initializeStatement = block.statements[1];
  if (liveStatement.kind() != mir::MirStatementKind::StorageLive ||
      liveStatement.storageLocal() != localDecl.id ||
      initializeStatement.kind() != mir::MirStatementKind::Assign) {
    return zc::none;
  }

  const auto& initializeAssign = initializeStatement.assignmentValue();
  if (initializeAssign.destination.local() != localDecl.id ||
      initializeAssign.destination.projections().size() != 0 ||
      initializeAssign.initialization != mir::MirInitializationKind::Initialize ||
      initializeAssign.value.kind() != mir::MirRvalueKind::Use) {
    return zc::none;
  }
  const auto& initOperand = initializeAssign.value.useValue().operand;
  if (initOperand.kind() != mir::MirOperandKind::Constant ||
      initOperand.constantValue().type != localDecl.type) {
    return zc::none;
  }

  auto leafOperand = [&](const mir::MirOperand& operand) -> zc::Maybe<Operand> {
    if (operand.kind() == mir::MirOperandKind::Constant) {
      return lirOperandFor(operand, resultCarrierValue);
    }
    if (operand.place().projections().size() != 0) { return zc::none; }
    const uint32_t ordinal = operand.place().local().ordinal();
    bool declared = false;
    for (const auto& local : function.locals) {
      if (local.id.ordinal() == ordinal) {
        declared = true;
        break;
      }
    }
    if (!declared) { return zc::none; }
    return Operand::localUse(ordinal);
  };

  const uint32_t destinationOrdinal = localDecl.id.ordinal();
  zc::Vector<Statement> statements;
  auto initLowered = leafOperand(initOperand);
  if (initLowered == zc::none) { return zc::none; }
  statements.add(Statement::assign(destinationOrdinal, ZC_REQUIRE_NONNULL(initLowered)));

  // Lower each overwrite write (statements 2 through end-1).
  for (size_t i = 2; i < block.statements.size(); ++i) {
    const auto& overwriteStatement = block.statements[i];
    if (overwriteStatement.kind() != mir::MirStatementKind::Assign) { return zc::none; }
    const auto& overwriteAssign = overwriteStatement.assignmentValue();
    if (overwriteAssign.destination.local() != localDecl.id ||
        overwriteAssign.destination.projections().size() != 0 ||
        overwriteAssign.initialization != mir::MirInitializationKind::Overwrite) {
      return zc::none;
    }

    if (overwriteAssign.value.kind() == mir::MirRvalueKind::Use) {
      auto lowered = leafOperand(overwriteAssign.value.useValue().operand);
      if (lowered == zc::none) { return zc::none; }
      statements.add(Statement::assign(destinationOrdinal, ZC_REQUIRE_NONNULL(lowered)));
    } else if (overwriteAssign.value.kind() == mir::MirRvalueKind::Arithmetic) {
      const auto& arithmetic = overwriteAssign.value.arithmeticValue();
      auto op = lirArithmeticOpFor(arithmetic.op);
      if (op == zc::none) { return zc::none; }
      if (arithmetic.resultType != localDecl.type) { return zc::none; }
      auto left = leafOperand(arithmetic.left);
      auto right = leafOperand(arithmetic.right);
      if (left == zc::none || right == zc::none) { return zc::none; }
      statements.add(Statement::arithmetic(destinationOrdinal, ZC_REQUIRE_NONNULL(op),
                                           ZC_REQUIRE_NONNULL(left), ZC_REQUIRE_NONNULL(right)));
    } else if (overwriteAssign.value.kind() == mir::MirRvalueKind::Comparison) {
      const auto& comparison = overwriteAssign.value.comparisonValue();
      auto op = lirComparisonOpFor(comparison.op);
      if (comparison.resultType != localDecl.type) { return zc::none; }
      auto left = leafOperand(comparison.left);
      auto right = leafOperand(comparison.right);
      if (left == zc::none || right == zc::none) { return zc::none; }
      statements.add(Statement::compare(destinationOrdinal, op, ZC_REQUIRE_NONNULL(left),
                                        ZC_REQUIRE_NONNULL(right)));
    } else {
      return zc::none;
    }
  }

  if (block.terminator.kind() != mir::MirTerminatorKind::Return) { return zc::none; }
  const auto& returnValue = block.terminator.returnValue().value;
  if (returnValue == zc::none) { return zc::none; }
  bool returnsLocal = false;
  ZC_IF_SOME(value, returnValue) {
    returnsLocal = value.kind() != mir::MirOperandKind::Constant &&
                   value.place().local() == localDecl.id && value.place().projections().size() == 0;
  }
  if (!returnsLocal) { return zc::none; }

  auto entryId = LirBlockId::fromOrdinal(1);
  if (entryId == zc::none) { return zc::none; }
  zc::Vector<BasicBlock> blocks;
  blocks.add(BasicBlock(ZC_REQUIRE_NONNULL(entryId), zc::mv(statements),
                        Terminator::returnLocal(destinationOrdinal)));

  zc::Vector<Local> parameters;
  for (size_t i = 0; i < parameterCount; ++i) {
    parameters.add(Local(function.locals[i].id.ordinal(), resultCarrierValue));
  }
  zc::Vector<Local> locals;
  locals.add(Local(destinationOrdinal, resultCarrierValue));

  // A parameter-free overwrite body folds to the reserved no-argument
  // `zom.module_init` entry the runtime `_start` calls; a parameterized body
  // keeps an index-independent reserved symbol and stays object-only.
  zc::String symbol = zc::heapString(parameterCount == 0 ? "zom.module_init" : "zom.overwrite");
  zc::Vector<Function> functions;
  functions.add(Function(function.owner, zc::mv(symbol), resultCarrierValue, zc::mv(parameters),
                         zc::mv(locals), zc::mv(blocks)));
  return Module(zc::mv(functions));
}

zc::Maybe<Module> MirToLirLowering::lowerCallModule(const mir::MirFunction& caller,
                                                    const mir::MirFunction& callee,
                                                    const type::SemanticTypeStore& semanticTypes) {
  // Callee: the scalar constant-return shape (no locals, one block returning an
  // integer constant). Build its single-block ReturnInteger LIR function.
  if (callee.kind != mir::MirFunctionKind::Function || callee.locals.size() != 0 ||
      callee.blocks.size() != 1) {
    return zc::none;
  }
  auto calleeCarrier = integerCarrierFor(callee.resultType, semanticTypes);
  if (calleeCarrier == zc::none) { return zc::none; }
  const auto calleeCarrierValue = ZC_REQUIRE_NONNULL(calleeCarrier);
  const auto& calleeBlock = callee.blocks[0];
  if (calleeBlock.statements.size() != 0 ||
      calleeBlock.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& calleeReturn = calleeBlock.terminator.returnValue().value;
  if (calleeReturn == zc::none) { return zc::none; }
  zc::Maybe<IntegerConstant> calleeConstant;
  ZC_IF_SOME(value, calleeReturn) {
    if (value.kind() != mir::MirOperandKind::Constant ||
        value.constantValue().type != callee.resultType) {
      return zc::none;
    }
    const auto integer = value.constantValue().value.integerValue();
    if (integer == zc::none) { return zc::none; }
    auto bits = zeroExtendedBits(ZC_REQUIRE_NONNULL(integer), calleeCarrierValue.integerWidth());
    if (bits == zc::none) { return zc::none; }
    calleeConstant = IntegerConstant::from(calleeCarrierValue, ZC_REQUIRE_NONNULL(bits));
  }
  if (calleeConstant == zc::none) { return zc::none; }

  // Caller: P leading parameter locals followed by one integer result local, two
  // blocks (entry StorageLive(result) + Call, continuation Return of the result).
  // This call carries no arguments.
  if (caller.kind != mir::MirFunctionKind::Function || caller.locals.size() < 1 ||
      caller.blocks.size() != 2 || caller.resultType != callee.resultType) {
    return zc::none;
  }
  const size_t callerParameterCount = caller.locals.size() - 1;
  for (size_t p = 0; p < callerParameterCount; ++p) {
    if (caller.locals[p].kind != mir::MirLocalKind::Parameter) { return zc::none; }
  }
  const auto& resultLocal = caller.locals[callerParameterCount];
  if (resultLocal.type != caller.resultType) { return zc::none; }
  auto callerCarrier = integerCarrierFor(caller.resultType, semanticTypes);
  if (callerCarrier == zc::none) { return zc::none; }
  const auto callerCarrierValue = ZC_REQUIRE_NONNULL(callerCarrier);
  const uint32_t resultOrdinal = resultLocal.id.ordinal();

  const auto& entry = caller.blocks[0];
  const auto& continuation = caller.blocks[1];
  if (entry.statements.size() != 1 ||
      entry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != resultLocal.id ||
      entry.terminator.kind() != mir::MirTerminatorKind::Call) {
    return zc::none;
  }
  const auto& call = entry.terminator.callValue();
  if (call.arguments.size() != 0 || call.destination.local() != resultLocal.id ||
      call.destination.projections().size() != 0 || call.normalTarget != continuation.id ||
      call.unwindTarget != zc::none) {
    return zc::none;
  }
  // The call's target definition must be the callee being lowered; otherwise the
  // caller would be wired to the wrong module-local function index.
  if (!(call.callee == callee.owner)) { return zc::none; }
  if (continuation.statements.size() != 0 ||
      continuation.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& continuationReturn = continuation.terminator.returnValue().value;
  if (continuationReturn == zc::none) { return zc::none; }
  bool returnsResult = false;
  ZC_IF_SOME(value, continuationReturn) {
    returnsResult = value.kind() != mir::MirOperandKind::Constant &&
                    value.place().local() == resultLocal.id &&
                    value.place().projections().size() == 0;
  }
  if (!returnsResult) { return zc::none; }

  auto callerEntryId = LirBlockId::fromOrdinal(1);
  auto callerContId = LirBlockId::fromOrdinal(2);
  auto calleeEntryId = LirBlockId::fromOrdinal(1);
  if (callerEntryId == zc::none || callerContId == zc::none || calleeEntryId == zc::none) {
    return zc::none;
  }

  zc::Vector<Function> functions;

  // Function 0: the caller. Its call targets function index 1 (the callee),
  // stores the integer result into the result slot, and continues to the return.
  {
    zc::Vector<BasicBlock> callerBlocks;
    zc::Vector<Statement> entryStatements;
    zc::Vector<Operand> noArguments;
    auto callTerminator = Terminator::callFunction(
        /*calleeIndex=*/1, resultOrdinal, zc::mv(noArguments), ZC_REQUIRE_NONNULL(callerContId));
    if (callTerminator == zc::none) { return zc::none; }
    callerBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(callerEntryId), zc::mv(entryStatements),
                                ZC_REQUIRE_NONNULL(zc::mv(callTerminator))));
    zc::Vector<Statement> contStatements;
    callerBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(callerContId), zc::mv(contStatements),
                                Terminator::returnLocal(resultOrdinal)));
    zc::Vector<Local> parameters;
    for (size_t p = 0; p < callerParameterCount; ++p) {
      auto carrier = integerCarrierFor(caller.locals[p].type, semanticTypes);
      if (carrier == zc::none) { return zc::none; }
      parameters.add(Local(caller.locals[p].id.ordinal(), ZC_REQUIRE_NONNULL(carrier)));
    }
    zc::Vector<Local> locals;
    locals.add(Local(resultOrdinal, callerCarrierValue));
    functions.add(Function(caller.owner, moduleEntrySymbol(callerParameterCount),
                           callerCarrierValue, zc::mv(parameters), zc::mv(locals),
                           zc::mv(callerBlocks)));
  }

  // Function 1: the callee, a single block returning the integer constant.
  {
    zc::Vector<BasicBlock> calleeBlocks;
    calleeBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(calleeEntryId),
                                Terminator::returnInteger(ZC_REQUIRE_NONNULL(calleeConstant))));
    functions.add(Function(callee.owner, zc::heapString("zom.callee"), calleeCarrierValue,
                           zc::mv(calleeBlocks)));
  }

  return Module(zc::mv(functions));
}

zc::Maybe<Module> MirToLirLowering::lowerCallModuleWithArgument(
    const mir::MirFunction& caller, const mir::MirFunction& callee,
    const type::SemanticTypeStore& semanticTypes) {
  // Callee: the single-parameter return shape (one Parameter local, one block
  // returning a place-use of that parameter). Build a one-parameter LIR function
  // whose single block returns the parameter slot.
  if (callee.kind != mir::MirFunctionKind::Function || callee.locals.size() != 1 ||
      callee.blocks.size() != 1) {
    return zc::none;
  }
  const auto& calleeParam = callee.locals[0];
  if (calleeParam.kind != mir::MirLocalKind::Parameter || calleeParam.type != callee.resultType) {
    return zc::none;
  }
  auto calleeCarrier = integerCarrierFor(callee.resultType, semanticTypes);
  if (calleeCarrier == zc::none) { return zc::none; }
  const auto calleeCarrierValue = ZC_REQUIRE_NONNULL(calleeCarrier);
  const uint32_t calleeParamOrdinal = calleeParam.id.ordinal();
  const auto& calleeBlock = callee.blocks[0];
  if (calleeBlock.statements.size() != 0 ||
      calleeBlock.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& calleeReturn = calleeBlock.terminator.returnValue().value;
  if (calleeReturn == zc::none) { return zc::none; }
  bool calleeReturnsParam = false;
  ZC_IF_SOME(value, calleeReturn) {
    calleeReturnsParam = value.kind() != mir::MirOperandKind::Constant &&
                         value.place().local() == calleeParam.id &&
                         value.place().projections().size() == 0;
  }
  if (!calleeReturnsParam) { return zc::none; }

  // The caller is either of two verified shapes over P leading parameter locals.
  //
  //  * initializer shape: one trailing user local R; entry is StorageLive(R) +
  //    Call(dest=R); the empty continuation returns R.
  //  * return shape: a trailing Temporary T and FunctionResult R; entry is
  //    StorageLive(T) + Call(dest=T); the continuation stores R from T
  //    (StorageLive(R); Assign(R = move T); StorageDead(T)) and returns R.
  //
  // In both shapes the single call argument is an integer constant or a place-use
  // of a leading parameter slot.
  if (caller.kind != mir::MirFunctionKind::Function || caller.locals.size() < 1 ||
      caller.blocks.size() != 2 || caller.resultType != callee.resultType) {
    return zc::none;
  }
  // Split the leading parameter locals from the trailing call/result locals.
  size_t callerParameterCount = 0;
  while (callerParameterCount < caller.locals.size() &&
         caller.locals[callerParameterCount].kind == mir::MirLocalKind::Parameter) {
    ++callerParameterCount;
  }
  const size_t trailingCount = caller.locals.size() - callerParameterCount;

  const auto& entry = caller.blocks[0];
  const auto& continuation = caller.blocks[1];
  if (entry.terminator.kind() != mir::MirTerminatorKind::Call ||
      continuation.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& call = entry.terminator.callValue();
  if (call.arguments.size() != 1 || call.destination.projections().size() != 0 ||
      call.normalTarget != continuation.id || call.unwindTarget != zc::none ||
      !(call.callee == callee.owner)) {
    return zc::none;
  }

  // Lower the single argument to a constant operand or a use of a leading
  // parameter slot. A projected place or a non-parameter place is rejected.
  auto namesLeadingParameter = [&](const mir::MirOperand& operand) -> bool {
    if (operand.kind() == mir::MirOperandKind::Constant ||
        operand.place().projections().size() != 0) {
      return false;
    }
    const uint32_t ordinal = operand.place().local().ordinal();
    for (size_t p = 0; p < callerParameterCount; ++p) {
      if (caller.locals[p].id.ordinal() == ordinal) { return true; }
    }
    return false;
  };
  const auto& argument = call.arguments[0];
  zc::Maybe<Operand> argumentOperand;
  if (argument.kind() == mir::MirOperandKind::Constant) {
    if (argument.constantValue().type != calleeParam.type) { return zc::none; }
    argumentOperand = lirOperandFor(argument, calleeCarrierValue);
  } else if (namesLeadingParameter(argument)) {
    argumentOperand = lirOperandFor(argument, calleeCarrierValue);
  }
  if (argumentOperand == zc::none) { return zc::none; }

  // Describes how the trailing locals and the two blocks lower for one shape.
  struct CallerShape {
    uint32_t destinationOrdinal;
    uint32_t resultOrdinal;
    zc::Vector<Local> shapeLocals;
    zc::Vector<Statement> continuationStatements;
  };
  zc::Maybe<CallerShape> shape;

  const auto& callReturn = continuation.terminator.returnValue().value;
  if (callReturn == zc::none) { return zc::none; }
  bool returnsPlace = false;
  ZC_IF_SOME(value, callReturn) {
    returnsPlace =
        value.kind() != mir::MirOperandKind::Constant && value.place().projections().size() == 0;
  }
  if (!returnsPlace) { return zc::none; }

  if (trailingCount == 1) {
    // Initializer shape: one trailing user local R.
    const auto& resultLocal = caller.locals[callerParameterCount];
    const auto& returnOperand = ZC_ASSERT_NONNULL(callReturn);
    if (resultLocal.kind != mir::MirLocalKind::UserLocal || resultLocal.type != caller.resultType ||
        entry.statements.size() != 1 ||
        entry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
        entry.statements[0].storageLocal() != resultLocal.id ||
        call.destination.local() != resultLocal.id || continuation.statements.size() != 0 ||
        returnOperand.place().local() != resultLocal.id ||
        returnOperand.place().projections().size() != 0) {
      return zc::none;
    }
    zc::Vector<Local> shapeLocals;
    shapeLocals.add(Local(resultLocal.id.ordinal(), calleeCarrierValue));
    shape = CallerShape{
        call.destination.local().ordinal(), resultLocal.id.ordinal(), zc::mv(shapeLocals), {}};
  } else if (trailingCount == 2) {
    // Return shape: a Temporary T (the call destination) and a FunctionResult R.
    const auto& temporaryLocal = caller.locals[callerParameterCount];
    const auto& resultLocal = caller.locals[callerParameterCount + 1];
    const auto& returnOperand = ZC_ASSERT_NONNULL(callReturn);
    if (temporaryLocal.kind != mir::MirLocalKind::Temporary ||
        resultLocal.kind != mir::MirLocalKind::FunctionResult ||
        temporaryLocal.type != caller.resultType || resultLocal.type != caller.resultType ||
        entry.statements.size() != 1 ||
        entry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
        entry.statements[0].storageLocal() != temporaryLocal.id ||
        call.destination.local() != temporaryLocal.id ||
        returnOperand.place().local() != resultLocal.id ||
        returnOperand.place().projections().size() != 0) {
      return zc::none;
    }
    if (continuation.statements.size() != 3 ||
        continuation.statements[0].kind() != mir::MirStatementKind::StorageLive ||
        continuation.statements[0].storageLocal() != resultLocal.id ||
        continuation.statements[1].kind() != mir::MirStatementKind::Assign ||
        continuation.statements[2].kind() != mir::MirStatementKind::StorageDead ||
        continuation.statements[2].storageLocal() != temporaryLocal.id) {
      return zc::none;
    }
    const auto& moveAssign = continuation.statements[1].assignmentValue();
    if (moveAssign.initialization != mir::MirInitializationKind::Initialize ||
        moveAssign.destination.local() != resultLocal.id ||
        moveAssign.destination.projections().size() != 0 ||
        moveAssign.value.kind() != mir::MirRvalueKind::Use ||
        moveAssign.value.useValue().operand.kind() != mir::MirOperandKind::Move ||
        moveAssign.value.useValue().operand.place().local() != temporaryLocal.id ||
        moveAssign.value.useValue().operand.place().projections().size() != 0) {
      return zc::none;
    }
    zc::Vector<Local> shapeLocals;
    shapeLocals.add(Local(temporaryLocal.id.ordinal(), calleeCarrierValue));
    shapeLocals.add(Local(resultLocal.id.ordinal(), calleeCarrierValue));
    zc::Vector<Statement> loweredContinuation;
    loweredContinuation.add(Statement::assign(resultLocal.id.ordinal(),
                                              Operand::localUse(temporaryLocal.id.ordinal())));
    shape = CallerShape{call.destination.local().ordinal(), resultLocal.id.ordinal(),
                        zc::mv(shapeLocals), zc::mv(loweredContinuation)};
  } else {
    return zc::none;
  }
  if (shape == zc::none) { return zc::none; }
  auto& callerShape = ZC_ASSERT_NONNULL(shape);

  auto callerEntryId = LirBlockId::fromOrdinal(1);
  auto callerContId = LirBlockId::fromOrdinal(2);
  auto calleeEntryId = LirBlockId::fromOrdinal(1);
  if (callerEntryId == zc::none || callerContId == zc::none || calleeEntryId == zc::none) {
    return zc::none;
  }

  zc::Vector<Function> functions;

  // Function 0: the caller. Its call targets function index 1 (the callee),
  // passes the constant or parameter-slot argument, stores the integer result
  // into the destination slot, optionally moves a call temporary into the result
  // slot, and continues to the return.
  {
    zc::Vector<BasicBlock> callerBlocks;
    zc::Vector<Statement> entryStatements;
    zc::Vector<Operand> argumentOperands;
    argumentOperands.add(ZC_ASSERT_NONNULL(argumentOperand));
    auto callTerminator = Terminator::callFunction(
        /*calleeIndex=*/1, callerShape.destinationOrdinal, zc::mv(argumentOperands),
        ZC_REQUIRE_NONNULL(callerContId));
    if (callTerminator == zc::none) { return zc::none; }
    callerBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(callerEntryId), zc::mv(entryStatements),
                                ZC_REQUIRE_NONNULL(zc::mv(callTerminator))));
    callerBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(callerContId),
                                zc::mv(callerShape.continuationStatements),
                                Terminator::returnLocal(callerShape.resultOrdinal)));
    zc::Vector<Local> parameters;
    for (size_t p = 0; p < callerParameterCount; ++p) {
      auto carrier = integerCarrierFor(caller.locals[p].type, semanticTypes);
      if (carrier == zc::none) { return zc::none; }
      parameters.add(Local(caller.locals[p].id.ordinal(), ZC_REQUIRE_NONNULL(carrier)));
    }
    functions.add(Function(caller.owner, moduleEntrySymbol(callerParameterCount),
                           calleeCarrierValue, zc::mv(parameters), zc::mv(callerShape.shapeLocals),
                           zc::mv(callerBlocks)));
  }

  // Function 1: the callee, one parameter, a single block returning the
  // parameter slot.
  {
    zc::Vector<BasicBlock> calleeBlocks;
    zc::Vector<Statement> none;
    calleeBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(calleeEntryId), zc::mv(none),
                                Terminator::returnLocal(calleeParamOrdinal)));
    zc::Vector<Local> parameters;
    parameters.add(Local(calleeParamOrdinal, calleeCarrierValue));
    zc::Vector<Local> locals;
    functions.add(Function(callee.owner, zc::heapString("zom.callee"), calleeCarrierValue,
                           zc::mv(parameters), zc::mv(locals), zc::mv(calleeBlocks)));
  }

  return Module(zc::mv(functions));
}

zc::Maybe<Module> MirToLirLowering::lowerCallModuleWithConditionalCallee(
    const mir::MirFunction& caller, const mir::MirFunction& callee,
    const type::SemanticTypeStore& semanticTypes) {
  // Callee: the four-block conditional return shape, either the boolean-
  // conditional stride (2 locals: one parameter, one result; entry is
  // StorageLive(result) + SwitchInt on the parameter), the equality-
  // conditional stride (3 locals: one parameter, one result, one bool
  // temporary; entry is StorageLive(result) + StorageLive(temp) +
  // Assign(temp = Comparison) + SwitchInt on the temp), or the conjunctive
  // stride (5 locals: two parameters, one result, two bool temporaries;
  // entry is StorageLive(result) + StorageLive(guardTemp) +
  // Assign(guardTemp = Comparison) + StorageLive(conjTemp) +
  // Assign(conjTemp = BitAnd) + SwitchInt on the conjTemp).
  if (callee.kind != mir::MirFunctionKind::Function || callee.sourceScopes.size() != 1 ||
      (callee.locals.size() != 2 && callee.locals.size() != 3 && callee.locals.size() != 5) ||
      callee.blocks.size() != 4) {
    return zc::none;
  }
  const bool isEqualityStride = callee.locals.size() == 3;
  const bool isConjunctiveStride = callee.locals.size() == 5;
  const size_t calleeParameterCount =
      isConjunctiveStride ? 2 : (isEqualityStride ? 1 : callee.locals.size() - 1);
  const auto& calleeResult = callee.locals[calleeParameterCount];
  if (calleeResult.kind != mir::MirLocalKind::FunctionResult ||
      calleeResult.type != callee.resultType) {
    return zc::none;
  }
  for (size_t i = 0; i < calleeParameterCount; ++i) {
    if (callee.locals[i].kind != mir::MirLocalKind::Parameter) { return zc::none; }
  }
  const mir::MirLocalDeclaration* calleeTemp = nullptr;
  const mir::MirLocalDeclaration* calleeConjTemp = nullptr;
  if (isEqualityStride) {
    calleeTemp = &callee.locals[2];
    if (calleeTemp->kind != mir::MirLocalKind::Temporary) { return zc::none; }
  }
  if (isConjunctiveStride) {
    calleeTemp = &callee.locals[3];
    calleeConjTemp = &callee.locals[4];
    if (calleeTemp->kind != mir::MirLocalKind::Temporary ||
        calleeConjTemp->kind != mir::MirLocalKind::Temporary) {
      return zc::none;
    }
  }
  auto calleeResultCarrier = integerCarrierFor(callee.resultType, semanticTypes);
  if (calleeResultCarrier == zc::none) { return zc::none; }
  const auto calleeResultCarrierValue = ZC_REQUIRE_NONNULL(calleeResultCarrier);
  const uint32_t calleeResultOrdinal = calleeResult.id.ordinal();

  const auto& calleeEntry = callee.blocks[0];
  const auto& calleeThen = callee.blocks[1];
  const auto& calleeElse = callee.blocks[2];
  const auto& calleeJoin = callee.blocks[3];

  if (isConjunctiveStride) {
    // Conjunctive stride: StorageLive(result), StorageLive(guardTemp),
    // Assign(guardTemp = Comparison), StorageLive(conjTemp),
    // Assign(conjTemp = BitAnd).
    if (calleeEntry.statements.size() != 5 ||
        calleeEntry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
        calleeEntry.statements[0].storageLocal() != calleeResult.id ||
        calleeEntry.statements[1].kind() != mir::MirStatementKind::StorageLive ||
        calleeEntry.statements[1].storageLocal() != calleeTemp->id ||
        calleeEntry.statements[2].kind() != mir::MirStatementKind::Assign ||
        calleeEntry.statements[3].kind() != mir::MirStatementKind::StorageLive ||
        calleeEntry.statements[3].storageLocal() != calleeConjTemp->id ||
        calleeEntry.statements[4].kind() != mir::MirStatementKind::Assign ||
        calleeEntry.terminator.kind() != mir::MirTerminatorKind::SwitchInt) {
      return zc::none;
    }
    const auto& guardAssign = calleeEntry.statements[2].assignmentValue();
    if (guardAssign.destination.local() != calleeTemp->id ||
        guardAssign.destination.projections().size() != 0 ||
        guardAssign.value.kind() != mir::MirRvalueKind::Comparison) {
      return zc::none;
    }
    const auto& conjAssign = calleeEntry.statements[4].assignmentValue();
    if (conjAssign.destination.local() != calleeConjTemp->id ||
        conjAssign.destination.projections().size() != 0 ||
        conjAssign.value.kind() != mir::MirRvalueKind::Arithmetic) {
      return zc::none;
    }
  } else if (isEqualityStride) {
    // Equality stride: StorageLive(result), StorageLive(temp), Assign(temp = Comparison).
    if (calleeEntry.statements.size() != 3 ||
        calleeEntry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
        calleeEntry.statements[0].storageLocal() != calleeResult.id ||
        calleeEntry.statements[1].kind() != mir::MirStatementKind::StorageLive ||
        calleeEntry.statements[1].storageLocal() != calleeTemp->id ||
        calleeEntry.statements[2].kind() != mir::MirStatementKind::Assign ||
        calleeEntry.terminator.kind() != mir::MirTerminatorKind::SwitchInt) {
      return zc::none;
    }
    const auto& tempAssign = calleeEntry.statements[2].assignmentValue();
    if (tempAssign.destination.local() != calleeTemp->id ||
        tempAssign.destination.projections().size() != 0 ||
        tempAssign.value.kind() != mir::MirRvalueKind::Comparison) {
      return zc::none;
    }
  } else {
    // Boolean stride: StorageLive(result) only.
    if (calleeEntry.statements.size() != 1 ||
        calleeEntry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
        calleeEntry.statements[0].storageLocal() != calleeResult.id ||
        calleeEntry.terminator.kind() != mir::MirTerminatorKind::SwitchInt) {
      return zc::none;
    }
  }
  const auto& calleeSwitch = calleeEntry.terminator.switchIntValue();
  if (calleeSwitch.discriminant.kind() != mir::MirOperandKind::Copy ||
      calleeSwitch.discriminant.place().projections().size() != 0 ||
      calleeSwitch.arms.size() != 2) {
    return zc::none;
  }
  if (isConjunctiveStride) {
    // The discriminant must be the conjunction temp.
    if (calleeSwitch.discriminant.place().local() != calleeConjTemp->id) { return zc::none; }
  } else if (isEqualityStride) {
    // The discriminant must be the comparison temp.
    if (calleeSwitch.discriminant.place().local() != calleeTemp->id) { return zc::none; }
  } else {
    // The discriminant must be the bool parameter.
    bool calleeConditionIsParameter = false;
    for (size_t i = 0; i < calleeParameterCount; ++i) {
      if (callee.locals[i].id == calleeSwitch.discriminant.place().local()) {
        calleeConditionIsParameter = true;
        if (boolCarrierFor(callee.locals[i].type, semanticTypes) == zc::none) { return zc::none; }
      }
    }
    if (!calleeConditionIsParameter) { return zc::none; }
  }
  const uint32_t calleeConditionOrdinal = calleeSwitch.discriminant.place().local().ordinal();
  const auto calleeTrueTarget = calleeSwitch.arms[0].target.ordinal();
  const auto calleeFalseTarget = calleeSwitch.arms[1].target.ordinal();
  if (calleeTrueTarget != calleeThen.id.ordinal() || calleeFalseTarget != calleeElse.id.ordinal() ||
      calleeSwitch.defaultTarget != calleeElse.id) {
    return zc::none;
  }

  auto lowerCalleeArm = [&](const mir::MirBasicBlock& branch, zc::Vector<Statement>& out) -> bool {
    if (branch.statements.size() != 1 ||
        branch.statements[0].kind() != mir::MirStatementKind::Assign ||
        branch.terminator.kind() != mir::MirTerminatorKind::Goto ||
        branch.terminator.gotoValue().target != calleeJoin.id) {
      return false;
    }
    const auto& assignment = branch.statements[0].assignmentValue();
    if (assignment.destination.local() != calleeResult.id ||
        assignment.destination.projections().size() != 0 ||
        assignment.value.kind() != mir::MirRvalueKind::Use) {
      return false;
    }
    const auto& operand = assignment.value.useValue().operand;
    if (operand.kind() == mir::MirOperandKind::Constant &&
        operand.constantValue().type != callee.resultType) {
      return false;
    }
    if (operand.kind() != mir::MirOperandKind::Constant &&
        (operand.place().projections().size() != 0 ||
         operand.place().rootType() != callee.resultType)) {
      return false;
    }
    auto lowered = lirOperandFor(operand, calleeResultCarrierValue);
    if (lowered == zc::none) { return false; }
    out.add(Statement::assign(calleeResultOrdinal, ZC_REQUIRE_NONNULL(lowered)));
    return true;
  };

  zc::Vector<Statement> calleeThenStatements;
  zc::Vector<Statement> calleeElseStatements;
  if (!lowerCalleeArm(calleeThen, calleeThenStatements) ||
      !lowerCalleeArm(calleeElse, calleeElseStatements)) {
    return zc::none;
  }

  if (calleeJoin.statements.size() != 0 ||
      calleeJoin.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& calleeReturnValue = calleeJoin.terminator.returnValue().value;
  if (calleeReturnValue == zc::none) { return zc::none; }
  bool calleeReturnsResult = false;
  ZC_IF_SOME(value, calleeReturnValue) {
    calleeReturnsResult = value.kind() != mir::MirOperandKind::Constant &&
                          value.place().local() == calleeResult.id &&
                          value.place().projections().size() == 0;
  }
  if (!calleeReturnsResult) { return zc::none; }

  // Equality stride: lower the comparison rvalue from the entry block's Assign
  // statement so the LIR entry block can compute the temp before branching.
  zc::Maybe<ComparisonOp> equalityOp;
  zc::Maybe<Operand> equalityLeft;
  zc::Maybe<Operand> equalityRight;
  // Conjunctive stride: lower the guard comparison and the conjunction.
  zc::Maybe<ComparisonOp> guardOp;
  zc::Maybe<Operand> guardLeft;
  zc::Maybe<Operand> guardRight;
  zc::Maybe<Operand> conjLeft;
  zc::Maybe<Operand> conjRight;
  if (isConjunctiveStride) {
    // Guard comparison: guardTemp = Comparison(op, param, literal).
    const auto& guardAssign = calleeEntry.statements[2].assignmentValue();
    const auto& guardCmp = guardAssign.value.comparisonValue();
    auto operandCarrierFor = [&](const mir::MirOperand& operand) -> zc::Maybe<ValueType> {
      if (operand.kind() == mir::MirOperandKind::Constant) {
        return integerCarrierFor(operand.constantValue().type, semanticTypes);
      }
      return integerCarrierFor(operand.place().rootType(), semanticTypes);
    };
    auto gLeftCarrier = operandCarrierFor(guardCmp.left);
    auto gRightCarrier = operandCarrierFor(guardCmp.right);
    if (gLeftCarrier == zc::none || gRightCarrier == zc::none) { return zc::none; }
    auto lirGLeft = lirOperandFor(guardCmp.left, ZC_REQUIRE_NONNULL(gLeftCarrier));
    auto lirGRight = lirOperandFor(guardCmp.right, ZC_REQUIRE_NONNULL(gRightCarrier));
    if (lirGLeft == zc::none || lirGRight == zc::none) { return zc::none; }
    guardOp = lirComparisonOpFor(guardCmp.op);
    guardLeft = zc::mv(lirGLeft);
    guardRight = zc::mv(lirGRight);
    // Conjunction: conjTemp = BitAnd(scrutinee, guardTemp).
    const auto& conjAssign = calleeEntry.statements[4].assignmentValue();
    const auto& conjArith = conjAssign.value.arithmeticValue();
    if (conjArith.op != mir::MirArithmeticOperator::BitAnd) { return zc::none; }
    auto cLeftCarrier = boolCarrierFor(conjArith.left.place().rootType(), semanticTypes);
    auto cRightCarrier = boolCarrierFor(conjArith.right.place().rootType(), semanticTypes);
    if (cLeftCarrier == zc::none || cRightCarrier == zc::none) { return zc::none; }
    auto lirCLeft = lirOperandFor(conjArith.left, ZC_REQUIRE_NONNULL(cLeftCarrier));
    auto lirCRight = lirOperandFor(conjArith.right, ZC_REQUIRE_NONNULL(cRightCarrier));
    if (lirCLeft == zc::none || lirCRight == zc::none) { return zc::none; }
    conjLeft = zc::mv(lirCLeft);
    conjRight = zc::mv(lirCRight);
  } else if (isEqualityStride) {
    const auto& tempAssign = calleeEntry.statements[2].assignmentValue();
    const auto& comparison = tempAssign.value.comparisonValue();
    auto operandCarrierFor = [&](const mir::MirOperand& operand) -> zc::Maybe<ValueType> {
      if (operand.kind() == mir::MirOperandKind::Constant) {
        auto carrier = integerCarrierFor(operand.constantValue().type, semanticTypes);
        if (carrier == zc::none) {
          carrier = enumCarrierFor(operand.constantValue().type, semanticTypes);
        }
        return carrier;
      }
      auto carrier = integerCarrierFor(operand.place().rootType(), semanticTypes);
      if (carrier == zc::none) {
        carrier = enumCarrierFor(operand.place().rootType(), semanticTypes);
      }
      return carrier;
    };
    auto leftCarrier = operandCarrierFor(comparison.left);
    auto rightCarrier = operandCarrierFor(comparison.right);
    if (leftCarrier == zc::none || rightCarrier == zc::none) { return zc::none; }
    auto lirLeft = lirOperandFor(comparison.left, ZC_REQUIRE_NONNULL(leftCarrier));
    auto lirRight = lirOperandFor(comparison.right, ZC_REQUIRE_NONNULL(rightCarrier));
    if (lirLeft == zc::none || lirRight == zc::none) { return zc::none; }
    equalityOp = lirComparisonOpFor(comparison.op);
    equalityLeft = zc::mv(lirLeft);
    equalityRight = zc::mv(lirRight);
  }

  // Caller: the two-block call+return shape with a single constant argument
  // (same structure as lowerCallModuleWithArgument), or two constant arguments
  // for the conjunctive stride.
  if (caller.kind != mir::MirFunctionKind::Function || caller.locals.size() < 1 ||
      caller.blocks.size() != 2 || caller.resultType != callee.resultType) {
    return zc::none;
  }
  size_t callerParameterCount = 0;
  while (callerParameterCount < caller.locals.size() &&
         caller.locals[callerParameterCount].kind == mir::MirLocalKind::Parameter) {
    ++callerParameterCount;
  }
  const size_t callerTrailingCount = caller.locals.size() - callerParameterCount;

  const auto& callerEntry = caller.blocks[0];
  const auto& callerContinuation = caller.blocks[1];
  if (callerEntry.terminator.kind() != mir::MirTerminatorKind::Call ||
      callerContinuation.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& call = callerEntry.terminator.callValue();
  const size_t expectedArgumentCount = isConjunctiveStride ? 2 : 1;
  if (call.arguments.size() != expectedArgumentCount ||
      call.destination.projections().size() != 0 || call.normalTarget != callerContinuation.id ||
      call.unwindTarget != zc::none || !(call.callee == callee.owner)) {
    return zc::none;
  }

  zc::Vector<Operand> argumentOperands;
  for (size_t argIndex = 0; argIndex < call.arguments.size(); ++argIndex) {
    const auto& argument = call.arguments[argIndex];
    if (argument.kind() != mir::MirOperandKind::Constant) { return zc::none; }
    if (argument.constantValue().type != callee.locals[argIndex].type) { return zc::none; }
    auto argumentCarrier = boolCarrierFor(callee.locals[argIndex].type, semanticTypes);
    if (argumentCarrier == zc::none) {
      argumentCarrier = integerCarrierFor(callee.locals[argIndex].type, semanticTypes);
    }
    if (argumentCarrier == zc::none) {
      argumentCarrier = enumCarrierFor(callee.locals[argIndex].type, semanticTypes);
    }
    if (argumentCarrier == zc::none) { return zc::none; }
    auto lowered = lirOperandFor(argument, ZC_REQUIRE_NONNULL(argumentCarrier));
    if (lowered == zc::none) { return zc::none; }
    argumentOperands.add(zc::mv(ZC_ASSERT_NONNULL(lowered)));
  }

  const auto& callReturn = callerContinuation.terminator.returnValue().value;
  if (callReturn == zc::none) { return zc::none; }
  bool callerReturnsPlace = false;
  ZC_IF_SOME(value, callReturn) {
    callerReturnsPlace =
        value.kind() != mir::MirOperandKind::Constant && value.place().projections().size() == 0;
  }
  if (!callerReturnsPlace) { return zc::none; }

  struct CallerShape {
    uint32_t destinationOrdinal;
    uint32_t resultOrdinal;
    zc::Vector<Local> shapeLocals;
    zc::Vector<Statement> continuationStatements;
  };
  zc::Maybe<CallerShape> callerShape;

  if (callerTrailingCount == 1) {
    const auto& resultLocal = caller.locals[callerParameterCount];
    const auto& returnOperand = ZC_ASSERT_NONNULL(callReturn);
    if ((resultLocal.kind != mir::MirLocalKind::UserLocal &&
         resultLocal.kind != mir::MirLocalKind::FunctionResult) ||
        resultLocal.type != caller.resultType || callerEntry.statements.size() != 1 ||
        callerEntry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
        callerEntry.statements[0].storageLocal() != resultLocal.id ||
        call.destination.local() != resultLocal.id || callerContinuation.statements.size() != 0 ||
        returnOperand.place().local() != resultLocal.id ||
        returnOperand.place().projections().size() != 0) {
      return zc::none;
    }
    zc::Vector<Local> shapeLocals;
    shapeLocals.add(Local(resultLocal.id.ordinal(), calleeResultCarrierValue));
    callerShape = CallerShape{
        call.destination.local().ordinal(), resultLocal.id.ordinal(), zc::mv(shapeLocals), {}};
  } else if (callerTrailingCount == 2) {
    const auto& temporaryLocal = caller.locals[callerParameterCount];
    const auto& resultLocal = caller.locals[callerParameterCount + 1];
    const auto& returnOperand = ZC_ASSERT_NONNULL(callReturn);
    if (temporaryLocal.kind != mir::MirLocalKind::Temporary ||
        resultLocal.kind != mir::MirLocalKind::FunctionResult ||
        temporaryLocal.type != caller.resultType || resultLocal.type != caller.resultType ||
        callerEntry.statements.size() != 1 ||
        callerEntry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
        callerEntry.statements[0].storageLocal() != temporaryLocal.id ||
        call.destination.local() != temporaryLocal.id ||
        returnOperand.place().local() != resultLocal.id ||
        returnOperand.place().projections().size() != 0) {
      return zc::none;
    }
    if (callerContinuation.statements.size() != 3 ||
        callerContinuation.statements[0].kind() != mir::MirStatementKind::StorageLive ||
        callerContinuation.statements[0].storageLocal() != resultLocal.id ||
        callerContinuation.statements[1].kind() != mir::MirStatementKind::Assign ||
        callerContinuation.statements[2].kind() != mir::MirStatementKind::StorageDead ||
        callerContinuation.statements[2].storageLocal() != temporaryLocal.id) {
      return zc::none;
    }
    const auto& moveAssign = callerContinuation.statements[1].assignmentValue();
    if (moveAssign.initialization != mir::MirInitializationKind::Initialize ||
        moveAssign.destination.local() != resultLocal.id ||
        moveAssign.destination.projections().size() != 0 ||
        moveAssign.value.kind() != mir::MirRvalueKind::Use ||
        moveAssign.value.useValue().operand.kind() != mir::MirOperandKind::Move ||
        moveAssign.value.useValue().operand.place().local() != temporaryLocal.id ||
        moveAssign.value.useValue().operand.place().projections().size() != 0) {
      return zc::none;
    }
    zc::Vector<Local> shapeLocals;
    shapeLocals.add(Local(temporaryLocal.id.ordinal(), calleeResultCarrierValue));
    shapeLocals.add(Local(resultLocal.id.ordinal(), calleeResultCarrierValue));
    zc::Vector<Statement> loweredContinuation;
    loweredContinuation.add(Statement::assign(resultLocal.id.ordinal(),
                                              Operand::localUse(temporaryLocal.id.ordinal())));
    callerShape = CallerShape{call.destination.local().ordinal(), resultLocal.id.ordinal(),
                              zc::mv(shapeLocals), zc::mv(loweredContinuation)};
  } else {
    return zc::none;
  }
  if (callerShape == zc::none) { return zc::none; }
  auto& cs = ZC_ASSERT_NONNULL(callerShape);

  auto callerEntryId = LirBlockId::fromOrdinal(1);
  auto callerContId = LirBlockId::fromOrdinal(2);
  auto calleeEntryId = LirBlockId::fromOrdinal(1);
  auto calleeThenId = LirBlockId::fromOrdinal(2);
  auto calleeElseId = LirBlockId::fromOrdinal(3);
  auto calleeJoinId = LirBlockId::fromOrdinal(4);
  if (callerEntryId == zc::none || callerContId == zc::none || calleeEntryId == zc::none ||
      calleeThenId == zc::none || calleeElseId == zc::none || calleeJoinId == zc::none) {
    return zc::none;
  }

  zc::Vector<Function> functions;

  // Function 0: the caller. Its call targets function index 1 (the callee),
  // passes the constant arguments, stores the integer result into the
  // destination slot, and continues to the return.
  {
    zc::Vector<BasicBlock> callerBlocks;
    zc::Vector<Statement> entryStatements;
    auto callTerminator = Terminator::callFunction(
        /*calleeIndex=*/1, cs.destinationOrdinal, zc::mv(argumentOperands),
        ZC_REQUIRE_NONNULL(callerContId));
    if (callTerminator == zc::none) { return zc::none; }
    callerBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(callerEntryId), zc::mv(entryStatements),
                                ZC_REQUIRE_NONNULL(zc::mv(callTerminator))));
    callerBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(callerContId), zc::mv(cs.continuationStatements),
                                Terminator::returnLocal(cs.resultOrdinal)));
    zc::Vector<Local> parameters;
    for (size_t p = 0; p < callerParameterCount; ++p) {
      auto carrier = integerCarrierFor(caller.locals[p].type, semanticTypes);
      if (carrier == zc::none) { return zc::none; }
      parameters.add(Local(caller.locals[p].id.ordinal(), ZC_REQUIRE_NONNULL(carrier)));
    }
    functions.add(Function(caller.owner, moduleEntrySymbol(callerParameterCount),
                           calleeResultCarrierValue, zc::mv(parameters), zc::mv(cs.shapeLocals),
                           zc::mv(callerBlocks)));
  }

  // Function 1: the callee, a four-block conditional return (boolean,
  // equality, or conjunctive stride).
  {
    zc::Vector<BasicBlock> calleeBlocks;
    {
      zc::Vector<Statement> entryStatements;
      if (isConjunctiveStride) {
        // Guard comparison into guardTemp, then conjunction into conjTemp.
        entryStatements.add(
            Statement::compare(calleeTemp->id.ordinal(), ZC_REQUIRE_NONNULL(guardOp),
                               ZC_REQUIRE_NONNULL(guardLeft), ZC_REQUIRE_NONNULL(guardRight)));
        entryStatements.add(
            Statement::arithmetic(calleeConjTemp->id.ordinal(), ArithmeticOp::BitAnd,
                                  ZC_REQUIRE_NONNULL(conjLeft), ZC_REQUIRE_NONNULL(conjRight)));
      } else if (isEqualityStride) {
        entryStatements.add(Statement::compare(
            calleeConditionOrdinal, ZC_REQUIRE_NONNULL(equalityOp),
            ZC_REQUIRE_NONNULL(equalityLeft), ZC_REQUIRE_NONNULL(equalityRight)));
      }
      calleeBlocks.add(BasicBlock(
          ZC_REQUIRE_NONNULL(calleeEntryId), zc::mv(entryStatements),
          Terminator::condBranch(calleeConditionOrdinal, ZC_REQUIRE_NONNULL(calleeThenId),
                                 ZC_REQUIRE_NONNULL(calleeElseId))));
    }
    calleeBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(calleeThenId), zc::mv(calleeThenStatements),
                                Terminator::gotoBlock(ZC_REQUIRE_NONNULL(calleeJoinId))));
    calleeBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(calleeElseId), zc::mv(calleeElseStatements),
                                Terminator::gotoBlock(ZC_REQUIRE_NONNULL(calleeJoinId))));
    {
      zc::Vector<Statement> none;
      calleeBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(calleeJoinId), zc::mv(none),
                                  Terminator::returnLocal(calleeResultOrdinal)));
    }
    zc::Vector<Local> parameters;
    for (size_t i = 0; i < calleeParameterCount; ++i) {
      const auto& parameterLocal = callee.locals[i];
      zc::Maybe<ValueType> carrier = boolCarrierFor(parameterLocal.type, semanticTypes);
      if (carrier == zc::none) { carrier = integerCarrierFor(parameterLocal.type, semanticTypes); }
      if (carrier == zc::none) { carrier = enumCarrierFor(parameterLocal.type, semanticTypes); }
      if (carrier == zc::none) { return zc::none; }
      parameters.add(Local(parameterLocal.id.ordinal(), ZC_REQUIRE_NONNULL(carrier)));
    }
    zc::Vector<Local> locals;
    locals.add(Local(calleeResultOrdinal, calleeResultCarrierValue));
    if (isConjunctiveStride) {
      auto guardTempCarrier = boolCarrierFor(calleeTemp->type, semanticTypes);
      if (guardTempCarrier == zc::none) { return zc::none; }
      locals.add(Local(calleeTemp->id.ordinal(), ZC_REQUIRE_NONNULL(guardTempCarrier)));
      auto conjTempCarrier = boolCarrierFor(calleeConjTemp->type, semanticTypes);
      if (conjTempCarrier == zc::none) { return zc::none; }
      locals.add(Local(calleeConjTemp->id.ordinal(), ZC_REQUIRE_NONNULL(conjTempCarrier)));
    } else if (isEqualityStride) {
      auto tempCarrier = boolCarrierFor(calleeTemp->type, semanticTypes);
      if (tempCarrier == zc::none) { return zc::none; }
      locals.add(Local(calleeTemp->id.ordinal(), ZC_REQUIRE_NONNULL(tempCarrier)));
    }
    functions.add(Function(callee.owner, zc::heapString("zom.callee"), calleeResultCarrierValue,
                           zc::mv(parameters), zc::mv(locals), zc::mv(calleeBlocks)));
  }

  return Module(zc::mv(functions));
}

zc::Maybe<Module> MirToLirLowering::lowerCallModuleWithChainedConditionalCallee(
    const mir::MirFunction& caller, const mir::MirFunction& callee,
    const type::SemanticTypeStore& semanticTypes) {
  // Callee: the 2N+2-block chained conditional return shape produced by
  // buildChainedConditionalReturn (N >= 2 literal arms plus one default arm).
  // Locals are P parameters, one result, and N bool comparison temporaries.
  // Block layout (1-indexed ordinals):
  //   Block 2k+1 (k=0..N-1): entry k -- k=0 has StorageLive(result) plus
  //     StorageLive for every condition temp, then Assign(temp_k = Comparison);
  //     k>0 has just Assign(temp_k = Comparison). All entries end in SwitchInt.
  //   Block 2k+2 (k=0..N-1): then arm k -- Assign(result = literal_k), Goto join.
  //   Block 2N+1: else arm -- Assign(result = else_literal), Goto join.
  //   Block 2N+2: join -- Return(result).
  if (callee.kind != mir::MirFunctionKind::Function || callee.sourceScopes.size() != 1 ||
      callee.blocks.size() < 6 || callee.blocks.size() % 2 != 0) {
    return zc::none;
  }
  const size_t armCount = (callee.blocks.size() - 2) / 2;
  size_t calleeParameterCount = 0;
  while (calleeParameterCount < callee.locals.size() &&
         callee.locals[calleeParameterCount].kind == mir::MirLocalKind::Parameter) {
    ++calleeParameterCount;
  }
  if (callee.locals.size() != calleeParameterCount + 1 + armCount) { return zc::none; }
  const auto& calleeResult = callee.locals[calleeParameterCount];
  if (calleeResult.kind != mir::MirLocalKind::FunctionResult ||
      calleeResult.type != callee.resultType) {
    return zc::none;
  }
  for (size_t i = 0; i < calleeParameterCount; ++i) {
    if (callee.locals[i].kind != mir::MirLocalKind::Parameter) { return zc::none; }
  }
  for (size_t i = 0; i < armCount; ++i) {
    if (callee.locals[calleeParameterCount + 1 + i].kind != mir::MirLocalKind::Temporary) {
      return zc::none;
    }
  }
  auto calleeResultCarrier = integerCarrierFor(callee.resultType, semanticTypes);
  if (calleeResultCarrier == zc::none) { return zc::none; }
  const auto calleeResultCarrierValue = ZC_REQUIRE_NONNULL(calleeResultCarrier);
  const uint32_t calleeResultOrdinal = calleeResult.id.ordinal();
  const uint32_t joinBlockOrdinal = static_cast<uint32_t>(2 * armCount + 2);

  struct ArmData {
    ComparisonOp op;
    Operand left;
    Operand right;
    uint32_t tempOrdinal;
    uint32_t thenBlockOrdinal;
    uint32_t elseBlockOrdinal;
    Operand thenLiteral;
  };
  zc::Vector<ArmData> arms;

  for (size_t k = 0; k < armCount; ++k) {
    const auto& entryBlock = callee.blocks[2 * k];
    const auto& thenBlock = callee.blocks[2 * k + 1];
    const auto& tempLocal = callee.locals[calleeParameterCount + 1 + k];
    const uint32_t thenOrdinal = static_cast<uint32_t>(2 * k + 2);
    const uint32_t elseOrdinal = (k + 1 < armCount) ? static_cast<uint32_t>(2 * (k + 1) + 1)
                                                    : static_cast<uint32_t>(2 * armCount + 1);

    // Entry block: k=0 has StorageLive(result) + StorageLive for every
    // condition temp + Assign(temp_0, ...); k>0 has just Assign(temp_k, ...).
    const size_t expectedStatements = (k == 0) ? 2 + armCount : 1;
    if (entryBlock.statements.size() != expectedStatements) { return zc::none; }
    size_t stmtIndex = 0;
    if (k == 0) {
      if (entryBlock.statements[0].kind() != mir::MirStatementKind::StorageLive ||
          entryBlock.statements[0].storageLocal() != calleeResult.id) {
        return zc::none;
      }
      for (size_t t = 0; t < armCount; ++t) {
        if (entryBlock.statements[1 + t].kind() != mir::MirStatementKind::StorageLive ||
            entryBlock.statements[1 + t].storageLocal() !=
                callee.locals[calleeParameterCount + 1 + t].id) {
          return zc::none;
        }
      }
      stmtIndex = 1 + armCount;
    }
    const auto& tempAssign = entryBlock.statements[stmtIndex].assignmentValue();
    if (tempAssign.destination.local() != tempLocal.id ||
        tempAssign.destination.projections().size() != 0 ||
        tempAssign.value.kind() != mir::MirRvalueKind::Comparison) {
      return zc::none;
    }
    if (entryBlock.terminator.kind() != mir::MirTerminatorKind::SwitchInt) { return zc::none; }
    const auto& switchInt = entryBlock.terminator.switchIntValue();
    if (switchInt.discriminant.kind() != mir::MirOperandKind::Copy ||
        switchInt.discriminant.place().local() != tempLocal.id ||
        switchInt.discriminant.place().projections().size() != 0 || switchInt.arms.size() != 2 ||
        switchInt.arms[0].target.ordinal() != thenOrdinal ||
        switchInt.arms[1].target.ordinal() != elseOrdinal ||
        switchInt.defaultTarget.ordinal() != elseOrdinal) {
      return zc::none;
    }

    // Lower the comparison operands.
    const auto& comparison = tempAssign.value.comparisonValue();
    auto operandCarrierFor = [&](const mir::MirOperand& operand) -> zc::Maybe<ValueType> {
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

    // Then block: Assign(result = Use(constant)), Goto(join).
    if (thenBlock.statements.size() != 1 ||
        thenBlock.statements[0].kind() != mir::MirStatementKind::Assign ||
        thenBlock.terminator.kind() != mir::MirTerminatorKind::Goto ||
        thenBlock.terminator.gotoValue().target.ordinal() != joinBlockOrdinal) {
      return zc::none;
    }
    const auto& thenAssign = thenBlock.statements[0].assignmentValue();
    if (thenAssign.destination.local() != calleeResult.id ||
        thenAssign.destination.projections().size() != 0 ||
        thenAssign.value.kind() != mir::MirRvalueKind::Use ||
        thenAssign.value.useValue().operand.kind() != mir::MirOperandKind::Constant) {
      return zc::none;
    }
    auto thenLiteral = lirOperandFor(thenAssign.value.useValue().operand, calleeResultCarrierValue);
    if (thenLiteral == zc::none) { return zc::none; }

    arms.add(ArmData{lirComparisonOpFor(comparison.op), zc::mv(ZC_ASSERT_NONNULL(lirLeft)),
                     zc::mv(ZC_ASSERT_NONNULL(lirRight)), tempLocal.id.ordinal(), thenOrdinal,
                     elseOrdinal, zc::mv(ZC_ASSERT_NONNULL(thenLiteral))});
  }

  // Else block: Assign(result = Use(constant)), Goto(join).
  const auto& elseBlock = callee.blocks[2 * armCount];
  if (elseBlock.statements.size() != 1 ||
      elseBlock.statements[0].kind() != mir::MirStatementKind::Assign ||
      elseBlock.terminator.kind() != mir::MirTerminatorKind::Goto ||
      elseBlock.terminator.gotoValue().target.ordinal() != joinBlockOrdinal) {
    return zc::none;
  }
  const auto& elseAssign = elseBlock.statements[0].assignmentValue();
  if (elseAssign.destination.local() != calleeResult.id ||
      elseAssign.destination.projections().size() != 0 ||
      elseAssign.value.kind() != mir::MirRvalueKind::Use ||
      elseAssign.value.useValue().operand.kind() != mir::MirOperandKind::Constant) {
    return zc::none;
  }
  auto elseLiteral = lirOperandFor(elseAssign.value.useValue().operand, calleeResultCarrierValue);
  if (elseLiteral == zc::none) { return zc::none; }

  // Join block: Return(result).
  const auto& joinBlock = callee.blocks[2 * armCount + 1];
  if (joinBlock.statements.size() != 0 ||
      joinBlock.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& joinReturnValue = joinBlock.terminator.returnValue().value;
  if (joinReturnValue == zc::none) { return zc::none; }
  bool joinReturnsResult = false;
  ZC_IF_SOME(value, joinReturnValue) {
    joinReturnsResult = value.kind() != mir::MirOperandKind::Constant &&
                        value.place().local() == calleeResult.id &&
                        value.place().projections().size() == 0;
  }
  if (!joinReturnsResult) { return zc::none; }

  // Caller: the two-block call+return shape (same structure as
  // lowerCallModuleWithConditionalCallee).
  if (caller.kind != mir::MirFunctionKind::Function || caller.locals.size() < 1 ||
      caller.blocks.size() != 2 || caller.resultType != callee.resultType) {
    return zc::none;
  }
  size_t callerParameterCount = 0;
  while (callerParameterCount < caller.locals.size() &&
         caller.locals[callerParameterCount].kind == mir::MirLocalKind::Parameter) {
    ++callerParameterCount;
  }
  const size_t callerTrailingCount = caller.locals.size() - callerParameterCount;

  const auto& callerEntry = caller.blocks[0];
  const auto& callerContinuation = caller.blocks[1];
  if (callerEntry.terminator.kind() != mir::MirTerminatorKind::Call ||
      callerContinuation.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& call = callerEntry.terminator.callValue();
  if (call.arguments.size() != 1 || call.destination.projections().size() != 0 ||
      call.normalTarget != callerContinuation.id || call.unwindTarget != zc::none ||
      !(call.callee == callee.owner)) {
    return zc::none;
  }

  const auto& argument = call.arguments[0];
  if (argument.kind() != mir::MirOperandKind::Constant) { return zc::none; }
  if (argument.constantValue().type != callee.locals[0].type) { return zc::none; }
  auto argumentCarrier = integerCarrierFor(callee.locals[0].type, semanticTypes);
  if (argumentCarrier == zc::none) {
    argumentCarrier = enumCarrierFor(callee.locals[0].type, semanticTypes);
  }
  if (argumentCarrier == zc::none) { return zc::none; }
  auto loweredArgument = lirOperandFor(argument, ZC_REQUIRE_NONNULL(argumentCarrier));
  if (loweredArgument == zc::none) { return zc::none; }
  zc::Vector<Operand> argumentOperands;
  argumentOperands.add(zc::mv(ZC_ASSERT_NONNULL(loweredArgument)));

  const auto& callReturn = callerContinuation.terminator.returnValue().value;
  if (callReturn == zc::none) { return zc::none; }
  bool callerReturnsPlace = false;
  ZC_IF_SOME(value, callReturn) {
    callerReturnsPlace =
        value.kind() != mir::MirOperandKind::Constant && value.place().projections().size() == 0;
  }
  if (!callerReturnsPlace) { return zc::none; }

  struct CallerShape {
    uint32_t destinationOrdinal;
    uint32_t resultOrdinal;
    zc::Vector<Local> shapeLocals;
    zc::Vector<Statement> continuationStatements;
  };
  zc::Maybe<CallerShape> callerShape;

  if (callerTrailingCount == 1) {
    const auto& resultLocal = caller.locals[callerParameterCount];
    const auto& returnOperand = ZC_ASSERT_NONNULL(callReturn);
    if ((resultLocal.kind != mir::MirLocalKind::UserLocal &&
         resultLocal.kind != mir::MirLocalKind::FunctionResult) ||
        resultLocal.type != caller.resultType || callerEntry.statements.size() != 1 ||
        callerEntry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
        callerEntry.statements[0].storageLocal() != resultLocal.id ||
        call.destination.local() != resultLocal.id || callerContinuation.statements.size() != 0 ||
        returnOperand.place().local() != resultLocal.id ||
        returnOperand.place().projections().size() != 0) {
      return zc::none;
    }
    zc::Vector<Local> shapeLocals;
    shapeLocals.add(Local(resultLocal.id.ordinal(), calleeResultCarrierValue));
    callerShape = CallerShape{
        call.destination.local().ordinal(), resultLocal.id.ordinal(), zc::mv(shapeLocals), {}};
  } else if (callerTrailingCount == 2) {
    const auto& temporaryLocal = caller.locals[callerParameterCount];
    const auto& resultLocal = caller.locals[callerParameterCount + 1];
    const auto& returnOperand = ZC_ASSERT_NONNULL(callReturn);
    if (temporaryLocal.kind != mir::MirLocalKind::Temporary ||
        resultLocal.kind != mir::MirLocalKind::FunctionResult ||
        temporaryLocal.type != caller.resultType || resultLocal.type != caller.resultType ||
        callerEntry.statements.size() != 1 ||
        callerEntry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
        callerEntry.statements[0].storageLocal() != temporaryLocal.id ||
        call.destination.local() != temporaryLocal.id ||
        returnOperand.place().local() != resultLocal.id ||
        returnOperand.place().projections().size() != 0) {
      return zc::none;
    }
    if (callerContinuation.statements.size() != 3 ||
        callerContinuation.statements[0].kind() != mir::MirStatementKind::StorageLive ||
        callerContinuation.statements[0].storageLocal() != resultLocal.id ||
        callerContinuation.statements[1].kind() != mir::MirStatementKind::Assign ||
        callerContinuation.statements[2].kind() != mir::MirStatementKind::StorageDead ||
        callerContinuation.statements[2].storageLocal() != temporaryLocal.id) {
      return zc::none;
    }
    const auto& moveAssign = callerContinuation.statements[1].assignmentValue();
    if (moveAssign.initialization != mir::MirInitializationKind::Initialize ||
        moveAssign.destination.local() != resultLocal.id ||
        moveAssign.destination.projections().size() != 0 ||
        moveAssign.value.kind() != mir::MirRvalueKind::Use ||
        moveAssign.value.useValue().operand.kind() != mir::MirOperandKind::Move ||
        moveAssign.value.useValue().operand.place().local() != temporaryLocal.id ||
        moveAssign.value.useValue().operand.place().projections().size() != 0) {
      return zc::none;
    }
    zc::Vector<Local> shapeLocals;
    shapeLocals.add(Local(temporaryLocal.id.ordinal(), calleeResultCarrierValue));
    shapeLocals.add(Local(resultLocal.id.ordinal(), calleeResultCarrierValue));
    zc::Vector<Statement> loweredContinuation;
    loweredContinuation.add(Statement::assign(resultLocal.id.ordinal(),
                                              Operand::localUse(temporaryLocal.id.ordinal())));
    callerShape = CallerShape{call.destination.local().ordinal(), resultLocal.id.ordinal(),
                              zc::mv(shapeLocals), zc::mv(loweredContinuation)};
  } else {
    return zc::none;
  }
  if (callerShape == zc::none) { return zc::none; }
  auto& cs = ZC_ASSERT_NONNULL(callerShape);

  auto callerEntryId = LirBlockId::fromOrdinal(1);
  auto callerContId = LirBlockId::fromOrdinal(2);
  if (callerEntryId == zc::none || callerContId == zc::none) { return zc::none; }

  zc::Vector<Function> functions;

  // Function 0: the caller.
  {
    zc::Vector<BasicBlock> callerBlocks;
    zc::Vector<Statement> entryStatements;
    auto callTerminator = Terminator::callFunction(
        /*calleeIndex=*/1, cs.destinationOrdinal, zc::mv(argumentOperands),
        ZC_REQUIRE_NONNULL(callerContId));
    if (callTerminator == zc::none) { return zc::none; }
    callerBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(callerEntryId), zc::mv(entryStatements),
                                ZC_REQUIRE_NONNULL(zc::mv(callTerminator))));
    callerBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(callerContId), zc::mv(cs.continuationStatements),
                                Terminator::returnLocal(cs.resultOrdinal)));
    zc::Vector<Local> parameters;
    for (size_t p = 0; p < callerParameterCount; ++p) {
      auto carrier = integerCarrierFor(caller.locals[p].type, semanticTypes);
      if (carrier == zc::none) { return zc::none; }
      parameters.add(Local(caller.locals[p].id.ordinal(), ZC_REQUIRE_NONNULL(carrier)));
    }
    functions.add(Function(caller.owner, moduleEntrySymbol(callerParameterCount),
                           calleeResultCarrierValue, zc::mv(parameters), zc::mv(cs.shapeLocals),
                           zc::mv(callerBlocks)));
  }

  // Function 1: the callee, a 2N+2-block chained conditional return.
  {
    zc::Vector<BasicBlock> calleeBlocks;
    for (size_t k = 0; k < armCount; ++k) {
      const auto& arm = arms[k];
      auto entryId = LirBlockId::fromOrdinal(arm.thenBlockOrdinal - 1);
      auto thenId = LirBlockId::fromOrdinal(arm.thenBlockOrdinal);
      auto elseId = LirBlockId::fromOrdinal(arm.elseBlockOrdinal);
      if (entryId == zc::none || thenId == zc::none || elseId == zc::none) { return zc::none; }
      zc::Vector<Statement> entryStatements;
      entryStatements.add(
          Statement::compare(arm.tempOrdinal, arm.op, zc::mv(arm.left), zc::mv(arm.right)));
      calleeBlocks.add(
          BasicBlock(ZC_REQUIRE_NONNULL(entryId), zc::mv(entryStatements),
                     Terminator::condBranch(arm.tempOrdinal, ZC_REQUIRE_NONNULL(thenId),
                                            ZC_REQUIRE_NONNULL(elseId))));
      zc::Vector<Statement> thenStatements;
      thenStatements.add(Statement::assign(calleeResultOrdinal, zc::mv(arm.thenLiteral)));
      auto joinId = LirBlockId::fromOrdinal(joinBlockOrdinal);
      if (joinId == zc::none) { return zc::none; }
      calleeBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(thenId), zc::mv(thenStatements),
                                  Terminator::gotoBlock(ZC_REQUIRE_NONNULL(joinId))));
    }
    // Else arm.
    {
      auto elseId = LirBlockId::fromOrdinal(2 * armCount + 1);
      auto joinId = LirBlockId::fromOrdinal(joinBlockOrdinal);
      if (elseId == zc::none || joinId == zc::none) { return zc::none; }
      zc::Vector<Statement> elseStatements;
      elseStatements.add(
          Statement::assign(calleeResultOrdinal, zc::mv(ZC_ASSERT_NONNULL(elseLiteral))));
      calleeBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(elseId), zc::mv(elseStatements),
                                  Terminator::gotoBlock(ZC_REQUIRE_NONNULL(joinId))));
    }
    // Join block.
    {
      auto joinId = LirBlockId::fromOrdinal(joinBlockOrdinal);
      if (joinId == zc::none) { return zc::none; }
      zc::Vector<Statement> none;
      calleeBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(joinId), zc::mv(none),
                                  Terminator::returnLocal(calleeResultOrdinal)));
    }
    zc::Vector<Local> parameters;
    for (size_t i = 0; i < calleeParameterCount; ++i) {
      const auto& parameterLocal = callee.locals[i];
      auto carrier = integerCarrierFor(parameterLocal.type, semanticTypes);
      if (carrier == zc::none) { carrier = enumCarrierFor(parameterLocal.type, semanticTypes); }
      if (carrier == zc::none) { return zc::none; }
      parameters.add(Local(parameterLocal.id.ordinal(), ZC_REQUIRE_NONNULL(carrier)));
    }
    zc::Vector<Local> locals;
    locals.add(Local(calleeResultOrdinal, calleeResultCarrierValue));
    for (size_t i = 0; i < armCount; ++i) {
      auto tempCarrier =
          boolCarrierFor(callee.locals[calleeParameterCount + 1 + i].type, semanticTypes);
      if (tempCarrier == zc::none) { return zc::none; }
      locals.add(Local(callee.locals[calleeParameterCount + 1 + i].id.ordinal(),
                       ZC_REQUIRE_NONNULL(tempCarrier)));
    }
    functions.add(Function(callee.owner, zc::heapString("zom.callee"), calleeResultCarrierValue,
                           zc::mv(parameters), zc::mv(locals), zc::mv(calleeBlocks)));
  }

  return Module(zc::mv(functions));
}

zc::Maybe<Module> MirToLirLowering::lowerByValueAggregateCallModule(
    const mir::MirFunction& caller, const mir::MirFunction& callee,
    const type::SemanticTypeStore& semanticTypes) {
  // ---- Callee: one struct Parameter local, single block returning one field.
  if (callee.kind != mir::MirFunctionKind::Function || callee.locals.size() != 1 ||
      callee.blocks.size() != 1) {
    return zc::none;
  }
  const auto& calleeParameter = callee.locals[0];
  if (callee.kind != mir::MirFunctionKind::Function ||
      calleeParameter.kind != mir::MirLocalKind::Parameter) {
    return zc::none;
  }
  auto resultCarrier = integerCarrierFor(callee.resultType, semanticTypes);
  if (resultCarrier == zc::none) { return zc::none; }
  const auto resultCarrierValue = ZC_REQUIRE_NONNULL(resultCarrier);
  const auto& calleeBlock = callee.blocks[0];
  if (calleeBlock.statements.size() != 0 ||
      calleeBlock.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& calleeReturn = calleeBlock.terminator.returnValue().value;
  if (calleeReturn == zc::none) { return zc::none; }
  zc::Maybe<identity::DefId> projectedField;
  ZC_IF_SOME(value, calleeReturn) {
    if (value.kind() == mir::MirOperandKind::Constant ||
        value.place().local() != calleeParameter.id || value.place().projections().size() != 1 ||
        value.place().rootType() != calleeParameter.type ||
        value.place().resultType() != callee.resultType) {
      return zc::none;
    }
    const auto& projection = value.place().projections()[0];
    if (projection.kind() != mir::MirProjectionKind::Field ||
        projection.inputType() != calleeParameter.type ||
        projection.resultType() != callee.resultType) {
      return zc::none;
    }
    projectedField = projection.fieldValue().field;
  }
  if (projectedField == zc::none) { return zc::none; }
  const auto projected = ZC_REQUIRE_NONNULL(projectedField);

  // ---- Caller: one aggregate UserLocal and one result Temporary.
  if (caller.kind != mir::MirFunctionKind::Function || caller.locals.size() != 2 ||
      caller.blocks.size() != 2 || caller.resultType != callee.resultType) {
    return zc::none;
  }
  const auto& aggregateLocal = caller.locals[0];
  const auto& resultLocal = caller.locals[1];
  if (aggregateLocal.kind != mir::MirLocalKind::UserLocal ||
      aggregateLocal.type != calleeParameter.type ||
      resultLocal.kind != mir::MirLocalKind::Temporary || resultLocal.type != caller.resultType) {
    return zc::none;
  }
  const auto& entry = caller.blocks[0];
  const auto& continuation = caller.blocks[1];
  if (entry.statements.size() != 3 ||
      entry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != aggregateLocal.id ||
      entry.statements[1].kind() != mir::MirStatementKind::Assign ||
      entry.statements[2].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[2].storageLocal() != resultLocal.id ||
      entry.terminator.kind() != mir::MirTerminatorKind::Call ||
      continuation.statements.size() != 0 ||
      continuation.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& aggregateAssign = entry.statements[1].assignmentValue();
  if (aggregateAssign.destination.local() != aggregateLocal.id ||
      aggregateAssign.destination.projections().size() != 0 ||
      aggregateAssign.value.kind() != mir::MirRvalueKind::NominalAggregate) {
    return zc::none;
  }
  const auto& aggregate = aggregateAssign.value.nominalAggregateValue();
  if (aggregate.type != aggregateLocal.type || aggregate.elements.size() < 2) { return zc::none; }
  // Defense in depth: the checker rejects a repeated property name before facts
  // are published, but two elements must never collapse to one projected slot
  // (last-wins) here either.
  for (size_t left = 0; left < aggregate.elements.size(); ++left) {
    for (size_t right = left + 1; right < aggregate.elements.size(); ++right) {
      if (aggregate.elements[left].field == aggregate.elements[right].field) { return zc::none; }
    }
  }
  const auto& call = entry.terminator.callValue();
  if (call.arguments.size() != 1 || call.destination.local() != resultLocal.id ||
      call.destination.projections().size() != 0 || call.normalTarget != continuation.id ||
      call.unwindTarget != zc::none || !(call.callee == callee.owner)) {
    return zc::none;
  }
  const auto& callArgument = call.arguments[0];
  if (callArgument.kind() == mir::MirOperandKind::Constant ||
      callArgument.place().local() != aggregateLocal.id ||
      callArgument.place().projections().size() != 0 ||
      callArgument.place().rootType() != aggregateLocal.type ||
      callArgument.place().resultType() != aggregateLocal.type) {
    return zc::none;
  }
  const auto& continuationReturn = continuation.terminator.returnValue().value;
  if (continuationReturn == zc::none) { return zc::none; }
  ZC_IF_SOME(value, continuationReturn) {
    if (value.kind() == mir::MirOperandKind::Constant || value.place().local() != resultLocal.id ||
        value.place().projections().size() != 0) {
      return zc::none;
    }
  }

  // ---- Flatten the aggregate into ordered integer call arguments and resolve
  // the callee's projected field to its parameter-slot ordinal. The aggregate
  // element order is the admitted slice's only field ordering.
  zc::Vector<Operand> argumentOperands;
  zc::Maybe<uint32_t> projectedSlotOrdinal;
  for (size_t index = 0; index < aggregate.elements.size(); ++index) {
    const auto& element = aggregate.elements[index];
    const auto& elementOperand = element.operand;
    if (elementOperand.kind() != mir::MirOperandKind::Constant) { return zc::none; }
    auto elementCarrier = integerCarrierFor(elementOperand.constantValue().type, semanticTypes);
    if (elementCarrier == zc::none ||
        ZC_REQUIRE_NONNULL(elementCarrier).integerWidth() != resultCarrierValue.integerWidth()) {
      return zc::none;
    }
    auto lowered = lirOperandFor(elementOperand, resultCarrierValue);
    if (lowered == zc::none) { return zc::none; }
    argumentOperands.add(ZC_REQUIRE_NONNULL(lowered));
    if (element.field == projected) { projectedSlotOrdinal = index + 1; }
  }
  if (projectedSlotOrdinal == zc::none) { return zc::none; }
  const uint32_t projectedOrdinal = ZC_REQUIRE_NONNULL(projectedSlotOrdinal);

  auto callerEntryId = LirBlockId::fromOrdinal(1);
  auto callerContId = LirBlockId::fromOrdinal(2);
  auto calleeEntryId = LirBlockId::fromOrdinal(1);
  if (callerEntryId == zc::none || callerContId == zc::none || calleeEntryId == zc::none) {
    return zc::none;
  }

  zc::Vector<Function> functions;
  // Function 0: the runnable entry. The aggregate local folds into the call's
  // constant arguments and is dropped; the result temporary is renumbered to the
  // sole dense body-local slot (ordinal 1) and is the call destination and
  // return slot.
  {
    constexpr uint32_t lirResultOrdinal = 1;
    zc::Vector<BasicBlock> callerBlocks;
    zc::Vector<Statement> entryStatements;
    auto callTerminator = Terminator::callFunction(
        /*calleeIndex=*/1, lirResultOrdinal, zc::mv(argumentOperands),
        ZC_REQUIRE_NONNULL(callerContId));
    if (callTerminator == zc::none) { return zc::none; }
    callerBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(callerEntryId), zc::mv(entryStatements),
                                ZC_REQUIRE_NONNULL(zc::mv(callTerminator))));
    zc::Vector<Statement> continuationStatements;
    callerBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(callerContId), zc::mv(continuationStatements),
                                Terminator::returnLocal(lirResultOrdinal)));
    zc::Vector<Local> noParameters;
    zc::Vector<Local> locals;
    locals.add(Local(lirResultOrdinal, resultCarrierValue));
    functions.add(Function(caller.owner, zc::heapString("zom.module_init"), resultCarrierValue,
                           zc::mv(noParameters), zc::mv(locals), zc::mv(callerBlocks)));
  }
  // Function 1: the callee with one integer parameter slot per flattened struct
  // field, returning the projected field's slot.
  {
    zc::Vector<BasicBlock> calleeBlocks;
    calleeBlocks.add(
        BasicBlock(ZC_REQUIRE_NONNULL(calleeEntryId), Terminator::returnLocal(projectedOrdinal)));
    zc::Vector<Local> parameters;
    for (size_t index = 0; index < aggregate.elements.size(); ++index) {
      parameters.add(Local(static_cast<uint32_t>(index + 1), resultCarrierValue));
    }
    zc::Vector<Local> locals;
    functions.add(Function(callee.owner, zc::heapString("zom.callee"), resultCarrierValue,
                           zc::mv(parameters), zc::mv(locals), zc::mv(calleeBlocks)));
  }

  return Module(zc::mv(functions));
}

zc::Maybe<Module> MirToLirLowering::lowerScalarLocalCallModule(
    const mir::MirFunction& caller, const mir::MirFunction& callee,
    const type::SemanticTypeStore& semanticTypes) {
  // ---- Caller: one scalar UserLocal initialized from a constant plus one
  // result Temporary; the local is copied as the sole call argument.
  if (caller.kind != mir::MirFunctionKind::Function || caller.sourceScopes.size() != 1 ||
      caller.locals.size() != 2 || caller.blocks.size() != 2 ||
      caller.resultType != callee.resultType) {
    return zc::none;
  }
  const auto& scalarLocal = caller.locals[0];
  const auto& resultLocal = caller.locals[1];
  if (scalarLocal.kind != mir::MirLocalKind::UserLocal || scalarLocal.id.ordinal() != 1 ||
      resultLocal.kind != mir::MirLocalKind::Temporary || resultLocal.id.ordinal() != 2 ||
      resultLocal.type != caller.resultType) {
    return zc::none;
  }
  auto callerCarrier = integerCarrierFor(caller.resultType, semanticTypes);
  if (callerCarrier == zc::none) {
    callerCarrier = boolCarrierFor(caller.resultType, semanticTypes);
  }
  if (callerCarrier == zc::none) { return zc::none; }
  const auto callerCarrierValue = ZC_REQUIRE_NONNULL(callerCarrier);
  if (callerCarrierValue.kind() != ValueTypeKind::Integer) { return zc::none; }
  {
    auto scalarCarrier = integerCarrierFor(scalarLocal.type, semanticTypes);
    if (scalarCarrier == zc::none) {
      scalarCarrier = boolCarrierFor(scalarLocal.type, semanticTypes);
    }
    if (scalarCarrier != callerCarrier) { return zc::none; }
  }
  const auto& entry = caller.blocks[0];
  const auto& continuation = caller.blocks[1];
  if (entry.statements.size() != 3 ||
      entry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != scalarLocal.id ||
      entry.statements[1].kind() != mir::MirStatementKind::Assign ||
      entry.statements[2].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[2].storageLocal() != resultLocal.id ||
      entry.terminator.kind() != mir::MirTerminatorKind::Call ||
      continuation.statements.size() != 0 ||
      continuation.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& initialization = entry.statements[1].assignmentValue();
  if (initialization.initialization != mir::MirInitializationKind::Initialize ||
      initialization.destination.local() != scalarLocal.id ||
      initialization.destination.projections().size() != 0 ||
      initialization.value.kind() != mir::MirRvalueKind::Use) {
    return zc::none;
  }
  const auto& initializerOperand = initialization.value.useValue().operand;
  if (initializerOperand.kind() != mir::MirOperandKind::Constant ||
      initializerOperand.constantValue().type != scalarLocal.type) {
    return zc::none;
  }
  auto loweredConstant = lirOperandFor(initializerOperand, callerCarrierValue);
  if (loweredConstant == zc::none) { return zc::none; }
  const auto& call = entry.terminator.callValue();
  if (call.arguments.size() != 1 || call.destination.local() != resultLocal.id ||
      call.destination.projections().size() != 0 || call.normalTarget != continuation.id ||
      call.unwindTarget != zc::none || !(call.callee == callee.owner)) {
    return zc::none;
  }
  const auto& callArgument = call.arguments[0];
  if (callArgument.kind() == mir::MirOperandKind::Constant ||
      callArgument.place().local() != scalarLocal.id ||
      callArgument.place().projections().size() != 0 ||
      callArgument.place().rootType() != scalarLocal.type ||
      callArgument.place().resultType() != scalarLocal.type) {
    return zc::none;
  }
  const auto& continuationReturn = continuation.terminator.returnValue().value;
  if (continuationReturn == zc::none) { return zc::none; }
  ZC_IF_SOME(value, continuationReturn) {
    if (value.kind() == mir::MirOperandKind::Constant || value.place().local() != resultLocal.id ||
        value.place().projections().size() != 0) {
      return zc::none;
    }
  }

  // ---- Callee: one integer parameter local; a single block that either
  // returns the parameter directly or computes an admitted use/arithmetic body.
  if (callee.kind != mir::MirFunctionKind::Function || callee.sourceScopes.size() != 1 ||
      callee.blocks.size() != 1 || callee.locals.size() < 1) {
    return zc::none;
  }
  const auto& calleeBlock = callee.blocks[0];
  size_t calleeParameterCount = 0;
  while (calleeParameterCount < callee.locals.size() &&
         callee.locals[calleeParameterCount].kind == mir::MirLocalKind::Parameter) {
    ++calleeParameterCount;
  }
  if (calleeParameterCount != 1) { return zc::none; }
  const auto& calleeParameter = callee.locals[0];
  if (calleeParameter.kind != mir::MirLocalKind::Parameter) { return zc::none; }
  auto calleeCarrier = integerCarrierFor(callee.resultType, semanticTypes);
  if (calleeCarrier == zc::none) {
    calleeCarrier = boolCarrierFor(callee.resultType, semanticTypes);
  }
  if (calleeCarrier == zc::none || ZC_REQUIRE_NONNULL(calleeCarrier) != callerCarrierValue) {
    return zc::none;
  }
  for (const auto& local : callee.locals) {
    auto localCarrier = integerCarrierFor(local.type, semanticTypes);
    if (localCarrier == zc::none) { localCarrier = boolCarrierFor(local.type, semanticTypes); }
    if (localCarrier != calleeCarrier) { return zc::none; }
  }
  const size_t calleeBodyCount = callee.locals.size() - 1;
  for (size_t index = 1; index < callee.locals.size(); ++index) {
    const auto kind = callee.locals[index].kind;
    if (kind != mir::MirLocalKind::UserLocal && kind != mir::MirLocalKind::Temporary &&
        kind != mir::MirLocalKind::FunctionResult) {
      return zc::none;
    }
  }
  if (calleeBlock.terminator.kind() != mir::MirTerminatorKind::Return) { return zc::none; }
  const auto& calleeReturnValue = calleeBlock.terminator.returnValue().value;
  if (calleeReturnValue == zc::none) { return zc::none; }

  zc::Vector<Statement> calleeStatements;
  uint32_t calleeReturnOrdinal = calleeParameter.id.ordinal();
  if (calleeBodyCount == 0) {
    // Identity callee: an empty block returning the parameter slot.
    if (calleeBlock.statements.size() != 0) { return zc::none; }
    ZC_IF_SOME(value, calleeReturnValue) {
      if (value.kind() == mir::MirOperandKind::Constant ||
          value.place().local() != calleeParameter.id || value.place().projections().size() != 0) {
        return zc::none;
      }
    }
  } else {
    // Use/arithmetic callee body, validated and lowered like the arithmetic
    // slice: each body local has a StorageLive plus an initializing Assign of a
    // Use or Arithmetic rvalue, and the terminator returns the last local.
    if (calleeBlock.statements.size() != calleeBodyCount * 2) { return zc::none; }
    auto leafOperand = [&](const mir::MirOperand& operand) -> zc::Maybe<Operand> {
      if (operand.kind() == mir::MirOperandKind::Constant) {
        return lirOperandFor(operand, callerCarrierValue);
      }
      if (operand.place().projections().size() != 0) { return zc::none; }
      const uint32_t ordinal = operand.place().local().ordinal();
      bool declared = false;
      for (const auto& local : callee.locals) {
        if (local.id.ordinal() == ordinal) {
          declared = true;
          break;
        }
      }
      if (!declared) { return zc::none; }
      return Operand::localUse(ordinal);
    };
    for (size_t index = 0; index < calleeBodyCount; ++index) {
      const auto& localDecl = callee.locals[1 + index];
      const auto& liveStatement = calleeBlock.statements[2 * index];
      const auto& assignStatement = calleeBlock.statements[2 * index + 1];
      if (liveStatement.kind() != mir::MirStatementKind::StorageLive ||
          liveStatement.storageLocal() != localDecl.id ||
          assignStatement.kind() != mir::MirStatementKind::Assign) {
        return zc::none;
      }
      const auto& assignment = assignStatement.assignmentValue();
      if (assignment.destination.local() != localDecl.id ||
          assignment.destination.projections().size() != 0 ||
          assignment.initialization != mir::MirInitializationKind::Initialize) {
        return zc::none;
      }
      const uint32_t destinationOrdinal = localDecl.id.ordinal();
      if (assignment.value.kind() == mir::MirRvalueKind::Use) {
        auto lowered = leafOperand(assignment.value.useValue().operand);
        if (lowered == zc::none) { return zc::none; }
        calleeStatements.add(Statement::assign(destinationOrdinal, ZC_REQUIRE_NONNULL(lowered)));
      } else if (assignment.value.kind() == mir::MirRvalueKind::Arithmetic) {
        const auto& arithmetic = assignment.value.arithmeticValue();
        auto op = lirArithmeticOpFor(arithmetic.op);
        if (op == zc::none || arithmetic.resultType != localDecl.type) { return zc::none; }
        auto left = leafOperand(arithmetic.left);
        auto right = leafOperand(arithmetic.right);
        if (left == zc::none || right == zc::none) { return zc::none; }
        calleeStatements.add(Statement::arithmetic(destinationOrdinal, ZC_REQUIRE_NONNULL(op),
                                                   ZC_REQUIRE_NONNULL(left),
                                                   ZC_REQUIRE_NONNULL(right)));
      } else if (assignment.value.kind() == mir::MirRvalueKind::Comparison) {
        const auto& comparison = assignment.value.comparisonValue();
        auto op = lirComparisonOpFor(comparison.op);
        if (comparison.resultType != localDecl.type) { return zc::none; }
        auto left = leafOperand(comparison.left);
        auto right = leafOperand(comparison.right);
        if (left == zc::none || right == zc::none) { return zc::none; }
        calleeStatements.add(Statement::compare(destinationOrdinal, op, ZC_REQUIRE_NONNULL(left),
                                                ZC_REQUIRE_NONNULL(right)));
      } else {
        return zc::none;
      }
    }
    const auto& lastLocal = callee.locals[callee.locals.size() - 1];
    calleeReturnOrdinal = lastLocal.id.ordinal();
    ZC_IF_SOME(value, calleeReturnValue) {
      if (value.kind() == mir::MirOperandKind::Constant || value.place().local() != lastLocal.id ||
          value.place().projections().size() != 0) {
        return zc::none;
      }
    }
  }

  auto callerEntryId = LirBlockId::fromOrdinal(1);
  auto callerContId = LirBlockId::fromOrdinal(2);
  auto calleeEntryId = LirBlockId::fromOrdinal(1);
  if (callerEntryId == zc::none || callerContId == zc::none || calleeEntryId == zc::none) {
    return zc::none;
  }

  zc::Vector<Function> functions;
  // Function 0: the runnable entry. The scalar local keeps its constant holder
  // slot; the call passes that slot and stores into the result slot.
  {
    zc::Vector<BasicBlock> callerBlocks;
    zc::Vector<Statement> entryStatements;
    entryStatements.add(
        Statement::assign(scalarLocal.id.ordinal(), ZC_REQUIRE_NONNULL(loweredConstant)));
    zc::Vector<Operand> argumentOperands;
    argumentOperands.add(Operand::localUse(scalarLocal.id.ordinal()));
    auto callTerminator = Terminator::callFunction(
        /*calleeIndex=*/1, resultLocal.id.ordinal(), zc::mv(argumentOperands),
        ZC_REQUIRE_NONNULL(callerContId));
    if (callTerminator == zc::none) { return zc::none; }
    callerBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(callerEntryId), zc::mv(entryStatements),
                                ZC_REQUIRE_NONNULL(zc::mv(callTerminator))));
    zc::Vector<Statement> continuationStatements;
    callerBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(callerContId), zc::mv(continuationStatements),
                                Terminator::returnLocal(resultLocal.id.ordinal())));
    zc::Vector<Local> noParameters;
    zc::Vector<Local> locals;
    locals.add(Local(scalarLocal.id.ordinal(), callerCarrierValue));
    locals.add(Local(resultLocal.id.ordinal(), callerCarrierValue));
    functions.add(Function(caller.owner, zc::heapString("zom.module_init"), callerCarrierValue,
                           zc::mv(noParameters), zc::mv(locals), zc::mv(callerBlocks)));
  }
  // Function 1: the callee with one integer parameter slot and its lowered
  // use/arithmetic body.
  {
    zc::Vector<BasicBlock> calleeBlocks;
    calleeBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(calleeEntryId), zc::mv(calleeStatements),
                                Terminator::returnLocal(calleeReturnOrdinal)));
    zc::Vector<Local> parameters;
    parameters.add(Local(calleeParameter.id.ordinal(), callerCarrierValue));
    zc::Vector<Local> locals;
    for (size_t index = 1; index < callee.locals.size(); ++index) {
      locals.add(Local(callee.locals[index].id.ordinal(), callerCarrierValue));
    }
    functions.add(Function(callee.owner, zc::heapString("zom.callee"), callerCarrierValue,
                           zc::mv(parameters), zc::mv(locals), zc::mv(calleeBlocks)));
  }

  return Module(zc::mv(functions));
}

zc::Maybe<Module> MirToLirLowering::lowerCallModuleWithArguments(
    const mir::MirFunction& caller, const mir::MirFunction& callee,
    const type::SemanticTypeStore& semanticTypes) {
  // Callee: exactly two parameter locals, one block returning the first parameter.
  // The two parameters share the result carrier (a same-typed integer call in this
  // slice); the return is a place-use of parameter 0.
  if (callee.kind != mir::MirFunctionKind::Function || callee.locals.size() != 2 ||
      callee.blocks.size() != 1) {
    return zc::none;
  }
  const auto& calleeParam0 = callee.locals[0];
  const auto& calleeParam1 = callee.locals[1];
  if (calleeParam0.kind != mir::MirLocalKind::Parameter ||
      calleeParam1.kind != mir::MirLocalKind::Parameter || calleeParam0.type != callee.resultType) {
    return zc::none;
  }
  auto calleeCarrier = integerCarrierFor(callee.resultType, semanticTypes);
  auto calleeParam1Carrier = integerCarrierFor(calleeParam1.type, semanticTypes);
  if (calleeCarrier == zc::none || calleeParam1Carrier == zc::none) { return zc::none; }
  const auto calleeCarrierValue = ZC_REQUIRE_NONNULL(calleeCarrier);
  const uint32_t calleeParam0Ordinal = calleeParam0.id.ordinal();
  const uint32_t calleeParam1Ordinal = calleeParam1.id.ordinal();
  const auto& calleeBlock = callee.blocks[0];
  if (calleeBlock.statements.size() != 0 ||
      calleeBlock.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& calleeReturn = calleeBlock.terminator.returnValue().value;
  if (calleeReturn == zc::none) { return zc::none; }
  bool calleeReturnsParam0 = false;
  ZC_IF_SOME(value, calleeReturn) {
    calleeReturnsParam0 = value.kind() != mir::MirOperandKind::Constant &&
                          value.place().local() == calleeParam0.id &&
                          value.place().projections().size() == 0;
  }
  if (!calleeReturnsParam0) { return zc::none; }

  // Caller: P leading parameter locals then one integer result local, two blocks
  // (entry StorageLive(result) + Call, continuation Return of the result). The
  // two arguments are integer constants or place-uses of leading parameter slots.
  if (caller.kind != mir::MirFunctionKind::Function || caller.locals.size() < 1 ||
      caller.blocks.size() != 2 || caller.resultType != callee.resultType) {
    return zc::none;
  }
  const size_t callerParameterCount = caller.locals.size() - 1;
  for (size_t p = 0; p < callerParameterCount; ++p) {
    if (caller.locals[p].kind != mir::MirLocalKind::Parameter) { return zc::none; }
  }
  const auto& resultLocal = caller.locals[callerParameterCount];
  if (resultLocal.type != caller.resultType) { return zc::none; }
  auto callerCarrier = integerCarrierFor(caller.resultType, semanticTypes);
  if (callerCarrier == zc::none) { return zc::none; }
  const auto callerCarrierValue = ZC_REQUIRE_NONNULL(callerCarrier);
  const uint32_t resultOrdinal = resultLocal.id.ordinal();

  const auto& entry = caller.blocks[0];
  const auto& continuation = caller.blocks[1];
  if (entry.statements.size() != 1 ||
      entry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != resultLocal.id ||
      entry.terminator.kind() != mir::MirTerminatorKind::Call) {
    return zc::none;
  }
  const auto& call = entry.terminator.callValue();
  // Exactly two arguments; the call must target the identified callee.
  if (call.arguments.size() != 2 || call.destination.local() != resultLocal.id ||
      call.destination.projections().size() != 0 || call.normalTarget != continuation.id ||
      call.unwindTarget != zc::none) {
    return zc::none;
  }
  if (!(call.callee == callee.owner)) { return zc::none; }

  // Lower both arguments in order. Each is either an integer constant of the
  // matching callee parameter type or a bare use of a leading parameter slot;
  // a projected or non-parameter place is outside the slice.
  const identity::SemanticTypeId calleeParamTypes[] = {calleeParam0.type, calleeParam1.type};
  const ValueType calleeParamCarriers[] = {calleeCarrierValue,
                                           ZC_REQUIRE_NONNULL(calleeParam1Carrier)};
  auto namesLeadingParameter = [&](const mir::MirOperand& operand) -> bool {
    if (operand.kind() == mir::MirOperandKind::Constant ||
        operand.place().projections().size() != 0) {
      return false;
    }
    const uint32_t ordinal = operand.place().local().ordinal();
    for (size_t p = 0; p < callerParameterCount; ++p) {
      if (caller.locals[p].id.ordinal() == ordinal) { return true; }
    }
    return false;
  };
  zc::Vector<Operand> argumentOperands;
  for (size_t index = 0; index < call.arguments.size(); ++index) {
    const auto& argument = call.arguments[index];
    if (argument.kind() == mir::MirOperandKind::Constant) {
      if (argument.constantValue().type != calleeParamTypes[index]) { return zc::none; }
    } else if (!namesLeadingParameter(argument)) {
      return zc::none;
    }
    auto lowered = lirOperandFor(argument, calleeParamCarriers[index]);
    if (lowered == zc::none) { return zc::none; }
    argumentOperands.add(ZC_REQUIRE_NONNULL(lowered));
  }

  if (continuation.statements.size() != 0 ||
      continuation.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& continuationReturn = continuation.terminator.returnValue().value;
  if (continuationReturn == zc::none) { return zc::none; }
  bool returnsResult = false;
  ZC_IF_SOME(value, continuationReturn) {
    returnsResult = value.kind() != mir::MirOperandKind::Constant &&
                    value.place().local() == resultLocal.id &&
                    value.place().projections().size() == 0;
  }
  if (!returnsResult) { return zc::none; }

  auto callerEntryId = LirBlockId::fromOrdinal(1);
  auto callerContId = LirBlockId::fromOrdinal(2);
  auto calleeEntryId = LirBlockId::fromOrdinal(1);
  if (callerEntryId == zc::none || callerContId == zc::none || calleeEntryId == zc::none) {
    return zc::none;
  }

  zc::Vector<Function> functions;

  // Function 0: the caller. Its call targets function index 1 (the callee), passes
  // the two constant or parameter-slot arguments, stores the integer result into
  // the result slot, and continues to the return.
  {
    zc::Vector<BasicBlock> callerBlocks;
    zc::Vector<Statement> entryStatements;
    auto callTerminator = Terminator::callFunction(
        /*calleeIndex=*/1, resultOrdinal, zc::mv(argumentOperands),
        ZC_REQUIRE_NONNULL(callerContId));
    if (callTerminator == zc::none) { return zc::none; }
    callerBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(callerEntryId), zc::mv(entryStatements),
                                ZC_REQUIRE_NONNULL(zc::mv(callTerminator))));
    zc::Vector<Statement> contStatements;
    callerBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(callerContId), zc::mv(contStatements),
                                Terminator::returnLocal(resultOrdinal)));
    zc::Vector<Local> parameters;
    for (size_t p = 0; p < callerParameterCount; ++p) {
      auto carrier = integerCarrierFor(caller.locals[p].type, semanticTypes);
      if (carrier == zc::none) { return zc::none; }
      parameters.add(Local(caller.locals[p].id.ordinal(), ZC_REQUIRE_NONNULL(carrier)));
    }
    zc::Vector<Local> locals;
    locals.add(Local(resultOrdinal, callerCarrierValue));
    functions.add(Function(caller.owner, moduleEntrySymbol(callerParameterCount),
                           callerCarrierValue, zc::mv(parameters), zc::mv(locals),
                           zc::mv(callerBlocks)));
  }

  // Function 1: the callee, two parameters, a single block returning parameter 0.
  {
    zc::Vector<BasicBlock> calleeBlocks;
    zc::Vector<Statement> none;
    calleeBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(calleeEntryId), zc::mv(none),
                                Terminator::returnLocal(calleeParam0Ordinal)));
    zc::Vector<Local> parameters;
    parameters.add(Local(calleeParam0Ordinal, calleeCarrierValue));
    parameters.add(Local(calleeParam1Ordinal, ZC_REQUIRE_NONNULL(calleeParam1Carrier)));
    zc::Vector<Local> locals;
    functions.add(Function(callee.owner, zc::heapString("zom.callee"), calleeCarrierValue,
                           zc::mv(parameters), zc::mv(locals), zc::mv(calleeBlocks)));
  }

  return Module(zc::mv(functions));
}

zc::Maybe<Module> MirToLirLowering::lowerCallModuleWithLeaf(
    const mir::MirFunction& caller, const mir::MirFunction& callee, const mir::MirFunction& leaf,
    const type::SemanticTypeStore& semanticTypes) {
  // Callee: the scalar constant-return shape (no locals, one block returning an
  // integer constant), identical to the zero-argument call callee.
  if (callee.kind != mir::MirFunctionKind::Function || callee.locals.size() != 0 ||
      callee.blocks.size() != 1) {
    return zc::none;
  }
  auto calleeCarrier = integerCarrierFor(callee.resultType, semanticTypes);
  if (calleeCarrier == zc::none) { return zc::none; }
  const auto calleeCarrierValue = ZC_REQUIRE_NONNULL(calleeCarrier);
  const auto& calleeBlock = callee.blocks[0];
  if (calleeBlock.statements.size() != 0 ||
      calleeBlock.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& calleeReturn = calleeBlock.terminator.returnValue().value;
  if (calleeReturn == zc::none) { return zc::none; }
  zc::Maybe<IntegerConstant> calleeConstant;
  ZC_IF_SOME(value, calleeReturn) {
    if (value.kind() != mir::MirOperandKind::Constant ||
        value.constantValue().type != callee.resultType) {
      return zc::none;
    }
    const auto integer = value.constantValue().value.integerValue();
    if (integer == zc::none) { return zc::none; }
    auto bits = zeroExtendedBits(ZC_REQUIRE_NONNULL(integer), calleeCarrierValue.integerWidth());
    if (bits == zc::none) { return zc::none; }
    calleeConstant = IntegerConstant::from(calleeCarrierValue, ZC_REQUIRE_NONNULL(bits));
  }
  if (calleeConstant == zc::none) { return zc::none; }

  // Leaf: the same scalar constant-return shape, but referenced by no call. It is
  // an independent module-local function emitted alongside the caller/callee pair.
  if (leaf.kind != mir::MirFunctionKind::Function || leaf.locals.size() != 0 ||
      leaf.blocks.size() != 1) {
    return zc::none;
  }
  auto leafCarrier = integerCarrierFor(leaf.resultType, semanticTypes);
  if (leafCarrier == zc::none) { return zc::none; }
  const auto leafCarrierValue = ZC_REQUIRE_NONNULL(leafCarrier);
  const auto& leafBlock = leaf.blocks[0];
  if (leafBlock.statements.size() != 0 ||
      leafBlock.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& leafReturn = leafBlock.terminator.returnValue().value;
  if (leafReturn == zc::none) { return zc::none; }
  zc::Maybe<IntegerConstant> leafConstant;
  ZC_IF_SOME(value, leafReturn) {
    if (value.kind() != mir::MirOperandKind::Constant ||
        value.constantValue().type != leaf.resultType) {
      return zc::none;
    }
    const auto integer = value.constantValue().value.integerValue();
    if (integer == zc::none) { return zc::none; }
    auto bits = zeroExtendedBits(ZC_REQUIRE_NONNULL(integer), leafCarrierValue.integerWidth());
    if (bits == zc::none) { return zc::none; }
    leafConstant = IntegerConstant::from(leafCarrierValue, ZC_REQUIRE_NONNULL(bits));
  }
  if (leafConstant == zc::none) { return zc::none; }

  // Caller: P leading parameter locals then one integer result local, two blocks
  // (entry StorageLive(result) + zero-argument Call, continuation Return). The
  // call must target the identified callee, never the leaf.
  if (caller.kind != mir::MirFunctionKind::Function || caller.locals.size() < 1 ||
      caller.blocks.size() != 2 || caller.resultType != callee.resultType) {
    return zc::none;
  }
  const size_t callerParameterCount = caller.locals.size() - 1;
  for (size_t p = 0; p < callerParameterCount; ++p) {
    if (caller.locals[p].kind != mir::MirLocalKind::Parameter) { return zc::none; }
  }
  const auto& resultLocal = caller.locals[callerParameterCount];
  if (resultLocal.type != caller.resultType) { return zc::none; }
  auto callerCarrier = integerCarrierFor(caller.resultType, semanticTypes);
  if (callerCarrier == zc::none) { return zc::none; }
  const auto callerCarrierValue = ZC_REQUIRE_NONNULL(callerCarrier);
  const uint32_t resultOrdinal = resultLocal.id.ordinal();

  const auto& entry = caller.blocks[0];
  const auto& continuation = caller.blocks[1];
  if (entry.statements.size() != 1 ||
      entry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != resultLocal.id ||
      entry.terminator.kind() != mir::MirTerminatorKind::Call) {
    return zc::none;
  }
  const auto& call = entry.terminator.callValue();
  if (call.arguments.size() != 0 || call.destination.local() != resultLocal.id ||
      call.destination.projections().size() != 0 || call.normalTarget != continuation.id ||
      call.unwindTarget != zc::none) {
    return zc::none;
  }
  // The call's target definition must be the identified callee; the leaf is never
  // a call target, so a caller pointing at the leaf's owner is rejected here.
  if (!(call.callee == callee.owner)) { return zc::none; }
  if (continuation.statements.size() != 0 ||
      continuation.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& continuationReturn = continuation.terminator.returnValue().value;
  if (continuationReturn == zc::none) { return zc::none; }
  bool returnsResult = false;
  ZC_IF_SOME(value, continuationReturn) {
    returnsResult = value.kind() != mir::MirOperandKind::Constant &&
                    value.place().local() == resultLocal.id &&
                    value.place().projections().size() == 0;
  }
  if (!returnsResult) { return zc::none; }

  auto callerEntryId = LirBlockId::fromOrdinal(1);
  auto callerContId = LirBlockId::fromOrdinal(2);
  auto calleeEntryId = LirBlockId::fromOrdinal(1);
  auto leafEntryId = LirBlockId::fromOrdinal(1);
  if (callerEntryId == zc::none || callerContId == zc::none || calleeEntryId == zc::none ||
      leafEntryId == zc::none) {
    return zc::none;
  }

  zc::Vector<Function> functions;

  // Function 0: the caller. Its call targets function index 1 (the callee) in the
  // fixed emission order below; the leaf at index 2 is never referenced. The
  // calleeIndex is the emission-order position, not the MIR array position.
  {
    zc::Vector<BasicBlock> callerBlocks;
    zc::Vector<Statement> entryStatements;
    zc::Vector<Operand> noArguments;
    auto callTerminator = Terminator::callFunction(
        /*calleeIndex=*/1, resultOrdinal, zc::mv(noArguments), ZC_REQUIRE_NONNULL(callerContId));
    if (callTerminator == zc::none) { return zc::none; }
    callerBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(callerEntryId), zc::mv(entryStatements),
                                ZC_REQUIRE_NONNULL(zc::mv(callTerminator))));
    zc::Vector<Statement> contStatements;
    callerBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(callerContId), zc::mv(contStatements),
                                Terminator::returnLocal(resultOrdinal)));
    zc::Vector<Local> parameters;
    for (size_t p = 0; p < callerParameterCount; ++p) {
      auto carrier = integerCarrierFor(caller.locals[p].type, semanticTypes);
      if (carrier == zc::none) { return zc::none; }
      parameters.add(Local(caller.locals[p].id.ordinal(), ZC_REQUIRE_NONNULL(carrier)));
    }
    zc::Vector<Local> locals;
    locals.add(Local(resultOrdinal, callerCarrierValue));
    functions.add(Function(caller.owner, moduleEntrySymbol(callerParameterCount),
                           callerCarrierValue, zc::mv(parameters), zc::mv(locals),
                           zc::mv(callerBlocks)));
  }

  // Function 1: the callee, a single block returning its integer constant.
  {
    zc::Vector<BasicBlock> calleeBlocks;
    calleeBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(calleeEntryId),
                                Terminator::returnInteger(ZC_REQUIRE_NONNULL(calleeConstant))));
    functions.add(Function(callee.owner, zc::heapString("zom.callee"), calleeCarrierValue,
                           zc::mv(calleeBlocks)));
  }

  // Function 2: the standalone leaf, a single block returning its integer
  // constant. It calls nothing and is called by nothing.
  {
    zc::Vector<BasicBlock> leafBlocks;
    leafBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(leafEntryId),
                              Terminator::returnInteger(ZC_REQUIRE_NONNULL(leafConstant))));
    functions.add(
        Function(leaf.owner, zc::heapString("zom.leaf"), leafCarrierValue, zc::mv(leafBlocks)));
  }

  return Module(zc::mv(functions));
}

zc::Maybe<Module> MirToLirLowering::lowerReceiverCallModule(
    const mir::MirFunction& caller, const mir::MirFunction& callee,
    const type::SemanticTypeStore& semanticTypes) {
  // Callee: a Method-sourced function whose first parameter local is the
  // receiver reference parameter (shared or mutable). It admits exactly one
  // block with one of:
  // (A) a scalar-constant return and no ordinary parameters (the
  //     literal-method slice),
  // (B) a [Dereference, Field] place-use of the receiver and no ordinary
  //     parameters (the `this.field` read slice),
  // (C) exactly one ordinary integer parameter local returned bare (the
  //     parameter-return method slice), or
  // (E) one user local StorageLive'd and Initialize-assigned a constant that
  //     is then returned bare (the constant-local fold; the local and its
  //     statements fold away, leaving a constant-return callee).
  if (callee.kind != mir::MirFunctionKind::Function ||
      callee.sourceDefinitionKind != identity::DefinitionKind::Method ||
      (callee.locals.size() != 1 && callee.locals.size() != 2 && callee.locals.size() != 3) ||
      callee.blocks.size() != 1) {
    // The one multi-block callee shape is the shared-receiver method
    // conditional: a receiver pointer plus one bool ordinary parameter, a
    // FunctionResult local, and a four-block diamond returning literal arms.
    if (callee.kind == mir::MirFunctionKind::Function &&
        callee.sourceDefinitionKind == identity::DefinitionKind::Method &&
        callee.locals.size() == 3 && callee.blocks.size() == 4) {
      return lowerReceiverConditionalCallModule(caller, callee, semanticTypes);
    }
    return zc::none;
  }
  const auto& receiverLocal = callee.locals[0];
  if (receiverLocal.kind != mir::MirLocalKind::Parameter) { return zc::none; }
  auto calleeReceiverMutable = receiverReferenceIsMutable(receiverLocal.type, semanticTypes);
  if (calleeReceiverMutable == zc::none) { return zc::none; }
  const bool calleeIsMutable = ZC_ASSERT_NONNULL(calleeReceiverMutable);
  auto receiverCarrier = receiverPointerCarrier(receiverLocal.type, semanticTypes);
  if (receiverCarrier == zc::none) { return zc::none; }
  const auto receiverCarrierValue = ZC_REQUIRE_NONNULL(receiverCarrier);
  auto calleeCarrier = integerCarrierFor(callee.resultType, semanticTypes);
  if (calleeCarrier == zc::none) { return zc::none; }
  const auto calleeCarrierValue = ZC_REQUIRE_NONNULL(calleeCarrier);
  // Every trailing parameter local must be an integer-typed parameter; their
  // carriers become the callee LIR parameter carriers in ordinal order. The one
  // alternative trailing layout is a single UserLocal initialized from a
  // constant and returned bare: the constant-local fold, whose body folds to a
  // constant return and whose user local never becomes an LIR slot.
  zc::Vector<ValueType> ordinaryCarriers;
  bool calleeConstLocalFold = false;
  // Case F: `fun m(this, p) -> T { return p OP <literal|p>; }`. The locals are
  // the receiver parameter, one ordinary integer parameter, and a FunctionResult
  // local; the one block stores the result live then assigns the arithmetic
  // rvalue and returns the result local. Detected after the callee block is
  // read below.
  bool calleeArithmetic = false;
  zc::Maybe<ArithmeticOp> calleeArithmeticOp;
  zc::Maybe<Operand> calleeArithmeticLeft;
  zc::Maybe<Operand> calleeArithmeticRight;
  uint32_t calleeArithmeticResultOrdinal = 0;
  // Case G: `fun m(this) -> T { return this.<field> OP <literal>; }`. The locals
  // are the receiver parameter and a FunctionResult; the one block stores the
  // result live then assigns an arithmetic rvalue whose one operand is a
  // [Dereference, Field] place-use of the receiver and whose other operand is a
  // constant. The field is first loaded into a synthesized slot and then fed to
  // the arithmetic statement.
  bool calleeFieldArithmetic = false;
  zc::Maybe<ArithmeticOp> calleeFieldArithmeticOp;
  zc::Maybe<Operand> calleeFieldArithmeticLeft;
  zc::Maybe<Operand> calleeFieldArithmeticRight;
  uint32_t calleeFieldArithmeticFieldOrdinal = 0;
  uint32_t calleeFieldArithmeticResultOrdinal = 0;

  const auto& calleeBlock = callee.blocks[0];
  if (calleeBlock.terminator.kind() != mir::MirTerminatorKind::Return) { return zc::none; }

  if (callee.locals.size() == 3 && callee.locals[1].kind == mir::MirLocalKind::Parameter &&
      callee.locals[2].kind == mir::MirLocalKind::FunctionResult &&
      callee.locals[1].id.ordinal() == receiverLocal.id.ordinal() + 1 &&
      callee.locals[2].id.ordinal() == receiverLocal.id.ordinal() + 2 &&
      calleeBlock.statements.size() == 2 &&
      calleeBlock.statements[0].kind() == mir::MirStatementKind::StorageLive &&
      calleeBlock.statements[0].storageLocal() == callee.locals[2].id &&
      calleeBlock.statements[1].kind() == mir::MirStatementKind::Assign) {
    const auto& arithmeticAssignment = calleeBlock.statements[1].assignmentValue();
    if (arithmeticAssignment.destination.local() == callee.locals[2].id &&
        arithmeticAssignment.destination.projections().size() == 0 &&
        arithmeticAssignment.initialization == mir::MirInitializationKind::Initialize &&
        arithmeticAssignment.value.kind() == mir::MirRvalueKind::Arithmetic) {
      const auto& arithmetic = arithmeticAssignment.value.arithmeticValue();
      auto op = lirArithmeticOpFor(arithmetic.op);
      auto ordinaryCarrier = integerCarrierFor(callee.locals[1].type, semanticTypes);
      auto left = lirOperandFor(arithmetic.left, ZC_REQUIRE_NONNULL(ordinaryCarrier));
      auto right = lirOperandFor(arithmetic.right, ZC_REQUIRE_NONNULL(ordinaryCarrier));
      const auto& terminatorReturn = calleeBlock.terminator.returnValue().value;
      bool returnsResult = false;
      ZC_IF_SOME(returned, terminatorReturn) {
        returnsResult = returned.kind() != mir::MirOperandKind::Constant &&
                        returned.place().local() == callee.locals[2].id &&
                        returned.place().projections().size() == 0;
      }
      if (op != zc::none && ordinaryCarrier != zc::none && left != zc::none && right != zc::none &&
          callee.resultType == callee.locals[2].type &&
          callee.resultType == callee.locals[1].type && returnsResult) {
        calleeArithmetic = true;
        calleeArithmeticOp = op;
        calleeArithmeticLeft = zc::mv(left);
        calleeArithmeticRight = zc::mv(right);
        calleeArithmeticResultOrdinal = callee.locals[2].id.ordinal();
        ordinaryCarriers.add(ZC_REQUIRE_NONNULL(ordinaryCarrier));
      }
    }
  }
  // Case G detection: two locals (receiver parameter, FunctionResult) and one
  // block whose storage-live/assign pair initializes the result from an
  // arithmetic rvalue over a projected receiver field and a constant.
  if (!calleeArithmetic && callee.locals.size() == 2 &&
      callee.locals[1].kind == mir::MirLocalKind::FunctionResult &&
      callee.locals[1].id.ordinal() == receiverLocal.id.ordinal() + 1 &&
      calleeBlock.statements.size() == 2 &&
      calleeBlock.statements[0].kind() == mir::MirStatementKind::StorageLive &&
      calleeBlock.statements[0].storageLocal() == callee.locals[1].id &&
      calleeBlock.statements[1].kind() == mir::MirStatementKind::Assign) {
    const auto& fieldAssignment = calleeBlock.statements[1].assignmentValue();
    if (fieldAssignment.destination.local() == callee.locals[1].id &&
        fieldAssignment.destination.projections().size() == 0 &&
        fieldAssignment.initialization == mir::MirInitializationKind::Initialize &&
        fieldAssignment.value.kind() == mir::MirRvalueKind::Arithmetic) {
      const auto& arithmetic = fieldAssignment.value.arithmeticValue();
      auto op = lirArithmeticOpFor(arithmetic.op);
      auto isReceiverFieldUse = [&](const mir::MirOperand& operand) -> bool {
        return operand.kind() != mir::MirOperandKind::Constant &&
               operand.place().local() == receiverLocal.id &&
               operand.place().rootType() == receiverLocal.type &&
               operand.place().resultType() == callee.resultType &&
               operand.place().projections().size() == 2 &&
               operand.place().projections()[0].kind() == mir::MirProjectionKind::Dereference &&
               operand.place().projections()[0].inputType() == receiverLocal.type &&
               operand.place().projections()[1].kind() == mir::MirProjectionKind::Field &&
               operand.place().projections()[1].resultType() == callee.resultType;
      };
      auto constOperand = [&](const mir::MirOperand& operand) -> zc::Maybe<IntegerConstant> {
        if (operand.kind() != mir::MirOperandKind::Constant ||
            operand.constantValue().type != callee.resultType) {
          return zc::none;
        }
        auto integer = operand.constantValue().value.integerValue();
        if (integer == zc::none) return zc::none;
        auto bits =
            zeroExtendedBits(ZC_REQUIRE_NONNULL(integer), calleeCarrierValue.integerWidth());
        if (bits == zc::none) return zc::none;
        return IntegerConstant::from(calleeCarrierValue, ZC_REQUIRE_NONNULL(bits));
      };
      const bool fieldIsLeft = isReceiverFieldUse(arithmetic.left);
      const bool fieldIsRight = isReceiverFieldUse(arithmetic.right);
      auto constantValue =
          fieldIsLeft ? constOperand(arithmetic.right) : constOperand(arithmetic.left);
      const auto& terminatorReturn = calleeBlock.terminator.returnValue().value;
      bool returnsResult = false;
      ZC_IF_SOME(returned, terminatorReturn) {
        returnsResult = returned.kind() != mir::MirOperandKind::Constant &&
                        returned.place().local() == callee.locals[1].id &&
                        returned.place().projections().size() == 0;
      }
      if (op != zc::none && fieldIsLeft != fieldIsRight && constantValue != zc::none &&
          callee.resultType == callee.locals[1].type && returnsResult) {
        const uint32_t fieldOrdinal = receiverLocal.id.ordinal() + 1;
        const uint32_t resultOrdinal = receiverLocal.id.ordinal() + 2;
        Operand fieldUse = Operand::localUse(fieldOrdinal);
        Operand constantUse = Operand::constant(ZC_REQUIRE_NONNULL(constantValue));
        calleeFieldArithmetic = true;
        calleeFieldArithmeticOp = op;
        calleeFieldArithmeticLeft = fieldIsLeft ? zc::mv(fieldUse) : zc::mv(constantUse);
        calleeFieldArithmeticRight = fieldIsLeft ? zc::mv(constantUse) : zc::mv(fieldUse);
        calleeFieldArithmeticFieldOrdinal = fieldOrdinal;
        calleeFieldArithmeticResultOrdinal = resultOrdinal;
      }
    }
  }
  // The arithmetic case recorded its ordinary carrier above; the constant-local
  // fold contributes none; every other trailing local is an ordinary parameter.
  if (!calleeArithmetic && !calleeFieldArithmetic && callee.locals.size() == 2 &&
      callee.locals[1].kind == mir::MirLocalKind::UserLocal &&
      callee.locals[1].id.ordinal() == receiverLocal.id.ordinal() + 1) {
    calleeConstLocalFold = true;
  } else if (!calleeArithmetic && !calleeFieldArithmetic) {
    for (size_t i = 1; i < callee.locals.size(); ++i) {
      const auto& parameterLocal = callee.locals[i];
      if (parameterLocal.kind != mir::MirLocalKind::Parameter ||
          parameterLocal.id.ordinal() != receiverLocal.id.ordinal() + i) {
        return zc::none;
      }
      auto carrier = integerCarrierFor(parameterLocal.type, semanticTypes);
      if (carrier == zc::none) { return zc::none; }
      ordinaryCarriers.add(ZC_REQUIRE_NONNULL(carrier));
    }
  }
  if (calleeBlock.statements.size() >
      (calleeArithmetic || calleeFieldArithmetic || calleeConstLocalFold ? 2 : 1)) {
    return zc::none;
  }
  const auto& calleeReturn = calleeBlock.terminator.returnValue().value;
  if (calleeReturn == zc::none) { return zc::none; }
  zc::Maybe<IntegerConstant> calleeConstant;
  bool calleeFieldRead = false;
  bool calleeFieldWriteRead = false;
  zc::Maybe<IntegerConstant> calleeWriteConstant;
  zc::Maybe<uint32_t> calleeReturnParameterOrdinal;
  if (calleeArithmetic) {
    // The arithmetic shape is validated structurally above; the return place is
    // the FunctionResult local and no further return classification applies.
  } else if (calleeFieldArithmetic) {
    // The field-arithmetic shape is validated structurally above; the return
    // place is its FunctionResult local.
  } else ZC_IF_SOME(value, calleeReturn) {
    if (value.kind() == mir::MirOperandKind::Constant) {
      // The literal-method body carries no ordinary parameters.
      if (callee.locals.size() != 1 || calleeBlock.statements.size() != 0) { return zc::none; }
      if (value.constantValue().type != callee.resultType) { return zc::none; }
      const auto integer = value.constantValue().value.integerValue();
      if (integer == zc::none) { return zc::none; }
      auto bits = zeroExtendedBits(ZC_REQUIRE_NONNULL(integer), calleeCarrierValue.integerWidth());
      if (bits == zc::none) { return zc::none; }
      calleeConstant = IntegerConstant::from(calleeCarrierValue, ZC_REQUIRE_NONNULL(bits));
    } else {
      const auto& place = value.place();
      bool projectedReceiverPlace =
          place.projections().size() == 2 && place.local() == receiverLocal.id &&
          place.rootType() == receiverLocal.type && place.resultType() == callee.resultType &&
          place.projections()[0].kind() == mir::MirProjectionKind::Dereference &&
          place.projections()[0].inputType() == receiverLocal.type &&
          place.projections()[1].kind() == mir::MirProjectionKind::Field &&
          place.projections()[1].resultType() == callee.resultType;
      if (projectedReceiverPlace && calleeBlock.statements.size() == 1) {
        // Case D: `this.field = <constant>; return this.field;` on a mutable
        // receiver. The single statement Overwrites the same projected place
        // with a constant use, and the return reads that place back.
        if (callee.locals.size() != 1 || !calleeIsMutable) { return zc::none; }
        const auto& write = calleeBlock.statements[0];
        if (write.kind() != mir::MirStatementKind::Assign) { return zc::none; }
        const auto& assignment = write.assignmentValue();
        const auto& destination = assignment.destination;
        if (assignment.initialization != mir::MirInitializationKind::Overwrite ||
            assignment.value.kind() != mir::MirRvalueKind::Use ||
            assignment.value.useValue().operand.kind() != mir::MirOperandKind::Constant ||
            destination.local() != place.local() || destination.rootType() != place.rootType() ||
            destination.resultType() != place.resultType() ||
            destination.projections().size() != place.projections().size()) {
          return zc::none;
        }
        for (size_t p = 0; p < destination.projections().size(); ++p) {
          const auto& wantedProjection = place.projections()[p];
          const auto& actualProjection = destination.projections()[p];
          if (actualProjection.kind() != wantedProjection.kind() ||
              actualProjection.inputType() != wantedProjection.inputType() ||
              actualProjection.resultType() != wantedProjection.resultType()) {
            return zc::none;
          }
          if (actualProjection.kind() == mir::MirProjectionKind::Field &&
              actualProjection.fieldValue().field != wantedProjection.fieldValue().field) {
            return zc::none;
          }
        }
        const auto& writeOperand = assignment.value.useValue().operand;
        if (writeOperand.constantValue().type != callee.resultType) { return zc::none; }
        const auto writeInteger = writeOperand.constantValue().value.integerValue();
        if (writeInteger == zc::none) { return zc::none; }
        auto writeBits =
            zeroExtendedBits(ZC_REQUIRE_NONNULL(writeInteger), calleeCarrierValue.integerWidth());
        if (writeBits == zc::none) { return zc::none; }
        calleeWriteConstant =
            IntegerConstant::from(calleeCarrierValue, ZC_REQUIRE_NONNULL(writeBits));
        calleeFieldWriteRead = true;
      } else if (projectedReceiverPlace && calleeBlock.statements.size() == 0) {
        // Case B carries no ordinary parameters: the synthesized result slot
        // ordinal would otherwise collide with the first parameter ordinal.
        if (callee.locals.size() != 1) { return zc::none; }
        calleeFieldRead = true;
      } else if (calleeConstLocalFold && place.projections().size() == 0 &&
                 calleeBlock.statements.size() == 2) {
        // Case E: `let x = <constant>; return x;` on a receiver method. The
        // single user local is StorageLive'd then Initialize-assigned the
        // returned constant; the body folds to a constant return and the user
        // local never becomes an LIR slot.
        const auto& userLocal = callee.locals[1];
        const auto& live = calleeBlock.statements[0];
        const auto& initialize = calleeBlock.statements[1];
        if (live.kind() != mir::MirStatementKind::StorageLive ||
            live.storageLocal() != userLocal.id ||
            initialize.kind() != mir::MirStatementKind::Assign ||
            initialize.assignmentValue().initialization != mir::MirInitializationKind::Initialize ||
            initialize.assignmentValue().destination.local() != userLocal.id ||
            initialize.assignmentValue().destination.projections().size() != 0 ||
            initialize.assignmentValue().value.kind() != mir::MirRvalueKind::Use ||
            initialize.assignmentValue().value.useValue().operand.kind() !=
                mir::MirOperandKind::Constant ||
            place.local() != userLocal.id || place.rootType() != userLocal.type ||
            place.resultType() != callee.resultType) {
          return zc::none;
        }
        const auto& foldedOperand =
            initialize.assignmentValue().value.useValue().operand.constantValue();
        if (foldedOperand.type != callee.resultType) { return zc::none; }
        const auto foldedInteger = foldedOperand.value.integerValue();
        if (foldedInteger == zc::none) { return zc::none; }
        auto foldedBits =
            zeroExtendedBits(ZC_REQUIRE_NONNULL(foldedInteger), calleeCarrierValue.integerWidth());
        if (foldedBits == zc::none) { return zc::none; }
        calleeConstant = IntegerConstant::from(calleeCarrierValue, ZC_REQUIRE_NONNULL(foldedBits));
      } else if (place.projections().size() == 0 && callee.locals.size() == 2) {
        // Case C: the single ordinary parameter at ordinal receiver + 1.
        const auto& parameterLocal = callee.locals[1];
        if (parameterLocal.kind != mir::MirLocalKind::Parameter ||
            place.local() != parameterLocal.id || place.rootType() != parameterLocal.type ||
            place.resultType() != callee.resultType) {
          return zc::none;
        }
        calleeReturnParameterOrdinal = parameterLocal.id.ordinal();
      } else {
        return zc::none;
      }
    }
  }
  if (!calleeArithmetic && !calleeFieldArithmetic && calleeConstant == zc::none &&
      !calleeFieldRead && !calleeFieldWriteRead && calleeReturnParameterOrdinal == zc::none) {
    return zc::none;
  }

  // Caller: one aggregate-initialized owner local, one receiver borrow
  // temporary whose mutability matches the callee receiver, and one call result
  // temporary; two blocks (Call then Return).
  if (caller.kind != mir::MirFunctionKind::Function ||
      caller.sourceDefinitionKind != identity::DefinitionKind::Function ||
      caller.locals.size() != 3 || caller.blocks.size() != 2 ||
      caller.resultType != callee.resultType) {
    return zc::none;
  }
  const auto& ownerLocal = caller.locals[0];
  const auto& borrowTemporary = caller.locals[1];
  const auto& resultTemporary = caller.locals[2];
  if (ownerLocal.kind != mir::MirLocalKind::UserLocal ||
      borrowTemporary.kind != mir::MirLocalKind::Temporary ||
      resultTemporary.kind != mir::MirLocalKind::Temporary) {
    return zc::none;
  }
  auto callerBorrowMutable = receiverReferenceIsMutable(borrowTemporary.type, semanticTypes);
  if (callerBorrowMutable == zc::none) { return zc::none; }
  const bool callerIsMutable = ZC_ASSERT_NONNULL(callerBorrowMutable);
  // The caller's borrow mode must agree with the callee's receiver mode; a
  // shared call into a mutable receiver or vice versa is an invalid lowering.
  if (callerIsMutable != calleeIsMutable) { return zc::none; }
  auto callerCarrier = integerCarrierFor(caller.resultType, semanticTypes);
  if (callerCarrier == zc::none) { return zc::none; }
  const auto callerCarrierValue = ZC_REQUIRE_NONNULL(callerCarrier);

  const auto& entry = caller.blocks[0];
  const auto& continuation = caller.blocks[1];
  if (entry.statements.size() != 5 || entry.terminator.kind() != mir::MirTerminatorKind::Call ||
      continuation.statements.size() != 0 ||
      continuation.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  if (entry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != ownerLocal.id ||
      entry.statements[1].kind() != mir::MirStatementKind::Assign ||
      entry.statements[2].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[2].storageLocal() != borrowTemporary.id ||
      entry.statements[3].kind() != mir::MirStatementKind::BorrowCreation ||
      entry.statements[4].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[4].storageLocal() != resultTemporary.id) {
    return zc::none;
  }
  // The owner initializer is a one-element nominal aggregate of a scalar
  // constant; its value folds into the owner slot, so no struct is materialized.
  const auto& initialization = entry.statements[1].assignmentValue();
  if (initialization.initialization != mir::MirInitializationKind::Initialize ||
      initialization.destination.local() != ownerLocal.id ||
      initialization.destination.projections().size() != 0 ||
      initialization.value.kind() != mir::MirRvalueKind::NominalAggregate) {
    return zc::none;
  }
  const auto& aggregate = initialization.value.nominalAggregateValue();
  if (aggregate.type != ownerLocal.type || aggregate.elements.size() != 1) { return zc::none; }
  const auto& elementOperand = aggregate.elements[0].operand;
  if (elementOperand.kind() != mir::MirOperandKind::Constant) { return zc::none; }
  // The one-field owner struct folds to the scalar carrier of its element.
  auto ownerCarrier = integerCarrierFor(elementOperand.constantValue().type, semanticTypes);
  if (ownerCarrier == zc::none) { return zc::none; }
  const auto ownerCarrierValue = ZC_REQUIRE_NONNULL(ownerCarrier);
  auto ownerConstant = lirOperandFor(elementOperand, ownerCarrierValue);
  if (ownerConstant == zc::none) { return zc::none; }

  const auto& borrow = entry.statements[3].borrowCreationValue();
  const auto expectedBorrowKind =
      callerIsMutable ? mir::MirBorrowKind::Mutable : mir::MirBorrowKind::Shared;
  if (borrow.kind != expectedBorrowKind || borrow.destination.local() != borrowTemporary.id ||
      borrow.destination.projections().size() != 0 || borrow.source.local() != ownerLocal.id ||
      borrow.source.projections().size() != 0) {
    return zc::none;
  }

  const auto& mirCall = entry.terminator.callValue();
  const size_t ordinaryParameterCount = ordinaryCarriers.size();
  const auto expectedEffectKind = callerIsMutable ? mir::MirCallEffectKind::ActivateMutableReceiver
                                                  : mir::MirCallEffectKind::NoActivation;
  if (mirCall.callee != callee.owner || mirCall.arguments.size() != 1 + ordinaryParameterCount ||
      mirCall.effect.kind() != expectedEffectKind ||
      mirCall.destination.local() != resultTemporary.id ||
      mirCall.destination.projections().size() != 0 || mirCall.normalTarget != continuation.id ||
      mirCall.unwindTarget != zc::none) {
    return zc::none;
  }
  if (callerIsMutable) {
    // The activation targets the receiver borrow temporary.
    auto activated = mirCall.effect.activatedMutableReceiver();
    if (activated == zc::none || ZC_ASSERT_NONNULL(activated) != borrowTemporary.id) {
      return zc::none;
    }
  }
  const auto& receiverArgument = mirCall.arguments[0];
  if (receiverArgument.kind() == mir::MirOperandKind::Constant ||
      receiverArgument.place().local() != borrowTemporary.id ||
      receiverArgument.place().projections().size() != 0) {
    return zc::none;
  }
  // Every trailing call argument is an integer constant matching the callee's
  // ordinary parameter carrier; place arguments ride a later slice.
  zc::Vector<Operand> extraArguments;
  for (size_t i = 0; i < ordinaryParameterCount; ++i) {
    const auto& argument = mirCall.arguments[1 + i];
    if (argument.kind() != mir::MirOperandKind::Constant ||
        argument.constantValue().type != callee.locals[1 + i].type) {
      return zc::none;
    }
    auto argumentOperand = lirOperandFor(argument, ordinaryCarriers[i]);
    if (argumentOperand == zc::none) { return zc::none; }
    extraArguments.add(ZC_REQUIRE_NONNULL(argumentOperand));
  }
  const auto& returnValue = continuation.terminator.returnValue().value;
  if (returnValue == zc::none) { return zc::none; }
  ZC_IF_SOME(value, returnValue) {
    if (value.kind() == mir::MirOperandKind::Constant ||
        value.place().local() != resultTemporary.id || value.place().projections().size() != 0) {
      return zc::none;
    }
  }

  auto callerEntryId = LirBlockId::fromOrdinal(1);
  auto callerContId = LirBlockId::fromOrdinal(2);
  auto calleeEntryId = LirBlockId::fromOrdinal(1);
  if (callerEntryId == zc::none || callerContId == zc::none || calleeEntryId == zc::none) {
    return zc::none;
  }

  zc::Vector<Function> functions;
  {
    zc::Vector<Statement> entryStatements;
    entryStatements.add(
        Statement::assign(ownerLocal.id.ordinal(), ZC_ASSERT_NONNULL(ownerConstant)));
    entryStatements.add(
        Statement::takeAddress(borrowTemporary.id.ordinal(), ownerLocal.id.ordinal()));
    zc::Vector<Operand> arguments;
    arguments.add(Operand::localUse(borrowTemporary.id.ordinal()));
    for (auto& extraArgument : extraArguments) { arguments.add(zc::mv(extraArgument)); }
    auto callTerminator = Terminator::callFunction(
        /*calleeIndex=*/1, resultTemporary.id.ordinal(), zc::mv(arguments),
        ZC_REQUIRE_NONNULL(callerContId));
    if (callTerminator == zc::none) { return zc::none; }
    zc::Vector<BasicBlock> callerBlocks;
    callerBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(callerEntryId), zc::mv(entryStatements),
                                ZC_REQUIRE_NONNULL(zc::mv(callTerminator))));
    callerBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(callerContId),
                                Terminator::returnLocal(resultTemporary.id.ordinal())));
    zc::Vector<Local> noParameters;
    zc::Vector<Local> locals;
    locals.add(Local(ownerLocal.id.ordinal(), ownerCarrierValue));
    locals.add(Local(borrowTemporary.id.ordinal(), receiverCarrierValue));
    locals.add(Local(resultTemporary.id.ordinal(), callerCarrierValue));
    // The zero-parameter entry folds to the reserved module-initializer symbol
    // the runtime `_start` invokes, so this receiver-call slice is runnable.
    functions.add(Function(caller.owner, zc::heapString("zom.module_init"), callerCarrierValue,
                           zc::mv(noParameters), zc::mv(locals), zc::mv(callerBlocks)));
  }
  {
    zc::Vector<Local> parameters;
    parameters.add(Local(receiverLocal.id.ordinal(), receiverCarrierValue));
    for (size_t i = 0; i < ordinaryCarriers.size(); ++i) {
      parameters.add(Local(receiverLocal.id.ordinal() + 1 + i, ordinaryCarriers[i]));
    }
    if (calleeFieldRead) {
      // The field read materializes in a synthesized result slot (the first body
      // local, ordinal receiver + 1): LoadField through the receiver pointer at
      // offset zero, then ReturnLocal the loaded field.
      const uint32_t resultOrdinal = receiverLocal.id.ordinal() + 1;
      zc::Vector<Statement> calleeStatements;
      calleeStatements.add(
          Statement::loadField(resultOrdinal, receiverLocal.id.ordinal(), /*fieldOffsetBytes=*/0));
      zc::Vector<BasicBlock> calleeBlocks;
      calleeBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(calleeEntryId), zc::mv(calleeStatements),
                                  Terminator::returnLocal(resultOrdinal)));
      zc::Vector<Local> calleeLocals;
      calleeLocals.add(Local(resultOrdinal, calleeCarrierValue));
      functions.add(Function(callee.owner, zc::heapString("zom.callee"), calleeCarrierValue,
                             zc::mv(parameters), zc::mv(calleeLocals), zc::mv(calleeBlocks)));
    } else if (calleeFieldWriteRead) {
      // Case D: StoreField the write constant through the mutable receiver
      // pointer at offset zero, then LoadField the same field into the
      // synthesized result slot (ordinal receiver + 1) and return it.
      const uint32_t resultOrdinal = receiverLocal.id.ordinal() + 1;
      zc::Vector<Statement> calleeStatements;
      calleeStatements.add(Statement::storeField(
          receiverLocal.id.ordinal(), Operand::constant(ZC_REQUIRE_NONNULL(calleeWriteConstant)),
          /*fieldOffsetBytes=*/0));
      calleeStatements.add(
          Statement::loadField(resultOrdinal, receiverLocal.id.ordinal(), /*fieldOffsetBytes=*/0));
      zc::Vector<BasicBlock> calleeBlocks;
      calleeBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(calleeEntryId), zc::mv(calleeStatements),
                                  Terminator::returnLocal(resultOrdinal)));
      zc::Vector<Local> calleeLocals;
      calleeLocals.add(Local(resultOrdinal, calleeCarrierValue));
      functions.add(Function(callee.owner, zc::heapString("zom.callee"), calleeCarrierValue,
                             zc::mv(parameters), zc::mv(calleeLocals), zc::mv(calleeBlocks)));
    } else if (calleeFieldArithmetic) {
      // Case G: LoadField the receiver field into the synthesized field slot,
      // combine it with the constant operand in an arithmetic statement into the
      // result slot, then return the result.
      zc::Vector<Statement> calleeStatements;
      calleeStatements.add(Statement::loadField(calleeFieldArithmeticFieldOrdinal,
                                                receiverLocal.id.ordinal(),
                                                /*fieldOffsetBytes=*/0));
      calleeStatements.add(Statement::arithmetic(calleeFieldArithmeticResultOrdinal,
                                                 ZC_REQUIRE_NONNULL(calleeFieldArithmeticOp),
                                                 ZC_REQUIRE_NONNULL(calleeFieldArithmeticLeft),
                                                 ZC_REQUIRE_NONNULL(calleeFieldArithmeticRight)));
      zc::Vector<BasicBlock> calleeBlocks;
      calleeBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(calleeEntryId), zc::mv(calleeStatements),
                                  Terminator::returnLocal(calleeFieldArithmeticResultOrdinal)));
      zc::Vector<Local> calleeLocals;
      calleeLocals.add(Local(calleeFieldArithmeticFieldOrdinal, calleeCarrierValue));
      calleeLocals.add(Local(calleeFieldArithmeticResultOrdinal, calleeCarrierValue));
      functions.add(Function(callee.owner, zc::heapString("zom.callee"), calleeCarrierValue,
                             zc::mv(parameters), zc::mv(calleeLocals), zc::mv(calleeBlocks)));
    } else if (calleeArithmetic) {
      // Case F: compute the arithmetic result over the ordinary parameter and a
      // literal/parameter operand, then return the result local.
      zc::Vector<Statement> calleeStatements;
      calleeStatements.add(Statement::arithmetic(
          calleeArithmeticResultOrdinal, ZC_REQUIRE_NONNULL(calleeArithmeticOp),
          ZC_REQUIRE_NONNULL(calleeArithmeticLeft), ZC_REQUIRE_NONNULL(calleeArithmeticRight)));
      zc::Vector<BasicBlock> calleeBlocks;
      calleeBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(calleeEntryId), zc::mv(calleeStatements),
                                  Terminator::returnLocal(calleeArithmeticResultOrdinal)));
      zc::Vector<Local> calleeLocals;
      calleeLocals.add(Local(calleeArithmeticResultOrdinal, calleeCarrierValue));
      functions.add(Function(callee.owner, zc::heapString("zom.callee"), calleeCarrierValue,
                             zc::mv(parameters), zc::mv(calleeLocals), zc::mv(calleeBlocks)));
    } else if (calleeConstant != zc::none) {
      zc::Vector<BasicBlock> calleeBlocks;
      calleeBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(calleeEntryId),
                                  Terminator::returnInteger(ZC_REQUIRE_NONNULL(calleeConstant))));
      zc::Vector<Local> noLocals;
      functions.add(Function(callee.owner, zc::heapString("zom.callee"), calleeCarrierValue,
                             zc::mv(parameters), zc::mv(noLocals), zc::mv(calleeBlocks)));
    } else {
      // The method returns one of its ordinary parameters: the incoming
      // argument slot is returned directly with no body local.
      zc::Vector<BasicBlock> calleeBlocks;
      calleeBlocks.add(
          BasicBlock(ZC_REQUIRE_NONNULL(calleeEntryId),
                     Terminator::returnLocal(ZC_REQUIRE_NONNULL(calleeReturnParameterOrdinal))));
      zc::Vector<Local> noLocals;
      functions.add(Function(callee.owner, zc::heapString("zom.callee"), calleeCarrierValue,
                             zc::mv(parameters), zc::mv(noLocals), zc::mv(calleeBlocks)));
    }
  }

  return Module(zc::mv(functions));
}

zc::Maybe<Module> MirToLirLowering::lowerReceiverConditionalCallModule(
    const mir::MirFunction& caller, const mir::MirFunction& callee,
    const type::SemanticTypeStore& semanticTypes) {
  // Callee: a shared-receiver method with one bool ordinary parameter and a
  // four-block literal-arm conditional. Locals are the receiver pointer at
  // ordinal 1, the bool parameter at ordinal 2, and the FunctionResult at
  // ordinal 3.
  if (callee.kind != mir::MirFunctionKind::Function ||
      callee.sourceDefinitionKind != identity::DefinitionKind::Method) {
    return zc::none;
  }
  const auto& receiverLocal = callee.locals[0];
  const auto& conditionLocal = callee.locals[1];
  const auto& resultLocal = callee.locals[2];
  if (receiverLocal.kind != mir::MirLocalKind::Parameter ||
      conditionLocal.kind != mir::MirLocalKind::Parameter ||
      resultLocal.kind != mir::MirLocalKind::FunctionResult) {
    return zc::none;
  }
  auto calleeReceiverMutable = receiverReferenceIsMutable(receiverLocal.type, semanticTypes);
  if (calleeReceiverMutable == zc::none || ZC_ASSERT_NONNULL(calleeReceiverMutable)) {
    return zc::none;
  }
  auto receiverCarrier = receiverPointerCarrier(receiverLocal.type, semanticTypes);
  const auto receiverCarrierValue = ZC_REQUIRE_NONNULL(receiverCarrier);
  auto conditionCarrier = boolCarrierFor(conditionLocal.type, semanticTypes);
  const auto conditionCarrierValue = ZC_REQUIRE_NONNULL(conditionCarrier);
  auto calleeCarrier = integerCarrierFor(callee.resultType, semanticTypes);
  const auto calleeCarrierValue = ZC_REQUIRE_NONNULL(calleeCarrier);

  const auto& entry = callee.blocks[0];
  const auto& thenBlock = callee.blocks[1];
  const auto& elseBlock = callee.blocks[2];
  const auto& joinBlock = callee.blocks[3];
  if (entry.statements.size() != 1 ||
      entry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != resultLocal.id ||
      entry.terminator.kind() != mir::MirTerminatorKind::SwitchInt ||
      thenBlock.statements.size() != 1 ||
      thenBlock.statements[0].kind() != mir::MirStatementKind::Assign ||
      thenBlock.terminator.kind() != mir::MirTerminatorKind::Goto ||
      elseBlock.statements.size() != 1 ||
      elseBlock.statements[0].kind() != mir::MirStatementKind::Assign ||
      elseBlock.terminator.kind() != mir::MirTerminatorKind::Goto ||
      joinBlock.statements.size() != 0 ||
      joinBlock.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& switchInt = entry.terminator.switchIntValue();
  if (switchInt.discriminant.kind() != mir::MirOperandKind::Copy ||
      switchInt.discriminant.place().local() != conditionLocal.id ||
      switchInt.discriminant.place().projections().size() != 0 || switchInt.arms.size() != 2 ||
      switchInt.defaultTarget != elseBlock.id) {
    return zc::none;
  }
  auto trueLiteral = switchInt.arms[0].value.booleanValue();
  auto falseLiteral = switchInt.arms[1].value.booleanValue();
  if (trueLiteral == zc::none || falseLiteral == zc::none || !ZC_ASSERT_NONNULL(trueLiteral) ||
      ZC_ASSERT_NONNULL(falseLiteral) || switchInt.arms[0].target != thenBlock.id ||
      switchInt.arms[1].target != elseBlock.id) {
    return zc::none;
  }
  if (thenBlock.terminator.gotoValue().target != joinBlock.id ||
      elseBlock.terminator.gotoValue().target != joinBlock.id) {
    return zc::none;
  }
  auto armConstant = [&](const mir::MirBasicBlock& branch) -> zc::Maybe<IntegerConstant> {
    const auto& assignment = branch.statements[0].assignmentValue();
    if (assignment.initialization != mir::MirInitializationKind::Initialize ||
        assignment.destination.local() != resultLocal.id ||
        assignment.destination.projections().size() != 0 ||
        assignment.value.kind() != mir::MirRvalueKind::Use ||
        assignment.value.useValue().operand.kind() != mir::MirOperandKind::Constant) {
      return zc::none;
    }
    const auto& constantOperand = assignment.value.useValue().operand.constantValue();
    const auto integer = constantOperand.value.integerValue();
    if (integer == zc::none) { return zc::none; }
    auto bits = zeroExtendedBits(ZC_REQUIRE_NONNULL(integer), calleeCarrierValue.integerWidth());
    if (bits == zc::none) { return zc::none; }
    return IntegerConstant::from(calleeCarrierValue, ZC_REQUIRE_NONNULL(bits));
  };
  auto thenConstant = armConstant(thenBlock);
  auto elseConstant = armConstant(elseBlock);
  const auto& joinReturn = joinBlock.terminator.returnValue().value;
  if (thenConstant == zc::none || elseConstant == zc::none || joinReturn == zc::none) {
    return zc::none;
  }
  ZC_IF_SOME(value, joinReturn) {
    if (value.kind() == mir::MirOperandKind::Constant || value.place().local() != resultLocal.id ||
        value.place().projections().size() != 0) {
      return zc::none;
    }
  }

  // Caller: one aggregate-initialized owner local, one shared-receiver borrow
  // temporary, and one result temporary, with one constant bool argument.
  // A comparison-argument variant adds one bool temporary and two statements
  // (StorageLive + Comparison assign) for a field comparison on the owner.
  const bool hasComparisonArgument = (caller.locals.size() == 4);
  if (caller.kind != mir::MirFunctionKind::Function ||
      caller.sourceDefinitionKind != identity::DefinitionKind::Function ||
      (caller.locals.size() != 3 && caller.locals.size() != 4) || caller.blocks.size() != 2 ||
      caller.resultType != callee.resultType) {
    return zc::none;
  }
  const auto& ownerLocal = caller.locals[0];
  const auto& borrowTemporary = caller.locals[1];
  const auto& resultTemporary = hasComparisonArgument ? caller.locals[3] : caller.locals[2];
  if (ownerLocal.kind != mir::MirLocalKind::UserLocal ||
      borrowTemporary.kind != mir::MirLocalKind::Temporary ||
      resultTemporary.kind != mir::MirLocalKind::Temporary) {
    return zc::none;
  }
  const mir::MirLocalDeclaration* comparisonTemporary = nullptr;
  if (hasComparisonArgument) {
    comparisonTemporary = &caller.locals[2];
    if (comparisonTemporary->kind != mir::MirLocalKind::Temporary) { return zc::none; }
  }
  auto callerBorrowMutable = receiverReferenceIsMutable(borrowTemporary.type, semanticTypes);
  if (callerBorrowMutable == zc::none || ZC_ASSERT_NONNULL(callerBorrowMutable)) {
    return zc::none;
  }
  auto callerCarrier = integerCarrierFor(caller.resultType, semanticTypes);
  const auto callerCarrierValue = ZC_REQUIRE_NONNULL(callerCarrier);

  const auto& callerEntry = caller.blocks[0];
  const auto& callerContinuation = caller.blocks[1];
  const size_t expectedStatements = hasComparisonArgument ? 7 : 5;
  if (callerEntry.statements.size() != expectedStatements ||
      callerEntry.terminator.kind() != mir::MirTerminatorKind::Call ||
      callerContinuation.statements.size() != 0 ||
      callerContinuation.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  if (callerEntry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      callerEntry.statements[0].storageLocal() != ownerLocal.id ||
      callerEntry.statements[1].kind() != mir::MirStatementKind::Assign ||
      callerEntry.statements[2].kind() != mir::MirStatementKind::StorageLive ||
      callerEntry.statements[2].storageLocal() != borrowTemporary.id ||
      callerEntry.statements[3].kind() != mir::MirStatementKind::BorrowCreation) {
    return zc::none;
  }
  if (hasComparisonArgument) {
    if (callerEntry.statements[4].kind() != mir::MirStatementKind::StorageLive ||
        callerEntry.statements[4].storageLocal() != comparisonTemporary->id ||
        callerEntry.statements[5].kind() != mir::MirStatementKind::Assign ||
        callerEntry.statements[6].kind() != mir::MirStatementKind::StorageLive ||
        callerEntry.statements[6].storageLocal() != resultTemporary.id) {
      return zc::none;
    }
  } else {
    if (callerEntry.statements[4].kind() != mir::MirStatementKind::StorageLive ||
        callerEntry.statements[4].storageLocal() != resultTemporary.id) {
      return zc::none;
    }
  }
  const auto& initialization = callerEntry.statements[1].assignmentValue();
  if (initialization.initialization != mir::MirInitializationKind::Initialize ||
      initialization.destination.local() != ownerLocal.id ||
      initialization.destination.projections().size() != 0 ||
      initialization.value.kind() != mir::MirRvalueKind::NominalAggregate) {
    return zc::none;
  }
  const auto& aggregate = initialization.value.nominalAggregateValue();
  if (aggregate.type != ownerLocal.type || aggregate.elements.size() != 1) { return zc::none; }
  const auto& elementOperand = aggregate.elements[0].operand;
  if (elementOperand.kind() != mir::MirOperandKind::Constant) { return zc::none; }
  auto ownerCarrier = integerCarrierFor(elementOperand.constantValue().type, semanticTypes);
  if (ownerCarrier == zc::none) { return zc::none; }
  const auto ownerCarrierValue = ZC_REQUIRE_NONNULL(ownerCarrier);
  auto ownerConstant = lirOperandFor(elementOperand, ownerCarrierValue);
  if (ownerConstant == zc::none) { return zc::none; }

  const auto& borrow = callerEntry.statements[3].borrowCreationValue();
  if (borrow.kind != mir::MirBorrowKind::Shared ||
      borrow.destination.local() != borrowTemporary.id ||
      borrow.destination.projections().size() != 0 || borrow.source.local() != ownerLocal.id ||
      borrow.source.projections().size() != 0) {
    return zc::none;
  }

  // For the comparison-argument shape, validate the field-comparison
  // assignment: the destination is the bool temporary, the left operand is a
  // field projection on the owner, and the right is a constant.
  zc::Maybe<ComparisonOp> comparisonOp;
  zc::Maybe<Operand> comparisonLiteralOperand;
  if (hasComparisonArgument) {
    const auto& comparisonAssign = callerEntry.statements[5].assignmentValue();
    if (comparisonAssign.initialization != mir::MirInitializationKind::Initialize ||
        comparisonAssign.destination.local() != comparisonTemporary->id ||
        comparisonAssign.destination.projections().size() != 0 ||
        comparisonAssign.value.kind() != mir::MirRvalueKind::Comparison) {
      return zc::none;
    }
    const auto& comparison = comparisonAssign.value.comparisonValue();
    comparisonOp = lirComparisonOpFor(comparison.op);
    if (comparisonOp == zc::none) { return zc::none; }
    if (comparison.left.kind() == mir::MirOperandKind::Constant ||
        comparison.left.place().local() != ownerLocal.id ||
        comparison.left.place().projections().size() != 1) {
      return zc::none;
    }
    if (comparison.right.kind() != mir::MirOperandKind::Constant) { return zc::none; }
    // The literal operand shares the owner field's carrier (the aggregate's
    // single element type), not the comparison result type.
    comparisonLiteralOperand = lirOperandFor(comparison.right, ownerCarrierValue);
    if (comparisonLiteralOperand == zc::none) { return zc::none; }
  }

  const auto& mirCall = callerEntry.terminator.callValue();
  if (mirCall.callee != callee.owner || mirCall.arguments.size() != 2 ||
      mirCall.effect.kind() != mir::MirCallEffectKind::NoActivation ||
      mirCall.destination.local() != resultTemporary.id ||
      mirCall.destination.projections().size() != 0 ||
      mirCall.normalTarget != callerContinuation.id || mirCall.unwindTarget != zc::none) {
    return zc::none;
  }
  const auto& receiverArgument = mirCall.arguments[0];
  if (receiverArgument.kind() == mir::MirOperandKind::Constant ||
      receiverArgument.place().local() != borrowTemporary.id ||
      receiverArgument.place().projections().size() != 0) {
    return zc::none;
  }
  const auto& boolArgument = mirCall.arguments[1];
  zc::Maybe<Operand> boolArgumentOperand;
  if (hasComparisonArgument) {
    // The bool argument is a place-use of the comparison temporary.
    if (boolArgument.kind() == mir::MirOperandKind::Constant ||
        boolArgument.place().local() != comparisonTemporary->id ||
        boolArgument.place().projections().size() != 0) {
      return zc::none;
    }
    boolArgumentOperand = Operand::localUse(comparisonTemporary->id.ordinal());
  } else {
    // A bool argument carries an i1 constant distinct from the integer path.
    if (boolArgument.kind() != mir::MirOperandKind::Constant ||
        boolArgument.constantValue().type != conditionLocal.type) {
      return zc::none;
    }
    const auto boolConstant = boolArgument.constantValue().value.booleanValue();
    if (boolConstant == zc::none) { return zc::none; }
    auto boolIntegerConstant =
        IntegerConstant::from(conditionCarrierValue, ZC_ASSERT_NONNULL(boolConstant) ? 1 : 0);
    if (boolIntegerConstant == zc::none) { return zc::none; }
    boolArgumentOperand = Operand::constant(ZC_ASSERT_NONNULL(boolIntegerConstant));
  }
  const auto& callerReturn = callerContinuation.terminator.returnValue().value;
  ZC_IF_SOME(value, callerReturn) {
    if (value.kind() == mir::MirOperandKind::Constant ||
        value.place().local() != resultTemporary.id || value.place().projections().size() != 0) {
      return zc::none;
    }
  }

  auto callerEntryId = LirBlockId::fromOrdinal(1);
  auto callerContId = LirBlockId::fromOrdinal(2);
  auto calleeEntryId = LirBlockId::fromOrdinal(1);
  auto calleeThenId = LirBlockId::fromOrdinal(2);
  auto calleeElseId = LirBlockId::fromOrdinal(3);
  auto calleeJoinId = LirBlockId::fromOrdinal(4);
  if (callerEntryId == zc::none || callerContId == zc::none || calleeEntryId == zc::none ||
      calleeThenId == zc::none || calleeElseId == zc::none || calleeJoinId == zc::none) {
    return zc::none;
  }

  zc::Vector<Function> functions;
  {
    zc::Vector<Statement> entryStatements;
    entryStatements.add(
        Statement::assign(ownerLocal.id.ordinal(), ZC_ASSERT_NONNULL(ownerConstant)));
    entryStatements.add(
        Statement::takeAddress(borrowTemporary.id.ordinal(), ownerLocal.id.ordinal()));
    if (hasComparisonArgument) {
      // The owner local in LIR is a scalar holding the single field value
      // directly, so the field comparison is Compare(owner, literal).
      entryStatements.add(Statement::compare(comparisonTemporary->id.ordinal(),
                                             ZC_REQUIRE_NONNULL(comparisonOp),
                                             Operand::localUse(ownerLocal.id.ordinal()),
                                             ZC_REQUIRE_NONNULL(comparisonLiteralOperand)));
    }
    zc::Vector<Operand> arguments;
    arguments.add(Operand::localUse(borrowTemporary.id.ordinal()));
    arguments.add(ZC_REQUIRE_NONNULL(boolArgumentOperand));
    auto callTerminator = Terminator::callFunction(
        /*calleeIndex=*/1, resultTemporary.id.ordinal(), zc::mv(arguments),
        ZC_REQUIRE_NONNULL(callerContId));
    if (callTerminator == zc::none) { return zc::none; }
    zc::Vector<BasicBlock> callerBlocks;
    callerBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(callerEntryId), zc::mv(entryStatements),
                                ZC_REQUIRE_NONNULL(zc::mv(callTerminator))));
    callerBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(callerContId),
                                Terminator::returnLocal(resultTemporary.id.ordinal())));
    zc::Vector<Local> noParameters;
    zc::Vector<Local> locals;
    locals.add(Local(ownerLocal.id.ordinal(), ownerCarrierValue));
    locals.add(Local(borrowTemporary.id.ordinal(), receiverCarrierValue));
    if (hasComparisonArgument) {
      locals.add(Local(comparisonTemporary->id.ordinal(), conditionCarrierValue));
    }
    locals.add(Local(resultTemporary.id.ordinal(), callerCarrierValue));
    functions.add(Function(caller.owner, zc::heapString("zom.module_init"), callerCarrierValue,
                           zc::mv(noParameters), zc::mv(locals), zc::mv(callerBlocks)));
  }
  {
    zc::Vector<Local> parameters;
    parameters.add(Local(receiverLocal.id.ordinal(), receiverCarrierValue));
    parameters.add(Local(conditionLocal.id.ordinal(), conditionCarrierValue));
    zc::Vector<Local> locals;
    locals.add(Local(resultLocal.id.ordinal(), calleeCarrierValue));
    zc::Vector<BasicBlock> calleeBlocks;
    calleeBlocks.add(BasicBlock(
        ZC_REQUIRE_NONNULL(calleeEntryId),
        Terminator::condBranch(conditionLocal.id.ordinal(), ZC_REQUIRE_NONNULL(calleeThenId),
                               ZC_REQUIRE_NONNULL(calleeElseId))));
    zc::Vector<Statement> thenStatements;
    thenStatements.add(Statement::assign(resultLocal.id.ordinal(),
                                         Operand::constant(ZC_ASSERT_NONNULL(thenConstant))));
    calleeBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(calleeThenId), zc::mv(thenStatements),
                                Terminator::gotoBlock(ZC_REQUIRE_NONNULL(calleeJoinId))));
    zc::Vector<Statement> elseStatements;
    elseStatements.add(Statement::assign(resultLocal.id.ordinal(),
                                         Operand::constant(ZC_ASSERT_NONNULL(elseConstant))));
    calleeBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(calleeElseId), zc::mv(elseStatements),
                                Terminator::gotoBlock(ZC_REQUIRE_NONNULL(calleeJoinId))));
    zc::Vector<Statement> joinStatements;
    calleeBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(calleeJoinId), zc::mv(joinStatements),
                                Terminator::returnLocal(resultLocal.id.ordinal())));
    functions.add(Function(callee.owner, zc::heapString("zom.callee"), calleeCarrierValue,
                           zc::mv(parameters), zc::mv(locals), zc::mv(calleeBlocks)));
  }
  return Module(zc::mv(functions));
}

zc::Maybe<Module> MirToLirLowering::lowerReceiverSelfCallModule(
    const mir::MirFunction& caller, const mir::MirFunction& forwarder, const mir::MirFunction& leaf,
    const type::SemanticTypeStore& semanticTypes) {
  // Forwarder: a shared-receiver Method with one leading receiver Parameter and
  // one result Temporary; two blocks (StorageLive(result) + Call forwarding the
  // receiver local, then Return of the result).
  if (forwarder.kind != mir::MirFunctionKind::Function ||
      forwarder.sourceDefinitionKind != identity::DefinitionKind::Method ||
      forwarder.locals.size() != 2 || forwarder.blocks.size() != 2 ||
      forwarder.resultType != leaf.resultType) {
    return zc::none;
  }
  const auto& forwarderReceiver = forwarder.locals[0];
  const auto& forwarderResult = forwarder.locals[1];
  if (forwarderReceiver.kind != mir::MirLocalKind::Parameter ||
      forwarderResult.kind != mir::MirLocalKind::Temporary) {
    return zc::none;
  }
  auto forwarderReceiverMutable = receiverReferenceIsMutable(forwarderReceiver.type, semanticTypes);
  if (forwarderReceiverMutable == zc::none || ZC_ASSERT_NONNULL(forwarderReceiverMutable)) {
    return zc::none;
  }
  auto pointerCarrier = receiverPointerCarrier(forwarderReceiver.type, semanticTypes);
  if (pointerCarrier == zc::none) { return zc::none; }
  const auto pointerCarrierValue = ZC_REQUIRE_NONNULL(pointerCarrier);
  auto forwarderCarrier = integerCarrierFor(forwarder.resultType, semanticTypes);
  if (forwarderCarrier == zc::none) { return zc::none; }
  const auto forwarderCarrierValue = ZC_REQUIRE_NONNULL(forwarderCarrier);
  const uint32_t forwarderResultOrdinal = forwarderResult.id.ordinal();
  const uint32_t forwarderReceiverOrdinal = forwarderReceiver.id.ordinal();

  const auto& forwarderEntry = forwarder.blocks[0];
  const auto& forwarderCont = forwarder.blocks[1];
  if (forwarderEntry.statements.size() != 1 ||
      forwarderEntry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      forwarderEntry.statements[0].storageLocal() != forwarderResult.id ||
      forwarderEntry.terminator.kind() != mir::MirTerminatorKind::Call) {
    return zc::none;
  }
  const auto& forwarderCall = forwarderEntry.terminator.callValue();
  if (forwarderCall.callee != leaf.owner || forwarderCall.arguments.size() != 1 ||
      forwarderCall.effect.kind() != mir::MirCallEffectKind::NoActivation ||
      forwarderCall.destination.local() != forwarderResult.id ||
      forwarderCall.destination.projections().size() != 0 ||
      forwarderCall.normalTarget != forwarderCont.id || forwarderCall.unwindTarget != zc::none) {
    return zc::none;
  }
  const auto& forwardedReceiver = forwarderCall.arguments[0];
  if (forwardedReceiver.kind() == mir::MirOperandKind::Constant ||
      forwardedReceiver.place().local() != forwarderReceiver.id ||
      forwardedReceiver.place().projections().size() != 0) {
    return zc::none;
  }
  if (forwarderCont.statements.size() != 0 ||
      forwarderCont.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& forwarderReturn = forwarderCont.terminator.returnValue().value;
  if (forwarderReturn == zc::none) { return zc::none; }
  bool returnsResult = false;
  ZC_IF_SOME(value, forwarderReturn) {
    returnsResult = value.kind() != mir::MirOperandKind::Constant &&
                    value.place().local() == forwarderResult.id &&
                    value.place().projections().size() == 0;
  }
  if (!returnsResult) { return zc::none; }

  // Leaf: a shared-receiver Method with one receiver Parameter and a single
  // block returning a scalar constant.
  if (leaf.kind != mir::MirFunctionKind::Function ||
      leaf.sourceDefinitionKind != identity::DefinitionKind::Method || leaf.locals.size() != 1 ||
      leaf.blocks.size() != 1) {
    return zc::none;
  }
  const auto& leafReceiver = leaf.locals[0];
  if (leafReceiver.kind != mir::MirLocalKind::Parameter) { return zc::none; }
  auto leafReceiverMutable = receiverReferenceIsMutable(leafReceiver.type, semanticTypes);
  if (leafReceiverMutable == zc::none || ZC_ASSERT_NONNULL(leafReceiverMutable)) {
    return zc::none;
  }
  if (leafReceiver.type != forwarderReceiver.type) { return zc::none; }
  auto leafCarrier = integerCarrierFor(leaf.resultType, semanticTypes);
  if (leafCarrier == zc::none) { return zc::none; }
  const auto leafCarrierValue = ZC_REQUIRE_NONNULL(leafCarrier);
  const auto& leafBlock = leaf.blocks[0];
  if (leafBlock.statements.size() != 0 ||
      leafBlock.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& leafReturn = leafBlock.terminator.returnValue().value;
  if (leafReturn == zc::none) { return zc::none; }
  zc::Maybe<IntegerConstant> leafConstant;
  ZC_IF_SOME(value, leafReturn) {
    if (value.kind() != mir::MirOperandKind::Constant ||
        value.constantValue().type != leaf.resultType) {
      return zc::none;
    }
    const auto integer = value.constantValue().value.integerValue();
    if (integer == zc::none) { return zc::none; }
    auto bits = zeroExtendedBits(ZC_REQUIRE_NONNULL(integer), leafCarrierValue.integerWidth());
    if (bits == zc::none) { return zc::none; }
    leafConstant = IntegerConstant::from(leafCarrierValue, ZC_REQUIRE_NONNULL(bits));
  }
  if (leafConstant == zc::none) { return zc::none; }

  // Caller: the existing two-function receiver-call caller shape: one owner
  // UserLocal, one borrow Temporary, one result Temporary; five entry
  // statements initializing the one-field owner, taking its address, and
  // StorageLive(result), then a Call whose receiver argument is the borrow
  // temporary and whose target is the forwarder.
  if (caller.kind != mir::MirFunctionKind::Function ||
      caller.sourceDefinitionKind != identity::DefinitionKind::Function ||
      caller.locals.size() != 3 || caller.blocks.size() != 2 ||
      caller.resultType != leaf.resultType) {
    return zc::none;
  }
  const auto& ownerLocal = caller.locals[0];
  const auto& borrowTemporary = caller.locals[1];
  const auto& resultTemporary = caller.locals[2];
  if (ownerLocal.kind != mir::MirLocalKind::UserLocal ||
      borrowTemporary.kind != mir::MirLocalKind::Temporary ||
      resultTemporary.kind != mir::MirLocalKind::Temporary) {
    return zc::none;
  }
  auto callerBorrowMutable = receiverReferenceIsMutable(borrowTemporary.type, semanticTypes);
  if (callerBorrowMutable == zc::none || ZC_ASSERT_NONNULL(callerBorrowMutable)) {
    return zc::none;
  }
  auto callerCarrier = integerCarrierFor(caller.resultType, semanticTypes);
  if (callerCarrier == zc::none) { return zc::none; }
  const auto callerCarrierValue = ZC_REQUIRE_NONNULL(callerCarrier);
  const auto& callerEntry = caller.blocks[0];
  const auto& callerCont = caller.blocks[1];
  if (callerEntry.statements.size() != 5 ||
      callerEntry.terminator.kind() != mir::MirTerminatorKind::Call ||
      callerCont.statements.size() != 0 ||
      callerCont.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  if (callerEntry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      callerEntry.statements[0].storageLocal() != ownerLocal.id ||
      callerEntry.statements[1].kind() != mir::MirStatementKind::Assign ||
      callerEntry.statements[2].kind() != mir::MirStatementKind::StorageLive ||
      callerEntry.statements[2].storageLocal() != borrowTemporary.id ||
      callerEntry.statements[3].kind() != mir::MirStatementKind::BorrowCreation ||
      callerEntry.statements[4].kind() != mir::MirStatementKind::StorageLive ||
      callerEntry.statements[4].storageLocal() != resultTemporary.id) {
    return zc::none;
  }
  const auto& initialization = callerEntry.statements[1].assignmentValue();
  if (initialization.initialization != mir::MirInitializationKind::Initialize ||
      initialization.destination.local() != ownerLocal.id ||
      initialization.destination.projections().size() != 0 ||
      initialization.value.kind() != mir::MirRvalueKind::NominalAggregate) {
    return zc::none;
  }
  const auto& aggregate = initialization.value.nominalAggregateValue();
  if (aggregate.type != ownerLocal.type || aggregate.elements.size() != 1) { return zc::none; }
  const auto& elementOperand = aggregate.elements[0].operand;
  if (elementOperand.kind() != mir::MirOperandKind::Constant) { return zc::none; }
  auto ownerCarrier = integerCarrierFor(elementOperand.constantValue().type, semanticTypes);
  if (ownerCarrier == zc::none) { return zc::none; }
  const auto ownerCarrierValue = ZC_REQUIRE_NONNULL(ownerCarrier);
  auto ownerConstant = lirOperandFor(elementOperand, ownerCarrierValue);
  if (ownerConstant == zc::none) { return zc::none; }
  const auto& borrow = callerEntry.statements[3].borrowCreationValue();
  if (borrow.kind != mir::MirBorrowKind::Shared ||
      borrow.destination.local() != borrowTemporary.id ||
      borrow.destination.projections().size() != 0 || borrow.source.local() != ownerLocal.id ||
      borrow.source.projections().size() != 0) {
    return zc::none;
  }
  const auto& callerCall = callerEntry.terminator.callValue();
  if (callerCall.callee != forwarder.owner || callerCall.arguments.size() != 1 ||
      callerCall.effect.kind() != mir::MirCallEffectKind::NoActivation ||
      callerCall.destination.local() != resultTemporary.id ||
      callerCall.destination.projections().size() != 0 ||
      callerCall.normalTarget != callerCont.id || callerCall.unwindTarget != zc::none) {
    return zc::none;
  }
  const auto& callerReceiverArgument = callerCall.arguments[0];
  if (callerReceiverArgument.kind() == mir::MirOperandKind::Constant ||
      callerReceiverArgument.place().local() != borrowTemporary.id ||
      callerReceiverArgument.place().projections().size() != 0) {
    return zc::none;
  }
  const auto& callerReturnValue = callerCont.terminator.returnValue().value;
  if (callerReturnValue == zc::none) { return zc::none; }
  ZC_IF_SOME(value, callerReturnValue) {
    if (value.kind() == mir::MirOperandKind::Constant ||
        value.place().local() != resultTemporary.id || value.place().projections().size() != 0) {
      return zc::none;
    }
  }

  auto callerEntryId = LirBlockId::fromOrdinal(1);
  auto callerContId = LirBlockId::fromOrdinal(2);
  auto forwarderEntryId = LirBlockId::fromOrdinal(1);
  auto forwarderContId = LirBlockId::fromOrdinal(2);
  auto leafEntryId = LirBlockId::fromOrdinal(1);
  if (callerEntryId == zc::none || callerContId == zc::none || forwarderEntryId == zc::none ||
      forwarderContId == zc::none || leafEntryId == zc::none) {
    return zc::none;
  }

  zc::Vector<Function> functions;

  // Function 0: the module initializer calling the forwarder at index 1.
  {
    zc::Vector<Statement> entryStatements;
    entryStatements.add(
        Statement::assign(ownerLocal.id.ordinal(), ZC_REQUIRE_NONNULL(ownerConstant)));
    entryStatements.add(
        Statement::takeAddress(borrowTemporary.id.ordinal(), ownerLocal.id.ordinal()));
    zc::Vector<Operand> arguments;
    arguments.add(Operand::localUse(borrowTemporary.id.ordinal()));
    auto callTerminator = Terminator::callFunction(
        /*calleeIndex=*/1, resultTemporary.id.ordinal(), zc::mv(arguments),
        ZC_REQUIRE_NONNULL(callerContId));
    if (callTerminator == zc::none) { return zc::none; }
    zc::Vector<BasicBlock> callerBlocks;
    callerBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(callerEntryId), zc::mv(entryStatements),
                                ZC_REQUIRE_NONNULL(zc::mv(callTerminator))));
    callerBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(callerContId),
                                Terminator::returnLocal(resultTemporary.id.ordinal())));
    zc::Vector<Local> noParameters;
    zc::Vector<Local> locals;
    locals.add(Local(ownerLocal.id.ordinal(), ownerCarrierValue));
    locals.add(Local(borrowTemporary.id.ordinal(), pointerCarrierValue));
    locals.add(Local(resultTemporary.id.ordinal(), callerCarrierValue));
    functions.add(Function(caller.owner, zc::heapString("zom.module_init"), callerCarrierValue,
                           zc::mv(noParameters), zc::mv(locals), zc::mv(callerBlocks)));
  }

  // Function 1: the forwarder, calling the leaf at index 2 with its own
  // receiver parameter slot as the sole argument.
  {
    zc::Vector<Operand> arguments;
    arguments.add(Operand::localUse(forwarderReceiverOrdinal));
    auto callTerminator = Terminator::callFunction(
        /*calleeIndex=*/2, forwarderResultOrdinal, zc::mv(arguments),
        ZC_REQUIRE_NONNULL(forwarderContId));
    if (callTerminator == zc::none) { return zc::none; }
    zc::Vector<BasicBlock> forwarderBlocks;
    zc::Vector<Statement> forwarderEntryStatements;
    forwarderBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(forwarderEntryId),
                                   zc::mv(forwarderEntryStatements),
                                   ZC_REQUIRE_NONNULL(zc::mv(callTerminator))));
    forwarderBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(forwarderContId),
                                   Terminator::returnLocal(forwarderResultOrdinal)));
    zc::Vector<Local> parameters;
    parameters.add(Local(forwarderReceiverOrdinal, pointerCarrierValue));
    zc::Vector<Local> locals;
    locals.add(Local(forwarderResultOrdinal, forwarderCarrierValue));
    functions.add(Function(forwarder.owner, zc::heapString("zom.forwarder"), forwarderCarrierValue,
                           zc::mv(parameters), zc::mv(locals), zc::mv(forwarderBlocks)));
  }

  // Function 2: the leaf method returning its scalar constant.
  {
    zc::Vector<BasicBlock> leafBlocks;
    leafBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(leafEntryId),
                              Terminator::returnInteger(ZC_REQUIRE_NONNULL(leafConstant))));
    zc::Vector<Local> parameters;
    parameters.add(Local(leafReceiver.id.ordinal(), pointerCarrierValue));
    zc::Vector<Local> noLocals;
    functions.add(Function(leaf.owner, zc::heapString("zom.leaf"), leafCarrierValue,
                           zc::mv(parameters), zc::mv(noLocals), zc::mv(leafBlocks)));
  }

  return Module(zc::mv(functions));
}

zc::Maybe<Module> MirToLirLowering::lowerReceiverVoidThenValueCallModule(
    const mir::MirFunction& caller, const mir::MirFunction& voidCallee,
    const mir::MirFunction& valueCallee, const type::SemanticTypeStore& semanticTypes) {
  // Void callee (the mutating setter): two dense parameter locals (a mutable
  // receiver pointer and one ordinary integer parameter), one block with a sole
  // Overwrite Assign copying the parameter through [Dereference, Field] into the
  // receiver field, and a value-less unit Return.
  if (voidCallee.kind != mir::MirFunctionKind::Function ||
      voidCallee.sourceDefinitionKind != identity::DefinitionKind::Method ||
      voidCallee.locals.size() != 2 || voidCallee.blocks.size() != 1) {
    return zc::none;
  }
  const auto& setterReceiver = voidCallee.locals[0];
  const auto& setterParameter = voidCallee.locals[1];
  if (setterReceiver.kind != mir::MirLocalKind::Parameter ||
      setterParameter.kind != mir::MirLocalKind::Parameter || setterReceiver.id.ordinal() != 1 ||
      setterParameter.id.ordinal() != 2) {
    return zc::none;
  }
  auto setterReceiverMutable = receiverReferenceIsMutable(setterReceiver.type, semanticTypes);
  if (setterReceiverMutable == zc::none || !ZC_ASSERT_NONNULL(setterReceiverMutable)) {
    return zc::none;
  }
  auto setterReceiverCarrier = receiverPointerCarrier(setterReceiver.type, semanticTypes);
  if (setterReceiverCarrier == zc::none) { return zc::none; }
  const auto setterReceiverCarrierValue = ZC_REQUIRE_NONNULL(setterReceiverCarrier);
  auto setterParameterCarrier = integerCarrierFor(setterParameter.type, semanticTypes);
  if (setterParameterCarrier == zc::none) { return zc::none; }
  const auto setterParameterCarrierValue = ZC_REQUIRE_NONNULL(setterParameterCarrier);
  auto setterUnitCarrier = unitCarrierFor(voidCallee.resultType, semanticTypes);
  if (setterUnitCarrier == zc::none) { return zc::none; }
  const auto setterUnitCarrierValue = ZC_REQUIRE_NONNULL(setterUnitCarrier);
  const auto& setterBlock = voidCallee.blocks[0];
  if (setterBlock.statements.size() != 1 ||
      setterBlock.terminator.kind() != mir::MirTerminatorKind::Return ||
      setterBlock.terminator.returnValue().value != zc::none) {
    return zc::none;
  }
  const auto& setterStatement = setterBlock.statements[0];
  if (setterStatement.kind() != mir::MirStatementKind::Assign) { return zc::none; }
  const auto& setterAssign = setterStatement.assignmentValue();
  const auto& setterDest = setterAssign.destination;
  const auto& setterProjections = setterDest.projections();
  if (setterAssign.initialization != mir::MirInitializationKind::Overwrite ||
      setterAssign.value.kind() != mir::MirRvalueKind::Use ||
      setterDest.local() != setterReceiver.id || setterDest.rootType() != setterReceiver.type ||
      setterDest.resultType() != setterParameter.type || setterProjections.size() != 2) {
    return zc::none;
  }
  if (setterProjections[0].kind() != mir::MirProjectionKind::Dereference ||
      setterProjections[0].inputType() != setterReceiver.type ||
      setterProjections[1].kind() != mir::MirProjectionKind::Field ||
      setterProjections[1].resultType() != setterParameter.type ||
      setterProjections[0].resultType() != setterProjections[1].inputType()) {
    return zc::none;
  }
  const auto& setterRhs = setterAssign.value.useValue().operand;
  if (setterRhs.kind() == mir::MirOperandKind::Constant ||
      setterRhs.place().local() != setterParameter.id ||
      setterRhs.place().projections().size() != 0 ||
      setterRhs.place().rootType() != setterParameter.type ||
      setterRhs.place().resultType() != setterParameter.type) {
    return zc::none;
  }

  // Value callee (the shared getter): one shared receiver parameter, one empty
  // block, and a [Dereference, Field] copy of the receiver field.
  if (valueCallee.kind != mir::MirFunctionKind::Function ||
      valueCallee.sourceDefinitionKind != identity::DefinitionKind::Method ||
      valueCallee.locals.size() != 1 || valueCallee.blocks.size() != 1) {
    return zc::none;
  }
  const auto& getterReceiver = valueCallee.locals[0];
  if (getterReceiver.kind != mir::MirLocalKind::Parameter || getterReceiver.id.ordinal() != 1) {
    return zc::none;
  }
  auto getterReceiverMutable = receiverReferenceIsMutable(getterReceiver.type, semanticTypes);
  if (getterReceiverMutable == zc::none || ZC_ASSERT_NONNULL(getterReceiverMutable)) {
    return zc::none;
  }
  auto getterReceiverCarrier = receiverPointerCarrier(getterReceiver.type, semanticTypes);
  if (getterReceiverCarrier == zc::none) { return zc::none; }
  const auto getterReceiverCarrierValue = ZC_REQUIRE_NONNULL(getterReceiverCarrier);
  auto getterCarrier = integerCarrierFor(valueCallee.resultType, semanticTypes);
  if (getterCarrier == zc::none) { return zc::none; }
  const auto getterCarrierValue = ZC_REQUIRE_NONNULL(getterCarrier);
  const auto& getterBlock = valueCallee.blocks[0];
  if (getterBlock.statements.size() != 0 ||
      getterBlock.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& getterReturn = getterBlock.terminator.returnValue().value;
  if (getterReturn == zc::none) { return zc::none; }
  {
    const auto& returned = ZC_ASSERT_NONNULL(getterReturn);
    if (returned.kind() == mir::MirOperandKind::Constant) { return zc::none; }
    const auto& place = returned.place();
    if (place.local() != getterReceiver.id || place.rootType() != getterReceiver.type ||
        place.resultType() != valueCallee.resultType || place.projections().size() != 2 ||
        place.projections()[0].kind() != mir::MirProjectionKind::Dereference ||
        place.projections()[0].inputType() != getterReceiver.type ||
        place.projections()[1].kind() != mir::MirProjectionKind::Field ||
        place.projections()[1].resultType() != valueCallee.resultType ||
        place.projections()[0].resultType() != place.projections()[1].inputType()) {
      return zc::none;
    }
  }

  // Caller: five dense locals across three blocks -- the owner UserLocal, a
  // mutable borrow temporary, the unread unit call-destination temporary, a
  // shared borrow temporary, and the value call result temporary.
  if (caller.kind != mir::MirFunctionKind::Function ||
      caller.sourceDefinitionKind != identity::DefinitionKind::Function ||
      caller.locals.size() != 5 || caller.blocks.size() != 3 ||
      caller.resultType != valueCallee.resultType) {
    return zc::none;
  }
  const auto& ownerLocal = caller.locals[0];
  const auto& borrowMutTemporary = caller.locals[1];
  const auto& unitTemporary = caller.locals[2];
  const auto& borrowShTemporary = caller.locals[3];
  const auto& resultTemporary = caller.locals[4];
  if (ownerLocal.kind != mir::MirLocalKind::UserLocal ||
      borrowMutTemporary.kind != mir::MirLocalKind::Temporary ||
      unitTemporary.kind != mir::MirLocalKind::Temporary ||
      borrowShTemporary.kind != mir::MirLocalKind::Temporary ||
      resultTemporary.kind != mir::MirLocalKind::Temporary) {
    return zc::none;
  }
  for (size_t i = 0; i < caller.locals.size(); ++i) {
    if (caller.locals[i].id.ordinal() != i + 1) { return zc::none; }
  }
  auto borrowMutIsMutable = receiverReferenceIsMutable(borrowMutTemporary.type, semanticTypes);
  auto borrowShIsMutable = receiverReferenceIsMutable(borrowShTemporary.type, semanticTypes);
  if (borrowMutIsMutable == zc::none || !ZC_ASSERT_NONNULL(borrowMutIsMutable) ||
      borrowShIsMutable == zc::none || ZC_ASSERT_NONNULL(borrowShIsMutable)) {
    return zc::none;
  }
  if (unitCarrierFor(unitTemporary.type, semanticTypes) == zc::none ||
      resultTemporary.type != caller.resultType) {
    return zc::none;
  }
  auto callerCarrier = integerCarrierFor(caller.resultType, semanticTypes);
  if (callerCarrier == zc::none) { return zc::none; }
  const auto callerCarrierValue = ZC_REQUIRE_NONNULL(callerCarrier);

  const auto& entry = caller.blocks[0];
  const auto& continuation = caller.blocks[1];
  const auto& tail = caller.blocks[2];
  if (entry.statements.size() != 5 || entry.terminator.kind() != mir::MirTerminatorKind::Call ||
      continuation.statements.size() != 3 ||
      continuation.terminator.kind() != mir::MirTerminatorKind::Call ||
      tail.statements.size() != 0 || tail.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  if (entry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != ownerLocal.id ||
      entry.statements[1].kind() != mir::MirStatementKind::Assign ||
      entry.statements[2].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[2].storageLocal() != borrowMutTemporary.id ||
      entry.statements[3].kind() != mir::MirStatementKind::BorrowCreation ||
      entry.statements[4].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[4].storageLocal() != unitTemporary.id) {
    return zc::none;
  }
  if (continuation.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      continuation.statements[0].storageLocal() != borrowShTemporary.id ||
      continuation.statements[1].kind() != mir::MirStatementKind::BorrowCreation ||
      continuation.statements[2].kind() != mir::MirStatementKind::StorageLive ||
      continuation.statements[2].storageLocal() != resultTemporary.id) {
    return zc::none;
  }
  // The owner folds from a one-element nominal aggregate of a scalar constant.
  const auto& initialization = entry.statements[1].assignmentValue();
  if (initialization.initialization != mir::MirInitializationKind::Initialize ||
      initialization.destination.local() != ownerLocal.id ||
      initialization.destination.projections().size() != 0 ||
      initialization.value.kind() != mir::MirRvalueKind::NominalAggregate) {
    return zc::none;
  }
  const auto& aggregate = initialization.value.nominalAggregateValue();
  if (aggregate.type != ownerLocal.type || aggregate.elements.size() != 1) { return zc::none; }
  const auto& elementOperand = aggregate.elements[0].operand;
  if (elementOperand.kind() != mir::MirOperandKind::Constant) { return zc::none; }
  auto ownerCarrier = integerCarrierFor(elementOperand.constantValue().type, semanticTypes);
  if (ownerCarrier == zc::none) { return zc::none; }
  const auto ownerCarrierValue = ZC_REQUIRE_NONNULL(ownerCarrier);
  auto ownerConstant = lirOperandFor(elementOperand, ownerCarrierValue);
  if (ownerConstant == zc::none) { return zc::none; }
  // The two receiver borrows are distinct, each sourced from the owner slot.
  const auto& mutableBorrow = entry.statements[3].borrowCreationValue();
  const auto& sharedBorrow = continuation.statements[1].borrowCreationValue();
  if (mutableBorrow.kind != mir::MirBorrowKind::Mutable ||
      mutableBorrow.destination.local() != borrowMutTemporary.id ||
      mutableBorrow.destination.projections().size() != 0 ||
      mutableBorrow.source.local() != ownerLocal.id ||
      mutableBorrow.source.projections().size() != 0 ||
      sharedBorrow.kind != mir::MirBorrowKind::Shared ||
      sharedBorrow.destination.local() != borrowShTemporary.id ||
      sharedBorrow.destination.projections().size() != 0 ||
      sharedBorrow.source.local() != ownerLocal.id ||
      sharedBorrow.source.projections().size() != 0) {
    return zc::none;
  }
  // The mutating call targets the setter, activating the mutable borrow and
  // writing its (unread) unit result into the unit temporary.
  const auto& voidCall = entry.terminator.callValue();
  if (voidCall.callee != voidCallee.owner || voidCall.arguments.size() != 2 ||
      voidCall.effect.kind() != mir::MirCallEffectKind::ActivateMutableReceiver ||
      voidCall.destination.local() != unitTemporary.id ||
      voidCall.destination.projections().size() != 0 || voidCall.normalTarget != continuation.id ||
      voidCall.unwindTarget != zc::none) {
    return zc::none;
  }
  {
    auto activated = voidCall.effect.activatedMutableReceiver();
    if (activated == zc::none || ZC_ASSERT_NONNULL(activated) != borrowMutTemporary.id) {
      return zc::none;
    }
  }
  if (voidCall.arguments[0].kind() == mir::MirOperandKind::Constant ||
      voidCall.arguments[0].place().local() != borrowMutTemporary.id ||
      voidCall.arguments[0].place().projections().size() != 0 ||
      voidCall.arguments[1].kind() != mir::MirOperandKind::Constant ||
      voidCall.arguments[1].constantValue().type != setterParameter.type) {
    return zc::none;
  }
  auto setterArgument = lirOperandFor(voidCall.arguments[1], setterParameterCarrierValue);
  if (setterArgument == zc::none) { return zc::none; }
  // The shared call targets the getter with the shared borrow as its sole
  // argument and stores the field value into the result temporary.
  const auto& valueCall = continuation.terminator.callValue();
  if (valueCall.callee != valueCallee.owner || valueCall.arguments.size() != 1 ||
      valueCall.effect.kind() != mir::MirCallEffectKind::NoActivation ||
      valueCall.destination.local() != resultTemporary.id ||
      valueCall.destination.projections().size() != 0 || valueCall.normalTarget != tail.id ||
      valueCall.unwindTarget != zc::none) {
    return zc::none;
  }
  if (valueCall.arguments[0].kind() == mir::MirOperandKind::Constant ||
      valueCall.arguments[0].place().local() != borrowShTemporary.id ||
      valueCall.arguments[0].place().projections().size() != 0) {
    return zc::none;
  }
  {
    const auto& returned = tail.terminator.returnValue().value;
    if (returned == zc::none) { return zc::none; }
    const auto& value = ZC_ASSERT_NONNULL(returned);
    if (value.kind() == mir::MirOperandKind::Constant ||
        value.place().local() != resultTemporary.id || value.place().projections().size() != 0) {
      return zc::none;
    }
  }

  // Dense LIR renumbering: the MIR unit destination temporary (ordinal 3) is
  // never materialized, so the five MIR locals collapse to four LIR slots.
  zc::Vector<uint32_t> lirOrdinalFor;
  uint32_t nextOrdinal = 1;
  for (const auto& local : caller.locals) {
    const bool droppedUnitTemp =
        &local == &caller.locals[2] && unitCarrierFor(local.type, semanticTypes) != zc::none;
    lirOrdinalFor.add(droppedUnitTemp ? 0 : nextOrdinal++);
  }
  ZC_IREQUIRE(nextOrdinal == 5, "four materialized caller slots after the unit drop");

  auto callerBb1 = LirBlockId::fromOrdinal(1);
  auto callerBb2 = LirBlockId::fromOrdinal(2);
  auto callerBb3 = LirBlockId::fromOrdinal(3);
  auto setterEntryId = LirBlockId::fromOrdinal(1);
  auto getterEntryId = LirBlockId::fromOrdinal(1);
  if (callerBb1 == zc::none || callerBb2 == zc::none || callerBb3 == zc::none ||
      setterEntryId == zc::none || getterEntryId == zc::none) {
    return zc::none;
  }

  zc::Vector<Function> functions;

  // Function 0: the module initializer. The unit call has no destination; the
  // getter call stores into the dense result slot (ordinal 4).
  {
    zc::Vector<Statement> bb1Statements;
    bb1Statements.add(Statement::assign(lirOrdinalFor[0], ZC_ASSERT_NONNULL(ownerConstant)));
    bb1Statements.add(Statement::takeAddress(lirOrdinalFor[1], lirOrdinalFor[0]));
    zc::Vector<Operand> voidArguments;
    voidArguments.add(Operand::localUse(lirOrdinalFor[1]));
    voidArguments.add(zc::mv(ZC_ASSERT_NONNULL(setterArgument)));
    auto voidTerminator = Terminator::callVoidFunction(
        /*calleeIndex=*/1, zc::mv(voidArguments), ZC_REQUIRE_NONNULL(callerBb2));
    if (voidTerminator == zc::none) { return zc::none; }

    zc::Vector<Statement> bb2Statements;
    bb2Statements.add(Statement::takeAddress(lirOrdinalFor[3], lirOrdinalFor[0]));
    zc::Vector<Operand> valueArguments;
    valueArguments.add(Operand::localUse(lirOrdinalFor[3]));
    auto valueTerminator = Terminator::callFunction(
        /*calleeIndex=*/2, lirOrdinalFor[4], zc::mv(valueArguments), ZC_REQUIRE_NONNULL(callerBb3));
    if (valueTerminator == zc::none) { return zc::none; }

    zc::Vector<BasicBlock> callerBlocks;
    callerBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(callerBb1), zc::mv(bb1Statements),
                                ZC_REQUIRE_NONNULL(zc::mv(voidTerminator))));
    callerBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(callerBb2), zc::mv(bb2Statements),
                                ZC_REQUIRE_NONNULL(zc::mv(valueTerminator))));
    callerBlocks.add(
        BasicBlock(ZC_REQUIRE_NONNULL(callerBb3), Terminator::returnLocal(lirOrdinalFor[4])));

    zc::Vector<Local> noParameters;
    zc::Vector<Local> locals;
    locals.add(Local(lirOrdinalFor[0], ownerCarrierValue));
    locals.add(Local(lirOrdinalFor[1], setterReceiverCarrierValue));
    locals.add(Local(lirOrdinalFor[3], getterReceiverCarrierValue));
    locals.add(Local(lirOrdinalFor[4], callerCarrierValue));
    functions.add(Function(caller.owner, zc::heapString("zom.module_init"), callerCarrierValue,
                           zc::mv(noParameters), zc::mv(locals), zc::mv(callerBlocks)));
  }

  // Function 1: the mutating setter. It stores the ordinary parameter into the
  // receiver field at offset zero and returns no value.
  {
    zc::Vector<Statement> setterStatements;
    setterStatements.add(Statement::storeField(setterReceiver.id.ordinal(),
                                               Operand::localUse(setterParameter.id.ordinal()),
                                               /*fieldOffsetBytes=*/0));
    zc::Vector<BasicBlock> setterBlocks;
    setterBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(setterEntryId), zc::mv(setterStatements),
                                Terminator::returnVoid()));
    zc::Vector<Local> parameters;
    parameters.add(Local(setterReceiver.id.ordinal(), setterReceiverCarrierValue));
    parameters.add(Local(setterParameter.id.ordinal(), setterParameterCarrierValue));
    zc::Vector<Local> noLocals;
    functions.add(Function(voidCallee.owner, zc::heapString("zom.setter"), setterUnitCarrierValue,
                           zc::mv(parameters), zc::mv(noLocals), zc::mv(setterBlocks)));
  }

  // Function 2: the shared getter. It loads the receiver field at offset zero
  // into a synthesized result slot and returns it.
  {
    const uint32_t resultOrdinal = getterReceiver.id.ordinal() + 1;
    zc::Vector<Statement> getterStatements;
    getterStatements.add(
        Statement::loadField(resultOrdinal, getterReceiver.id.ordinal(), /*fieldOffsetBytes=*/0));
    zc::Vector<BasicBlock> getterBlocks;
    getterBlocks.add(BasicBlock(ZC_REQUIRE_NONNULL(getterEntryId), zc::mv(getterStatements),
                                Terminator::returnLocal(resultOrdinal)));
    zc::Vector<Local> parameters;
    parameters.add(Local(getterReceiver.id.ordinal(), getterReceiverCarrierValue));
    zc::Vector<Local> locals;
    locals.add(Local(resultOrdinal, getterCarrierValue));
    functions.add(Function(valueCallee.owner, zc::heapString("zom.getter"), getterCarrierValue,
                           zc::mv(parameters), zc::mv(locals), zc::mv(getterBlocks)));
  }

  return Module(zc::mv(functions));
}

}  // namespace zomlang::compiler::lir
