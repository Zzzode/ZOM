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

#include "zomlang/compiler/lir/mir-to-lir.h"

#include "zomlang/compiler/checker/facts/signature-facts.h"
#include "zomlang/compiler/mir/built-mir.h"
#include "zomlang/compiler/type/semantic-type-data.h"
#include "zomlang/compiler/type/semantic-type-store.h"

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

}  // namespace zomlang::compiler::lir
