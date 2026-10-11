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

#include "compiler/lir/verify/translation-validator.h"

#include "compiler/checker/facts/signature-facts.h"
#include "compiler/lir/lir-store.h"
#include "compiler/mir/built-mir.h"
#include "compiler/type/semantic-type-data.h"
#include "compiler/type/semantic-type-store.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::lir {
namespace {

using mir::MirFunction;

constexpr uint32_t kFunctionLevel = 0;

TranslationFinding fault(TranslationFaultKind kind, uint32_t functionIndex,
                         uint32_t mirBlock = kFunctionLevel, uint32_t lirBlock = kFunctionLevel,
                         uint32_t statement = kFunctionLevel) noexcept {
  return TranslationFinding{kind, functionIndex, mirBlock, lirBlock, statement};
}

// Independently derived integer carrier for a MIR semantic type. This
// duplicates the producer derivation on purpose: the validator must not call
// any mir-to-lir helper.
zc::Maybe<ValueType> integerCarrier(identity::SemanticTypeId type,
                                    const type::SemanticTypeStore& types) noexcept {
  auto lookup = types.get(type);
  if (!lookup.is<type::SemanticTypeLookup>()) return zc::none;
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  zc::Maybe<type::semantic::PrimitiveKind> kind;
  ZC_IF_SOME(primitive, data.primitiveKind()) { kind = primitive; }
  if (kind == zc::none) return zc::none;
  switch (ZC_ASSERT_NONNULL(kind)) {
    case type::semantic::PrimitiveKind::I8:
    case type::semantic::PrimitiveKind::U8:
      return ValueType::integer(IntegerBitWidth::Bit8);
    case type::semantic::PrimitiveKind::I16:
    case type::semantic::PrimitiveKind::U16:
      return ValueType::integer(IntegerBitWidth::Bit16);
    case type::semantic::PrimitiveKind::I32:
    case type::semantic::PrimitiveKind::U32:
      return ValueType::integer(IntegerBitWidth::Bit32);
    case type::semantic::PrimitiveKind::I64:
    case type::semantic::PrimitiveKind::U64:
      return ValueType::integer(IntegerBitWidth::Bit64);
    default:
      return zc::none;
  }
}

zc::Maybe<ValueType> boolCarrier(identity::SemanticTypeId type,
                                 const type::SemanticTypeStore& types) noexcept {
  auto lookup = types.get(type);
  if (!lookup.is<type::SemanticTypeLookup>()) return zc::none;
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  zc::Maybe<type::semantic::PrimitiveKind> kind;
  ZC_IF_SOME(primitive, data.primitiveKind()) { kind = primitive; }
  if (kind != zc::none && ZC_ASSERT_NONNULL(kind) == type::semantic::PrimitiveKind::Bool) {
    return ValueType::integer(IntegerBitWidth::Bit1);
  }
  return zc::none;
}

// Independently derived float carrier for a MIR semantic type. This
// duplicates the producer derivation on purpose: the validator must not call
// any mir-to-lir helper. F32 lowers to Binary32 and F64 to Binary64; every
// non-float primitive fails closed.
zc::Maybe<ValueType> floatCarrier(identity::SemanticTypeId type,
                                  const type::SemanticTypeStore& types) noexcept {
  auto lookup = types.get(type);
  if (!lookup.is<type::SemanticTypeLookup>()) return zc::none;
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  zc::Maybe<type::semantic::PrimitiveKind> kind;
  ZC_IF_SOME(primitive, data.primitiveKind()) { kind = primitive; }
  if (kind == zc::none) return zc::none;
  switch (ZC_ASSERT_NONNULL(kind)) {
    case type::semantic::PrimitiveKind::F32:
      return ValueType::floating(FloatFormat::Binary32);
    case type::semantic::PrimitiveKind::F64:
      return ValueType::floating(FloatFormat::Binary64);
    default:
      return zc::none;
  }
}

// Independently derived opaque-pointer carrier for a shared const reference
// (the implicit `this` receiver). Mutable references and non-references fail.
zc::Maybe<ValueType> pointerCarrier(identity::SemanticTypeId type,
                                    const type::SemanticTypeStore& types) noexcept {
  auto lookup = types.get(type);
  if (!lookup.is<type::SemanticTypeLookup>()) return zc::none;
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  // Both shared and mutable references lower to the opaque pointer carrier;
  // mutability is enforced by borrow-mode facts rather than the LIR carrier.
  // Raw pointers (*const T / *mut T) share the same physical carrier.
  if (!data.is<type::semantic::ReferenceTypeData>() &&
      !data.is<type::semantic::RawPointerTypeData>()) {
    return zc::none;
  }
  return ValueType::pointer(0);
}

// Independently derived zero-sized Unit carrier for a Unit-result method.
zc::Maybe<ValueType> unitCarrier(identity::SemanticTypeId type,
                                 const type::SemanticTypeStore& types) noexcept {
  auto lookup = types.get(type);
  if (!lookup.is<type::SemanticTypeLookup>()) return zc::none;
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_IF_SOME(primitive, data.primitiveKind()) {
    if (primitive == type::semantic::PrimitiveKind::Unit) return ValueType::unit();
  }
  return zc::none;
}

// Independently derived opaque-pointer carrier for a Str primitive. A str
// value is the address of a null-terminated global string constant; its
// physical carrier is a target pointer in address space zero.
zc::Maybe<ValueType> stringCarrier(identity::SemanticTypeId type,
                                   const type::SemanticTypeStore& types) noexcept {
  auto lookup = types.get(type);
  if (!lookup.is<type::SemanticTypeLookup>()) return zc::none;
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_IF_SOME(primitive, data.primitiveKind()) {
    if (primitive == type::semantic::PrimitiveKind::Str) return ValueType::pointer(0);
  }
  return zc::none;
}

// Independently derived i32 carrier for a nominal (enum) semantic type. This
// duplicates the producer derivation on purpose: the validator must not call
// any mir-to-lir helper. Enum variants lower to integer discriminants; the
// default representation is i32. Struct types are nominal but never appear as
// integer constants in the admitted lowering shapes.
zc::Maybe<ValueType> enumCarrier(identity::SemanticTypeId type,
                                 const type::SemanticTypeStore& types) noexcept {
  auto lookup = types.get(type);
  if (!lookup.is<type::SemanticTypeLookup>()) return zc::none;
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  if (!data.is<type::semantic::NominalTypeData>()) return zc::none;
  return ValueType::integer(IntegerBitWidth::Bit32);
}

// Independently resolves the carrier of one materialized MIR local. Beyond the
// integer, boolean, and float carriers, a shared- or mutable-reference
// parameter or temporary carries an opaque pointer, and a one-field
// aggregate-initialized owner local folds to its single constant element's
// integer carrier (the receiver-call owner slot). Every other local returns
// none.
zc::Maybe<ValueType> localCarrier(const mir::MirLocalDeclaration& source,
                                  const MirFunction& function,
                                  const type::SemanticTypeStore& types) noexcept {
  ZC_IF_SOME(carrier, integerCarrier(source.type, types)) { return carrier; }
  ZC_IF_SOME(carrier, boolCarrier(source.type, types)) { return carrier; }
  ZC_IF_SOME(carrier, floatCarrier(source.type, types)) { return carrier; }
  ZC_IF_SOME(carrier, enumCarrier(source.type, types)) { return carrier; }
  ZC_IF_SOME(carrier, pointerCarrier(source.type, types)) { return carrier; }
  for (const auto& block : function.blocks) {
    for (const auto& statement : block.statements) {
      if (statement.kind() != mir::MirStatementKind::Assign) continue;
      const auto& assignment = statement.assignmentValue();
      if (assignment.destination.local() != source.id ||
          assignment.destination.projections().size() != 0 ||
          assignment.value.kind() != mir::MirRvalueKind::NominalAggregate) {
        continue;
      }
      const auto& aggregate = assignment.value.nominalAggregateValue();
      if (aggregate.elements.size() == 1 &&
          aggregate.elements[0].operand.kind() == mir::MirOperandKind::Constant) {
        return integerCarrier(aggregate.elements[0].operand.constantValue().type, types);
      }
    }
  }
  return zc::none;
}

// Independent zero-extension of a non-negative canonical integer magnitude.
zc::Maybe<uint64_t> zeroExtendedBits(const checker::signature::CanonicalInteger& integer,
                                     IntegerBitWidth width) noexcept {
  if (integer.sign != checker::signature::IntegerSign::NonNegative) return zc::none;
  const auto magnitude = integer.magnitude.asPtr();
  if (magnitude.size() > sizeof(uint64_t)) return zc::none;
  uint64_t bits = 0;
  for (const auto byte : magnitude) bits = (bits << 8) | static_cast<uint64_t>(byte);
  const uint32_t bitCount = static_cast<uint32_t>(width);
  if (bitCount < 64 && bits >= (static_cast<uint64_t>(1) << bitCount)) return zc::none;
  return bits;
}

// Independent two's-complement sign extension of a canonical integer. Negative
// values are encoded as their two's complement representation within the carrier
// width. The value is rejected when its magnitude exceeds the carrier width.
zc::Maybe<uint64_t> signExtendedBits(const checker::signature::CanonicalInteger& integer,
                                     IntegerBitWidth width) noexcept {
  const auto magnitude = integer.magnitude.asPtr();
  if (magnitude.size() > sizeof(uint64_t)) return zc::none;
  uint64_t bits = 0;
  for (const auto byte : magnitude) bits = (bits << 8) | static_cast<uint64_t>(byte);
  const uint32_t bitCount = static_cast<uint32_t>(width);
  if (integer.sign == checker::signature::IntegerSign::Negative) {
    if (bits == 0) return zc::none;
    if (bitCount < 64) {
      const uint64_t limit = (static_cast<uint64_t>(1) << bitCount);
      if (bits > limit) return zc::none;
      bits = (limit - bits) & (limit - 1);
    } else {
      bits = 0 - bits;
    }
  } else {
    if (bitCount < 64 && bits >= (static_cast<uint64_t>(1) << bitCount)) return zc::none;
  }
  return bits;
}

zc::Maybe<Operand> constantFor(const mir::MirOperand& operand, ValueType carrier) noexcept {
  if (operand.kind() != mir::MirOperandKind::Constant) return zc::none;
  const auto boolean = operand.constantValue().value.booleanValue();
  if (boolean != zc::none) {
    if (carrier.kind() != ValueTypeKind::Integer ||
        carrier.integerWidth() != IntegerBitWidth::Bit1) {
      return zc::none;
    }
    auto constant = IntegerConstant::from(carrier, ZC_ASSERT_NONNULL(boolean) ? 1 : 0);
    if (constant == zc::none) return zc::none;
    return Operand::constant(ZC_ASSERT_NONNULL(constant));
  }
  const auto integer = operand.constantValue().value.integerValue();
  if (integer != zc::none) {
    if (carrier.kind() != ValueTypeKind::Integer) return zc::none;
    if (carrier.integerWidth() == IntegerBitWidth::Bit1) return zc::none;
    // Prefer zero extension for non-negative values; fall back to two's
    // complement sign extension for negative constants such as the -1 used
    // by bitwise-not desugaring.
    auto bits = zeroExtendedBits(ZC_ASSERT_NONNULL(integer), carrier.integerWidth());
    if (bits == zc::none) {
      bits = signExtendedBits(ZC_ASSERT_NONNULL(integer), carrier.integerWidth());
    }
    if (bits == zc::none) return zc::none;
    auto constant = IntegerConstant::from(carrier, ZC_ASSERT_NONNULL(bits));
    if (constant == zc::none) return zc::none;
    return Operand::constant(ZC_ASSERT_NONNULL(constant));
  }
  const auto floatValue = operand.constantValue().value.floatValue();
  if (floatValue != zc::none) {
    if (carrier.kind() != ValueTypeKind::Float) return zc::none;
    auto constant = FloatConstant::from(carrier, ZC_ASSERT_NONNULL(floatValue).bits);
    if (constant == zc::none) return zc::none;
    return Operand::constant(ZC_ASSERT_NONNULL(constant));
  }
  return zc::none;
}

/// \brief Read a constant operand's semantic type without an unguarded OneOf
/// access. Returns none for any non-constant operand so callers fail closed
/// with a translation finding instead of aborting (or reading UB under NDEBUG).
zc::Maybe<identity::SemanticTypeId> constantType(const mir::MirOperand& operand) noexcept {
  if (operand.kind() != mir::MirOperandKind::Constant) return zc::none;
  return operand.constantValue().type;
}

ComparisonOp comparisonOp(mir::MirComparisonOperator op) noexcept {
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

ArithmeticOp arithmeticOp(mir::MirArithmeticOperator op) noexcept {
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
      return ArithmeticOp::Pow;
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
  return ArithmeticOp::Add;
}

// Maps a MIR operand used in an expression position to the expected LIR
// operand: a constant of the given carrier, or a bare local use.
zc::Maybe<Operand> expectedOperand(const mir::MirOperand& operand, ValueType carrier) noexcept {
  if (operand.kind() == mir::MirOperandKind::Constant) { return constantFor(operand, carrier); }
  if (operand.place().projections().size() != 0) return zc::none;
  return Operand::localUse(operand.place().local().ordinal());
}

bool sameConstant(const Operand& actual, const mir::MirOperand& expected,
                  ValueType carrier) noexcept {
  auto wanted = expectedOperand(expected, carrier);
  if (wanted == zc::none || actual.isConstant() != ZC_ASSERT_NONNULL(wanted).isConstant()) {
    return false;
  }
  if (actual.isConstant()) {
    // MIR constants are scalar; an aggregate LIR constant can never match a
    // MIR constant, and the integer/float accessors below would reject it.
    if (actual.isAggregateConstant()) { return false; }
    if (actual.isFloatConstant() != ZC_ASSERT_NONNULL(wanted).isFloatConstant()) { return false; }
    if (actual.isFloatConstant()) {
      return actual.floatConstantValue().bits() ==
                 ZC_ASSERT_NONNULL(wanted).floatConstantValue().bits() &&
             actual.floatConstantValue().carrier() == carrier;
    }
    return actual.constantValue().bits() == ZC_ASSERT_NONNULL(wanted).constantValue().bits() &&
           actual.constantValue().carrier() == carrier;
  }
  return actual.localOrdinal() == ZC_ASSERT_NONNULL(wanted).localOrdinal();
}

// Carrier of a declared LIR slot, or null when undeclared.
const ValueType* lirSlotCarrier(const Function& function, uint32_t ordinal) noexcept {
  for (const auto& parameter : function.parameters()) {
    if (parameter.ordinal() == ordinal) return &parameter.carrier();
  }
  for (const auto& local : function.locals()) {
    if (local.ordinal() == ordinal) return &local.carrier();
  }
  return nullptr;
}

// Describes the one constant/aggregate local a single-block return folds.
enum class FoldKind { None, ScalarConstant, DirectConstant, Aggregate };

struct ReturnFold final {
  FoldKind kind = FoldKind::None;
  mir::MirLocalId sourceLocal;
  const mir::MirAssignmentStatement* assignment = nullptr;
  zc::Maybe<identity::DefId> projectedField;
  bool hasFieldProjection = false;
};

}  // namespace

namespace {

// Validates one matched MIR/LIR function pair. `moduleFunctions` is the whole
// LIR module so a Call terminator's stored callee index can be resolved to the
// LIR function whose MIR owner must equal the MIR call target.
zc::Maybe<TranslationFinding> validatePair(uint32_t functionIndex, const MirFunction& mir,
                                           const Function& lir,
                                           const type::SemanticTypeStore& types,
                                           zc::ArrayPtr<const Function> moduleFunctions) noexcept {
  // Block bijection: equal count and dense ordinal order.
  if (mir.blocks.size() != lir.blocks().size()) {
    return fault(TranslationFaultKind::BlockBijectionMismatch, functionIndex);
  }
  for (uint32_t b = 0; b < mir.blocks.size(); ++b) {
    if (mir.blocks[b].id.ordinal() != b + 1 || lir.blocks()[b].id().ordinal() != b + 1) {
      return fault(TranslationFaultKind::BlockBijectionMismatch, functionIndex, b + 1, b + 1);
    }
  }

  // Resolve the return fold for a single-block constant/aggregate function. The
  // fold is a local constant-propagation at the return, not a shape template:
  // a bare or one-field-projected return of a local whose dominating
  // initializer is a constant or nominal aggregate resolves directly at the
  // terminator, with the source slot undeclared in LIR.
  ReturnFold fold;
  if (mir.blocks.size() == 1) {
    const auto& block = mir.blocks[0];
    const auto& terminator = block.terminator;
    if (terminator.kind() == mir::MirTerminatorKind::Return) {
      const auto& returned = terminator.returnValue().value;
      ZC_IF_SOME(operand, returned) {
        // A direct constant return (the scalar callee shape) folds to
        // ReturnInteger with no source local at all.
        if (operand.kind() == mir::MirOperandKind::Constant) {
          fold.kind = FoldKind::DirectConstant;
        } else if (operand.place().projections().size() <= 1) {
          const mir::MirLocalId root = operand.place().local();
          const mir::MirAssignmentStatement* source = nullptr;
          for (const auto& statement : block.statements) {
            if (statement.kind() == mir::MirStatementKind::Assign &&
                statement.assignmentValue().destination.local() == root) {
              source = &statement.assignmentValue();
            }
          }
          if (source != nullptr) {
            fold.sourceLocal = root;
            fold.assignment = source;
            if (operand.place().projections().size() == 1) {
              const auto& projection = operand.place().projections()[0];
              if (projection.kind() != mir::MirProjectionKind::Field) {
                return fault(TranslationFaultKind::PlaceMappingMismatch, functionIndex, 1, 1);
              }
              fold.hasFieldProjection = true;
              fold.projectedField = projection.fieldValue().field;
            }
            switch (source->value.kind()) {
              case mir::MirRvalueKind::Use:
                if (!fold.hasFieldProjection &&
                    source->value.useValue().operand.kind() == mir::MirOperandKind::Constant) {
                  // Only fold when the source local is the sole non-parameter
                  // local. Functions with multiple locals lower through the
                  // arithmetic return path, which materializes every local in
                  // LIR; folding here would mismatch the materialized slots.
                  bool soleNonParameter = true;
                  for (const auto& local : mir.locals) {
                    if (local.kind != mir::MirLocalKind::Parameter && local.id != root) {
                      soleNonParameter = false;
                      break;
                    }
                  }
                  if (soleNonParameter) { fold.kind = FoldKind::ScalarConstant; }
                }
                break;
              case mir::MirRvalueKind::NominalAggregate:
                fold.kind = FoldKind::Aggregate;
                break;
              default:
                break;
            }
          }
        }
      }
    }
  }

  const bool folded = fold.kind != FoldKind::None;

  // A shared- or mutating-receiver method returning this.field has no fold:
  // its Return operand is a copy/move place-use rooted at the receiver
  // parameter through [Dereference, Field]. The read materializes as one
  // LoadField into a synthesized result slot followed by ReturnLocal.
  struct ReceiverFieldRead final {
    uint32_t receiverOrdinal = 0;
    uint32_t resultOrdinal = 0;
  };
  zc::Maybe<ReceiverFieldRead> receiverFieldRead;
  if (!folded && mir.locals.size() == 1 && mir.locals[0].kind == mir::MirLocalKind::Parameter &&
      mir.blocks.size() == 1 && mir.blocks[0].statements.size() == 0) {
    const auto& term = mir.blocks[0].terminator;
    if (term.kind() == mir::MirTerminatorKind::Return && term.returnValue().value != zc::none) {
      const auto& rv = ZC_ASSERT_NONNULL(term.returnValue().value);
      if (rv.kind() != mir::MirOperandKind::Constant) {
        const auto& place = rv.place();
        const uint32_t receiverOrdinal = mir.locals[0].id.ordinal();
        const uint32_t resultOrdinal = receiverOrdinal + 1;
        if (place.local().ordinal() == receiverOrdinal && place.rootType() == mir.locals[0].type &&
            place.resultType() == mir.resultType && place.projections().size() == 2 &&
            place.projections()[0].kind() == mir::MirProjectionKind::Dereference &&
            place.projections()[0].inputType() == mir.locals[0].type &&
            place.projections()[0].resultType() == place.projections()[1].inputType() &&
            place.projections()[1].kind() == mir::MirProjectionKind::Field &&
            place.projections()[1].resultType() == mir.resultType) {
          receiverFieldRead = ReceiverFieldRead{receiverOrdinal, resultOrdinal};
        }
      }
    }
  }

  // A standalone function returning `unsafe { *ptr }` where `ptr` is a
  // raw-pointer parameter has no fold: its Return operand is a copy/move
  // place-use rooted at the parameter through a single [Dereference]
  // projection. The two unsafe-scope boundary statements (Enter/Exit) are
  // semantically transparent and skipped. The read materializes as one
  // LoadField at offset zero into a synthesized result slot followed by
  // ReturnLocal.
  struct RawDereferenceRead final {
    uint32_t paramOrdinal = 0;
    uint32_t resultOrdinal = 0;
  };
  zc::Maybe<RawDereferenceRead> rawDereferenceRead;
  if (!folded && receiverFieldRead == zc::none && mir.locals.size() == 1 &&
      mir.locals[0].kind == mir::MirLocalKind::Parameter && mir.blocks.size() == 1 &&
      mir.blocks[0].statements.size() == 2) {
    const auto& block = mir.blocks[0];
    if (block.statements[0].kind() == mir::MirStatementKind::UnsafeScopeBoundary &&
        block.statements[1].kind() == mir::MirStatementKind::UnsafeScopeBoundary) {
      const auto& enter = block.statements[0].unsafeScopeBoundaryValue();
      const auto& exit = block.statements[1].unsafeScopeBoundaryValue();
      if (enter.kind == mir::MirUnsafeScopeBoundaryKind::Enter &&
          exit.kind == mir::MirUnsafeScopeBoundaryKind::Exit && enter.scope == exit.scope) {
        const auto& term = block.terminator;
        if (term.kind() == mir::MirTerminatorKind::Return && term.returnValue().value != zc::none) {
          const auto& rv = ZC_ASSERT_NONNULL(term.returnValue().value);
          if (rv.kind() != mir::MirOperandKind::Constant) {
            const auto& place = rv.place();
            const uint32_t paramOrdinal = mir.locals[0].id.ordinal();
            const uint32_t resultOrdinal = paramOrdinal + 1;
            if (place.local().ordinal() == paramOrdinal && place.rootType() == mir.locals[0].type &&
                place.resultType() == mir.resultType && place.projections().size() == 1 &&
                place.projections()[0].kind() == mir::MirProjectionKind::Dereference &&
                place.projections()[0].inputType() == mir.locals[0].type &&
                place.projections()[0].resultType() == mir.resultType) {
              rawDereferenceRead = RawDereferenceRead{paramOrdinal, resultOrdinal};
            }
          }
        }
      }
    }
  }

  // A mutating-receiver method whose body is `this.field = <constant>; return
  // this.field;` has one Overwrite Assign whose destination is rooted at the
  // receiver parameter through [Dereference, Field] and the same projected place
  // as the Return operand. The write materializes as one StoreField through the
  // receiver pointer and the read as one LoadField into a synthesized result
  // slot. The receiver must be a mutable reference; a write through a shared
  // receiver cannot be admitted here even if the upstream drain misclassified
  // the method.
  struct ReceiverFieldWriteRead final {
    uint32_t receiverOrdinal = 0;
    uint32_t resultOrdinal = 0;
  };
  zc::Maybe<ReceiverFieldWriteRead> receiverFieldWriteRead;
  if (!folded && receiverFieldRead == zc::none && rawDereferenceRead == zc::none &&
      mir.locals.size() == 1 && mir.locals[0].kind == mir::MirLocalKind::Parameter &&
      mir.blocks.size() == 1) {
    const auto& receiverLocal = mir.locals[0];
    auto receiverLookup = types.get(receiverLocal.type);
    bool receiverMutable = false;
    if (receiverLookup.is<type::SemanticTypeLookup>()) {
      const auto& receiverData = receiverLookup.get<type::SemanticTypeLookup>().data();
      receiverMutable = receiverData.is<type::semantic::ReferenceTypeData>() &&
                        receiverData.get<type::semantic::ReferenceTypeData>().mutability ==
                            type::semantic::Mutability::Mutable;
    }
    const auto& block = mir.blocks[0];
    const auto& term = block.terminator;
    if (receiverMutable && block.statements.size() == 1 &&
        term.kind() == mir::MirTerminatorKind::Return && term.returnValue().value != zc::none) {
      const auto& rv = ZC_ASSERT_NONNULL(term.returnValue().value);
      const auto& write = block.statements[0];
      bool projectedReturn = false;
      if (rv.kind() != mir::MirOperandKind::Constant) {
        const auto& place = rv.place();
        projectedReturn = place.local() == receiverLocal.id &&
                          place.rootType() == receiverLocal.type &&
                          place.resultType() == mir.resultType && place.projections().size() == 2 &&
                          place.projections()[0].kind() == mir::MirProjectionKind::Dereference &&
                          place.projections()[0].inputType() == receiverLocal.type &&
                          place.projections()[1].kind() == mir::MirProjectionKind::Field &&
                          place.projections()[1].resultType() == mir.resultType;
      }
      if (projectedReturn && write.kind() == mir::MirStatementKind::Assign) {
        const auto& assignment = write.assignmentValue();
        const auto& destination = assignment.destination;
        const auto& returnedPlace = rv.place();
        bool sameProjectedPlace =
            destination.local() == returnedPlace.local() &&
            destination.rootType() == returnedPlace.rootType() &&
            destination.resultType() == returnedPlace.resultType() &&
            destination.projections().size() == returnedPlace.projections().size();
        if (sameProjectedPlace) {
          for (size_t p = 0; p < destination.projections().size(); ++p) {
            const auto& wanted = returnedPlace.projections()[p];
            const auto& actual = destination.projections()[p];
            if (actual.kind() != wanted.kind() || actual.inputType() != wanted.inputType() ||
                actual.resultType() != wanted.resultType()) {
              sameProjectedPlace = false;
              break;
            }
            if (actual.kind() == mir::MirProjectionKind::Field &&
                actual.fieldValue().field != wanted.fieldValue().field) {
              sameProjectedPlace = false;
              break;
            }
          }
        }
        if (sameProjectedPlace &&
            assignment.initialization == mir::MirInitializationKind::Overwrite &&
            assignment.value.kind() == mir::MirRvalueKind::Use &&
            assignment.value.useValue().operand.kind() == mir::MirOperandKind::Constant) {
          const uint32_t receiverOrdinal = receiverLocal.id.ordinal();
          receiverFieldWriteRead = ReceiverFieldWriteRead{receiverOrdinal, receiverOrdinal + 1};
        }
      }
    }
  }

  // A mutating-receiver unit method whose sole statement is
  // `this.field = <ordinary-parameter>;` and which ends in a value-less Return.
  // The write materializes as one StoreField carrying a parameter local-use (no
  // read-back, unlike ReceiverFieldWriteRead), and the value-less Return maps to
  // a LIR ReturnVoid. The receiver must be a mutable reference.
  struct ReceiverParameterFieldWriteVoid final {
    uint32_t receiverOrdinal = 0;
    uint32_t parameterOrdinal = 0;
  };
  zc::Maybe<ReceiverParameterFieldWriteVoid> receiverParameterFieldWriteVoid;
  if (!folded && receiverFieldWriteRead == zc::none && mir.locals.size() == 2 &&
      mir.locals[0].kind == mir::MirLocalKind::Parameter &&
      mir.locals[1].kind == mir::MirLocalKind::Parameter &&
      mir.locals[1].id.ordinal() == mir.locals[0].id.ordinal() + 1 && mir.blocks.size() == 1) {
    const auto& receiverLocal = mir.locals[0];
    const auto& parameterLocal = mir.locals[1];
    auto receiverLookup = types.get(receiverLocal.type);
    bool receiverMutable = false;
    if (receiverLookup.is<type::SemanticTypeLookup>()) {
      const auto& receiverData = receiverLookup.get<type::SemanticTypeLookup>().data();
      receiverMutable = receiverData.is<type::semantic::ReferenceTypeData>() &&
                        receiverData.get<type::semantic::ReferenceTypeData>().mutability ==
                            type::semantic::Mutability::Mutable;
    }
    const auto& block = mir.blocks[0];
    const auto& term = block.terminator;
    if (receiverMutable && block.statements.size() == 1 &&
        term.kind() == mir::MirTerminatorKind::Return && term.returnValue().value == zc::none &&
        unitCarrier(mir.resultType, types) != zc::none) {
      const auto& write = block.statements[0];
      if (write.kind() == mir::MirStatementKind::Assign) {
        const auto& assignment = write.assignmentValue();
        const auto& destination = assignment.destination;
        const auto& projections = destination.projections();
        bool projectedDestination =
            assignment.initialization == mir::MirInitializationKind::Overwrite &&
            assignment.value.kind() == mir::MirRvalueKind::Use &&
            destination.local() == receiverLocal.id &&
            destination.rootType() == receiverLocal.type &&
            destination.resultType() == parameterLocal.type && projections.size() == 2 &&
            projections[0].kind() == mir::MirProjectionKind::Dereference &&
            projections[0].inputType() == receiverLocal.type &&
            projections[1].kind() == mir::MirProjectionKind::Field &&
            projections[1].resultType() == parameterLocal.type &&
            projections[0].resultType() == projections[1].inputType();
        const auto& writeOperand = assignment.value.useValue().operand;
        bool parameterRhs = writeOperand.kind() != mir::MirOperandKind::Constant &&
                            writeOperand.place().local() == parameterLocal.id &&
                            writeOperand.place().projections().size() == 0 &&
                            writeOperand.place().rootType() == parameterLocal.type &&
                            writeOperand.place().resultType() == parameterLocal.type;
        if (projectedDestination && parameterRhs) {
          receiverParameterFieldWriteVoid = ReceiverParameterFieldWriteVoid{
              receiverLocal.id.ordinal(), parameterLocal.id.ordinal()};
        }
      }
    }
  }

  // A void-body function: one block with a value-less Return. Two body shapes
  // are admitted, both mapping to a LIR ReturnVoid:
  //   1. Empty body (`deinit() { }`): zero locals, zero statements.
  //   2. Void-let body (`deinit() { let x: T = <lit>; }`): one user local,
  //      StorageLive + Assign(Use constant). The dead local is folded away.
  bool emptyBodyVoid = false;
  if (!folded && receiverParameterFieldWriteVoid == zc::none && mir.blocks.size() == 1) {
    const auto& block = mir.blocks[0];
    const auto& term = block.terminator;
    if (term.kind() == mir::MirTerminatorKind::Return && term.returnValue().value == zc::none &&
        unitCarrier(mir.resultType, types) != zc::none) {
      const bool isEmptyBody = mir.locals.size() == 0 && block.statements.size() == 0;
      const bool isVoidLetBody = mir.locals.size() == 1 &&
                                 mir.locals[0].kind == mir::MirLocalKind::UserLocal &&
                                 block.statements.size() == 2 &&
                                 block.statements[0].kind() == mir::MirStatementKind::StorageLive &&
                                 block.statements[1].kind() == mir::MirStatementKind::Assign;
      if (isEmptyBody || isVoidLetBody) { emptyBodyVoid = true; }
    }
  }

  // A Function-sourced three-block caller that performs a mutating unit receiver
  // call and then a shared value receiver call, returning the value. Five MIR
  // locals (owner, mutable borrow, an unread unit call destination, shared
  // borrow, value result) collapse to four LIR slots: the unit destination is
  // dropped by dense renumbering, and neither call maps under lockstep.
  struct ReceiverVoidThenValueCall final {
    uint32_t ownerMir = 0, borrowMutMir = 0, unitTmpMir = 0;
    uint32_t borrowShMir = 0, resultMir = 0;
    uint32_t lirOwner = 0, lirBorrowMut = 0, lirBorrowSh = 0, lirResult = 0;
  };
  zc::Maybe<ReceiverVoidThenValueCall> receiverVoidThenValueCall;
  if (mir.kind == mir::MirFunctionKind::Function &&
      mir.sourceDefinitionKind == identity::DefinitionKind::Function && mir.locals.size() == 5 &&
      mir.blocks.size() == 3) {
    auto referenceIsMutable = [&](identity::SemanticTypeId type) -> zc::Maybe<bool> {
      auto lookup = types.get(type);
      if (!lookup.is<type::SemanticTypeLookup>()) return zc::none;
      const auto& data = lookup.get<type::SemanticTypeLookup>().data();
      if (!data.is<type::semantic::ReferenceTypeData>()) return zc::none;
      return data.get<type::semantic::ReferenceTypeData>().mutability ==
             type::semantic::Mutability::Mutable;
    };
    const auto& ownerLocal = mir.locals[0];
    const auto& borrowMutLocal = mir.locals[1];
    const auto& unitTmpLocal = mir.locals[2];
    const auto& borrowShLocal = mir.locals[3];
    const auto& resultLocal = mir.locals[4];
    auto borrowMutMutable = referenceIsMutable(borrowMutLocal.type);
    auto borrowShMutable = referenceIsMutable(borrowShLocal.type);
    bool denseOrdinals = true;
    for (uint32_t i = 0; i < mir.locals.size(); ++i) {
      if (mir.locals[i].id.ordinal() != i + 1) denseOrdinals = false;
    }
    bool headerShape =
        denseOrdinals && ownerLocal.kind == mir::MirLocalKind::UserLocal &&
        borrowMutLocal.kind == mir::MirLocalKind::Temporary &&
        unitTmpLocal.kind == mir::MirLocalKind::Temporary &&
        borrowShLocal.kind == mir::MirLocalKind::Temporary &&
        resultLocal.kind == mir::MirLocalKind::Temporary && borrowMutMutable != zc::none &&
        ZC_ASSERT_NONNULL(borrowMutMutable) && borrowShMutable != zc::none &&
        !ZC_ASSERT_NONNULL(borrowShMutable) && unitCarrier(unitTmpLocal.type, types) != zc::none &&
        resultLocal.type == mir.resultType && integerCarrier(mir.resultType, types) != zc::none;
    const auto& bbOne = mir.blocks[0];
    const auto& bbTwo = mir.blocks[1];
    const auto& bbThree = mir.blocks[2];
    if (headerShape && bbOne.statements.size() == 5 &&
        bbOne.terminator.kind() == mir::MirTerminatorKind::Call && bbTwo.statements.size() == 3 &&
        bbTwo.terminator.kind() == mir::MirTerminatorKind::Call && bbThree.statements.size() == 0 &&
        bbThree.terminator.kind() == mir::MirTerminatorKind::Return) {
      bool blockOneShape = bbOne.statements[0].kind() == mir::MirStatementKind::StorageLive &&
                           bbOne.statements[0].storageLocal() == ownerLocal.id &&
                           bbOne.statements[1].kind() == mir::MirStatementKind::Assign &&
                           bbOne.statements[2].kind() == mir::MirStatementKind::StorageLive &&
                           bbOne.statements[2].storageLocal() == borrowMutLocal.id &&
                           bbOne.statements[3].kind() == mir::MirStatementKind::BorrowCreation &&
                           bbOne.statements[4].kind() == mir::MirStatementKind::StorageLive &&
                           bbOne.statements[4].storageLocal() == unitTmpLocal.id;
      const auto& initialization = bbOne.statements[1].assignmentValue();
      bool ownerAggregate =
          initialization.initialization == mir::MirInitializationKind::Initialize &&
          initialization.destination.local() == ownerLocal.id &&
          initialization.destination.projections().size() == 0 &&
          initialization.value.kind() == mir::MirRvalueKind::NominalAggregate;
      if (ownerAggregate) {
        const auto& aggregate = initialization.value.nominalAggregateValue();
        ownerAggregate = aggregate.type == ownerLocal.type && aggregate.elements.size() == 1 &&
                         aggregate.elements[0].operand.kind() == mir::MirOperandKind::Constant;
      }
      const auto& mutableBorrow = bbOne.statements[3].borrowCreationValue();
      bool mutableBorrowShape = mutableBorrow.kind == mir::MirBorrowKind::Mutable &&
                                mutableBorrow.destination.local() == borrowMutLocal.id &&
                                mutableBorrow.destination.projections().size() == 0 &&
                                mutableBorrow.source.local() == ownerLocal.id &&
                                mutableBorrow.source.projections().size() == 0;
      const auto& voidCall = bbOne.terminator.callValue();
      bool voidCallShape =
          voidCall.arguments.size() == 2 &&
          voidCall.arguments[0].kind() != mir::MirOperandKind::Constant &&
          voidCall.arguments[0].place().local() == borrowMutLocal.id &&
          voidCall.arguments[0].place().projections().size() == 0 &&
          voidCall.arguments[1].kind() == mir::MirOperandKind::Constant &&
          voidCall.effect.kind() == mir::MirCallEffectKind::ActivateMutableReceiver &&
          voidCall.effect.activatedMutableReceiver() != zc::none &&
          ZC_ASSERT_NONNULL(voidCall.effect.activatedMutableReceiver()) == borrowMutLocal.id &&
          voidCall.destination.local() == unitTmpLocal.id &&
          voidCall.destination.projections().size() == 0 && voidCall.normalTarget == bbTwo.id &&
          voidCall.unwindTarget == zc::none;
      bool blockTwoShape = bbTwo.statements[0].kind() == mir::MirStatementKind::StorageLive &&
                           bbTwo.statements[0].storageLocal() == borrowShLocal.id &&
                           bbTwo.statements[1].kind() == mir::MirStatementKind::BorrowCreation &&
                           bbTwo.statements[2].kind() == mir::MirStatementKind::StorageLive &&
                           bbTwo.statements[2].storageLocal() == resultLocal.id;
      const auto& sharedBorrow = bbTwo.statements[1].borrowCreationValue();
      bool sharedBorrowShape = sharedBorrow.kind == mir::MirBorrowKind::Shared &&
                               sharedBorrow.destination.local() == borrowShLocal.id &&
                               sharedBorrow.destination.projections().size() == 0 &&
                               sharedBorrow.source.local() == ownerLocal.id &&
                               sharedBorrow.source.projections().size() == 0;
      const auto& valueCall = bbTwo.terminator.callValue();
      bool valueCallShape = valueCall.arguments.size() == 1 &&
                            valueCall.arguments[0].kind() != mir::MirOperandKind::Constant &&
                            valueCall.arguments[0].place().local() == borrowShLocal.id &&
                            valueCall.arguments[0].place().projections().size() == 0 &&
                            valueCall.effect.kind() == mir::MirCallEffectKind::NoActivation &&
                            valueCall.destination.local() == resultLocal.id &&
                            valueCall.destination.projections().size() == 0 &&
                            valueCall.normalTarget == bbThree.id &&
                            valueCall.unwindTarget == zc::none;
      const auto& tailReturn = bbThree.terminator.returnValue().value;
      bool tailShape = tailReturn != zc::none &&
                       ZC_ASSERT_NONNULL(tailReturn).kind() != mir::MirOperandKind::Constant &&
                       ZC_ASSERT_NONNULL(tailReturn).place().local() == resultLocal.id &&
                       ZC_ASSERT_NONNULL(tailReturn).place().projections().size() == 0;
      if (blockOneShape && ownerAggregate && mutableBorrowShape && voidCallShape && blockTwoShape &&
          sharedBorrowShape && valueCallShape && tailShape) {
        zc::Vector<uint32_t> dense;
        uint32_t nextDense = 1;
        for (const auto& local : mir.locals) {
          const bool droppedUnit =
              &local == &mir.locals[2] && unitCarrier(local.type, types) != zc::none;
          dense.add(droppedUnit ? 0 : nextDense++);
        }
        if (nextDense == 5) {
          receiverVoidThenValueCall = ReceiverVoidThenValueCall{ownerLocal.id.ordinal(),
                                                                borrowMutLocal.id.ordinal(),
                                                                unitTmpLocal.id.ordinal(),
                                                                borrowShLocal.id.ordinal(),
                                                                resultLocal.id.ordinal(),
                                                                dense[0],
                                                                dense[1],
                                                                dense[3],
                                                                dense[4]};
        }
      }
    }
  }

  // A shared-receiver method whose body is
  // `return this.<field> OP <constant>;` has one StorageLive/Initialize pair
  // whose arithmetic rvalue combines a [Dereference, Field] place-use of the
  // receiver with a constant. LIR materializes the field read as one LoadField
  // into a synthesized field slot and then one Arithmetic into the synthesized
  // result slot.
  struct ReceiverFieldArithmetic final {
    uint32_t receiverOrdinal = 0;
    uint32_t fieldOrdinal = 0;
    uint32_t resultOrdinal = 0;
    mir::MirArithmeticOperator op{};
    bool fieldIsLeft = true;
  };
  zc::Maybe<ReceiverFieldArithmetic> receiverFieldArithmetic;
  if (!folded && receiverFieldRead == zc::none && receiverFieldWriteRead == zc::none &&
      rawDereferenceRead == zc::none && mir.locals.size() == 2 &&
      mir.locals[0].kind == mir::MirLocalKind::Parameter &&
      mir.locals[1].kind == mir::MirLocalKind::FunctionResult && mir.blocks.size() == 1) {
    const auto& receiverLocal = mir.locals[0];
    const auto& resultLocal = mir.locals[1];
    const auto& block = mir.blocks[0];
    const auto& term = block.terminator;
    if (block.statements.size() == 2 &&
        block.statements[0].kind() == mir::MirStatementKind::StorageLive &&
        block.statements[0].storageLocal() == resultLocal.id &&
        block.statements[1].kind() == mir::MirStatementKind::Assign &&
        term.kind() == mir::MirTerminatorKind::Return && term.returnValue().value != zc::none) {
      const auto& assignment = block.statements[1].assignmentValue();
      const auto& returned = ZC_ASSERT_NONNULL(term.returnValue().value);
      if (assignment.initialization == mir::MirInitializationKind::Initialize &&
          assignment.destination.local() == resultLocal.id &&
          assignment.destination.projections().size() == 0 &&
          assignment.value.kind() == mir::MirRvalueKind::Arithmetic &&
          returned.kind() != mir::MirOperandKind::Constant &&
          returned.place().local() == resultLocal.id &&
          returned.place().projections().size() == 0) {
        const auto& arithmetic = assignment.value.arithmeticValue();
        auto isFieldUse = [&](const mir::MirOperand& operand) -> bool {
          return operand.kind() != mir::MirOperandKind::Constant &&
                 operand.place().local() == receiverLocal.id &&
                 operand.place().rootType() == receiverLocal.type &&
                 operand.place().resultType() == mir.resultType &&
                 operand.place().projections().size() == 2 &&
                 operand.place().projections()[0].kind() == mir::MirProjectionKind::Dereference &&
                 operand.place().projections()[0].inputType() == receiverLocal.type &&
                 operand.place().projections()[1].kind() == mir::MirProjectionKind::Field &&
                 operand.place().projections()[1].resultType() == mir.resultType;
        };
        auto isConstantUse = [&](const mir::MirOperand& operand) -> bool {
          return operand.kind() == mir::MirOperandKind::Constant &&
                 operand.constantValue().type == mir.resultType;
        };
        const bool fieldIsLeft = isFieldUse(arithmetic.left);
        const bool fieldIsRight = isFieldUse(arithmetic.right);
        const mir::MirOperand& constantOperand = fieldIsLeft ? arithmetic.right : arithmetic.left;
        if (fieldIsLeft != fieldIsRight && isConstantUse(constantOperand)) {
          const uint32_t receiverOrdinal = receiverLocal.id.ordinal();
          // LIR synthesizes the field slot at receiver + 1 and places the
          // arithmetic result at receiver + 2; neither MIR local ordinal maps
          // directly, since MIR keeps the field read as a projected place-use.
          receiverFieldArithmetic =
              ReceiverFieldArithmetic{receiverOrdinal, receiverOrdinal + 1, receiverOrdinal + 2,
                                      arithmetic.op, fieldIsLeft};
        }
      }
    }
  }

  // An enum tuple-variant destructure conditional return has no fold and no
  // receiver projection: the scrutinee is a bare enum-typed parameter, the
  // comparison tests the discriminant against a constant, and each arm
  // extracts the payload through an ExtractField rvalue. The LIR lowering
  // synthesizes a tag-temporary local (ordinal parameter-count + 3) and an
  // ExtractField statement in the entry block to read the tag before the
  // comparison, so neither the slot set nor the entry-block effects are in
  // lockstep with the MIR.
  struct EnumDestructureConditional final {
    uint32_t tagTempOrdinal = 0;
    uint32_t paramOrdinal = 0;
  };
  zc::Maybe<EnumDestructureConditional> enumDestructure;
  if (!folded && receiverFieldRead == zc::none && receiverFieldWriteRead == zc::none &&
      rawDereferenceRead == zc::none && receiverFieldArithmetic == zc::none &&
      mir.locals.size() == 3 && mir.locals[0].kind == mir::MirLocalKind::Parameter &&
      mir.locals[1].kind == mir::MirLocalKind::FunctionResult &&
      mir.locals[2].kind == mir::MirLocalKind::Temporary && mir.blocks.size() == 4) {
    const auto& paramLocal = mir.locals[0];
    const auto& resultLocal = mir.locals[1];
    const auto& tempLocal = mir.locals[2];
    const auto& entry = mir.blocks[0];
    if (entry.statements.size() == 3 &&
        entry.statements[0].kind() == mir::MirStatementKind::StorageLive &&
        entry.statements[0].storageLocal() == resultLocal.id &&
        entry.statements[1].kind() == mir::MirStatementKind::StorageLive &&
        entry.statements[1].storageLocal() == tempLocal.id &&
        entry.statements[2].kind() == mir::MirStatementKind::Assign &&
        entry.terminator.kind() == mir::MirTerminatorKind::SwitchInt) {
      const auto& assignment = entry.statements[2].assignmentValue();
      if (assignment.destination.local() == tempLocal.id &&
          assignment.destination.projections().size() == 0 &&
          assignment.value.kind() == mir::MirRvalueKind::Comparison) {
        const auto& comparison = assignment.value.comparisonValue();
        if (comparison.left.kind() != mir::MirOperandKind::Constant &&
            comparison.left.place().local() == paramLocal.id &&
            comparison.left.place().projections().size() == 0 &&
            comparison.right.kind() == mir::MirOperandKind::Constant &&
            enumCarrier(paramLocal.type, types) != zc::none) {
          // Verify both arms extract the payload from the parameter.
          bool armsOk = true;
          for (uint32_t armIdx = 1; armIdx <= 2; ++armIdx) {
            const auto& arm = mir.blocks[armIdx];
            if (arm.statements.size() != 1 ||
                arm.statements[0].kind() != mir::MirStatementKind::Assign ||
                arm.terminator.kind() != mir::MirTerminatorKind::Goto) {
              armsOk = false;
              break;
            }
            const auto& armAssign = arm.statements[0].assignmentValue();
            if (armAssign.destination.local() != resultLocal.id ||
                armAssign.destination.projections().size() != 0 ||
                armAssign.value.kind() != mir::MirRvalueKind::ExtractField) {
              armsOk = false;
              break;
            }
            const auto& extract = armAssign.value.extractFieldValue();
            if (extract.source.kind() == mir::MirOperandKind::Constant ||
                extract.source.place().local() != paramLocal.id ||
                extract.source.place().projections().size() != 0) {
              armsOk = false;
              break;
            }
          }
          if (armsOk) {
            const uint32_t paramOrdinal = paramLocal.id.ordinal();
            enumDestructure =
                EnumDestructureConditional{static_cast<uint32_t>(paramOrdinal + 3), paramOrdinal};
          }
        }
      }
    }
  }

  // An error-unwrap call (`!!`) has a 4-block CFG: entry (StorageLive + Call
  // into the error-union temporary), test (ExtractField tag + Comparison +
  // SwitchInt), trap (Unreachable), and success (ExtractField payload +
  // Return).  The LIR lowering simplifies the tag test to a trivially-true
  // comparison (the callee is a constant return, so the error-union tag is
  // always zero) and copies the scalar call result directly.  The first
  // trailing local therefore carries an integer in LIR instead of the error
  // union aggregate, and the test/success blocks have fewer LIR statements
  // than MIR statements.
  struct ErrorUnwrapCall final {
    uint32_t scalarResultOrdinal = 0;
    uint32_t tagTempOrdinal = 0;
    uint32_t boolTempOrdinal = 0;
    uint32_t resultOrdinal = 0;
  };
  zc::Maybe<ErrorUnwrapCall> errorUnwrapCall;
  if (!folded && receiverFieldRead == zc::none && receiverFieldWriteRead == zc::none &&
      rawDereferenceRead == zc::none && receiverFieldArithmetic == zc::none &&
      receiverVoidThenValueCall == zc::none && enumDestructure == zc::none &&
      mir.blocks.size() == 4) {
    // Count leading parameters.
    size_t paramCount = 0;
    while (paramCount < mir.locals.size() &&
           mir.locals[paramCount].kind == mir::MirLocalKind::Parameter) {
      ++paramCount;
    }
    if (mir.locals.size() - paramCount == 4) {
      const auto& tempLocal = mir.locals[paramCount];
      const auto& tagLocal = mir.locals[paramCount + 1];
      const auto& boolLocal = mir.locals[paramCount + 2];
      const auto& resLocal = mir.locals[paramCount + 3];
      if (tempLocal.kind == mir::MirLocalKind::Temporary &&
          tagLocal.kind == mir::MirLocalKind::Temporary &&
          boolLocal.kind == mir::MirLocalKind::Temporary &&
          resLocal.kind == mir::MirLocalKind::FunctionResult) {
        const auto& entry = mir.blocks[0];
        const auto& test = mir.blocks[1];
        const auto& trap = mir.blocks[2];
        const auto& success = mir.blocks[3];
        if (entry.statements.size() == 1 &&
            entry.statements[0].kind() == mir::MirStatementKind::StorageLive &&
            entry.statements[0].storageLocal() == tempLocal.id &&
            entry.terminator.kind() == mir::MirTerminatorKind::Call &&
            entry.terminator.callValue().destination.local() == tempLocal.id &&
            entry.terminator.callValue().normalTarget == test.id && test.statements.size() == 4 &&
            test.statements[0].kind() == mir::MirStatementKind::StorageLive &&
            test.statements[0].storageLocal() == tagLocal.id &&
            test.statements[1].kind() == mir::MirStatementKind::Assign &&
            test.statements[1].assignmentValue().value.kind() == mir::MirRvalueKind::ExtractField &&
            test.statements[1].assignmentValue().value.extractFieldValue().fieldIndex == 0 &&
            test.statements[2].kind() == mir::MirStatementKind::StorageLive &&
            test.statements[2].storageLocal() == boolLocal.id &&
            test.statements[3].kind() == mir::MirStatementKind::Assign &&
            test.statements[3].assignmentValue().value.kind() == mir::MirRvalueKind::Comparison &&
            test.terminator.kind() == mir::MirTerminatorKind::SwitchInt &&
            trap.statements.size() == 0 &&
            trap.terminator.kind() == mir::MirTerminatorKind::Unreachable &&
            success.statements.size() == 3 &&
            success.statements[0].kind() == mir::MirStatementKind::StorageLive &&
            success.statements[0].storageLocal() == resLocal.id &&
            success.statements[1].kind() == mir::MirStatementKind::Assign &&
            success.statements[1].assignmentValue().value.kind() ==
                mir::MirRvalueKind::ExtractField &&
            success.statements[1].assignmentValue().value.extractFieldValue().fieldIndex == 1 &&
            success.statements[2].kind() == mir::MirStatementKind::StorageDead &&
            success.statements[2].storageLocal() == tempLocal.id &&
            success.terminator.kind() == mir::MirTerminatorKind::Return) {
          errorUnwrapCall = ErrorUnwrapCall{tempLocal.id.ordinal(), tagLocal.id.ordinal(),
                                            boolLocal.id.ordinal(), resLocal.id.ordinal()};
        }
      }
    }
  }

  // Resolve the carrier the LIR return must carry. A materialized function
  // returns the MIR result carrier. A folded scalar function does too; a folded
  // whole-struct bundle carries its first element's carrier, and a folded
  // field projection carries the projected field's carrier.
  zc::Maybe<ValueType> expectedReturnCarrier;
  if (folded && fold.kind == FoldKind::Aggregate && fold.assignment != nullptr) {
    const auto& aggregate = fold.assignment->value.nominalAggregateValue();
    if (aggregate.elements.size() == 0) {
      return fault(TranslationFaultKind::EffectMismatch, functionIndex);
    }
    zc::Maybe<identity::SemanticTypeId> fieldType;
    if (fold.hasFieldProjection && fold.projectedField != zc::none) {
      for (const auto& element : aggregate.elements) {
        if (element.field == ZC_ASSERT_NONNULL(fold.projectedField)) {
          fieldType = constantType(element.operand);
          break;
        }
      }
      if (fieldType == zc::none) { fieldType = constantType(aggregate.elements[0].operand); }
    } else {
      fieldType = constantType(aggregate.elements[0].operand);
    }
    if (fieldType == zc::none) {
      return fault(TranslationFaultKind::EffectMismatch, functionIndex);
    }
    expectedReturnCarrier = integerCarrier(ZC_ASSERT_NONNULL(fieldType), types);
  } else {
    expectedReturnCarrier = integerCarrier(mir.resultType, types);
    if (expectedReturnCarrier == zc::none) {
      expectedReturnCarrier = boolCarrier(mir.resultType, types);
    }
    if (expectedReturnCarrier == zc::none) {
      expectedReturnCarrier = unitCarrier(mir.resultType, types);
    }
    if (expectedReturnCarrier == zc::none) {
      expectedReturnCarrier = stringCarrier(mir.resultType, types);
    }
  }
  if (expectedReturnCarrier == zc::none ||
      lir.returnCarrier() != ZC_ASSERT_NONNULL(expectedReturnCarrier)) {
    return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
  }

  // Slot set. A folded function declares no body locals; a folded direct
  // constant return (the receiver callee shape) may additionally declare its
  // parameter carriers (the implicit `this` pointer), which the scalar body
  // never reads. Every other fold declares no slots at all. A materialized
  // function maps every MIR local one-to-one to a parameter or body-local slot
  // with the independently derived carrier.
  if (folded) {
    uint32_t parameterCount = 0;
    for (const auto& local : mir.locals) {
      if (local.kind == mir::MirLocalKind::Parameter) ++parameterCount;
    }
    // A direct constant return (the literal-method callee) may declare its
    // parameter carriers, which the scalar body never reads. A scalar-constant
    // fold may do the same only when the fold's source local is the sole
    // non-parameter local (the receiver constant-local method fold); the
    // module-function scalar initializer fold still declares no slots.
    bool parametersAllowed = fold.kind == FoldKind::DirectConstant;
    if (!parametersAllowed && fold.kind == FoldKind::ScalarConstant) {
      parametersAllowed = true;
      for (const auto& local : mir.locals) {
        if (local.kind != mir::MirLocalKind::Parameter && local.id != fold.sourceLocal) {
          parametersAllowed = false;
          break;
        }
      }
    }
    if (lir.locals().size() != 0 || (!parametersAllowed && lir.parameters().size() != 0) ||
        (parametersAllowed && lir.parameters().size() != parameterCount)) {
      return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
    }
    for (uint32_t i = 0; i < lir.parameters().size(); ++i) {
      if (i >= mir.locals.size() || mir.locals[i].kind != mir::MirLocalKind::Parameter) {
        return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
      }
      const auto carrier = localCarrier(mir.locals[i], mir, types);
      if (carrier == zc::none || lir.parameters()[i].carrier() != ZC_ASSERT_NONNULL(carrier)) {
        return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
      }
    }
  } else if (receiverFieldRead != zc::none) {
    // The receiver parameter maps 1:1; the field-read result slot is
    // synthesized in LIR (ordinal receiver + 1) and has no MIR local.
    if (lir.parameters().size() != 1 || lir.locals().size() != 1) {
      return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
    }
    const auto& read = ZC_ASSERT_NONNULL(receiverFieldRead);
    const auto parameterCarrier = localCarrier(mir.locals[0], mir, types);
    if (parameterCarrier == zc::none || lir.parameters()[0].ordinal() != read.receiverOrdinal ||
        lir.parameters()[0].carrier() != ZC_ASSERT_NONNULL(parameterCarrier) ||
        lir.locals()[0].ordinal() != read.resultOrdinal ||
        lir.locals()[0].carrier() != ZC_ASSERT_NONNULL(integerCarrier(mir.resultType, types))) {
      return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
    }
  } else if (rawDereferenceRead != zc::none) {
    // The raw-pointer parameter maps 1:1; the dereference result slot is
    // synthesized in LIR (ordinal param + 1) and has no MIR local.
    if (lir.parameters().size() != 1 || lir.locals().size() != 1) {
      return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
    }
    const auto& read = ZC_ASSERT_NONNULL(rawDereferenceRead);
    const auto parameterCarrier = localCarrier(mir.locals[0], mir, types);
    if (parameterCarrier == zc::none || lir.parameters()[0].ordinal() != read.paramOrdinal ||
        lir.parameters()[0].carrier() != ZC_ASSERT_NONNULL(parameterCarrier) ||
        lir.locals()[0].ordinal() != read.resultOrdinal ||
        lir.locals()[0].carrier() != ZC_ASSERT_NONNULL(integerCarrier(mir.resultType, types))) {
      return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
    }
  } else if (receiverFieldWriteRead != zc::none) {
    // The mutable receiver parameter maps 1:1; the field-read result slot is
    // synthesized in LIR (ordinal receiver + 1) and has no MIR local. The
    // write itself names only the receiver slot and the stored constant.
    if (lir.parameters().size() != 1 || lir.locals().size() != 1) {
      return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
    }
    const auto& writeRead = ZC_ASSERT_NONNULL(receiverFieldWriteRead);
    const auto parameterCarrier = localCarrier(mir.locals[0], mir, types);
    if (parameterCarrier == zc::none ||
        lir.parameters()[0].ordinal() != writeRead.receiverOrdinal ||
        lir.parameters()[0].carrier() != ZC_ASSERT_NONNULL(parameterCarrier) ||
        lir.locals()[0].ordinal() != writeRead.resultOrdinal ||
        lir.locals()[0].carrier() != ZC_ASSERT_NONNULL(integerCarrier(mir.resultType, types))) {
      return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
    }
  } else if (receiverFieldArithmetic != zc::none) {
    // The receiver parameter maps 1:1; the field slot and arithmetic result
    // slot are both synthesized in LIR (ordinals receiver + 1 and receiver +
    // 2) and have no one-to-one MIR local.
    const auto& fieldArithmetic = ZC_ASSERT_NONNULL(receiverFieldArithmetic);
    if (lir.parameters().size() != 1 || lir.locals().size() != 2) {
      return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
    }
    const auto parameterCarrier = localCarrier(mir.locals[0], mir, types);
    const auto resultCarrier = integerCarrier(mir.resultType, types);
    if (parameterCarrier == zc::none || resultCarrier == zc::none ||
        lir.parameters()[0].ordinal() != fieldArithmetic.receiverOrdinal ||
        lir.parameters()[0].carrier() != ZC_ASSERT_NONNULL(parameterCarrier) ||
        lir.locals()[0].ordinal() != fieldArithmetic.fieldOrdinal ||
        lir.locals()[0].carrier() != ZC_ASSERT_NONNULL(resultCarrier) ||
        lir.locals()[1].ordinal() != fieldArithmetic.resultOrdinal ||
        lir.locals()[1].carrier() != ZC_ASSERT_NONNULL(resultCarrier)) {
      return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
    }
  } else if (receiverVoidThenValueCall != zc::none) {
    // The caller has no parameters and four dense body locals: the owner slot,
    // the two receiver pointer slots, and the value result slot. The MIR unit
    // call-destination temporary is dropped, so the generic 1:1 local path does
    // not apply.
    const auto& voidThenValue = ZC_ASSERT_NONNULL(receiverVoidThenValueCall);
    if (lir.parameters().size() != 0 || lir.locals().size() != 4) {
      return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
    }
    const auto& ownerAggregate =
        mir.blocks[0].statements[1].assignmentValue().value.nominalAggregateValue();
    auto ownerElementCarrier =
        integerCarrier(ownerAggregate.elements[0].operand.constantValue().type, types);
    auto resultCarrier = integerCarrier(mir.resultType, types);
    const ValueType pointerCarrierValue = ValueType::pointer(0);
    if (ownerElementCarrier == zc::none || resultCarrier == zc::none ||
        lir.locals()[0].ordinal() != voidThenValue.lirOwner ||
        lir.locals()[0].carrier() != ZC_ASSERT_NONNULL(ownerElementCarrier) ||
        lir.locals()[1].ordinal() != voidThenValue.lirBorrowMut ||
        lir.locals()[1].carrier() != pointerCarrierValue ||
        lir.locals()[2].ordinal() != voidThenValue.lirBorrowSh ||
        lir.locals()[2].carrier() != pointerCarrierValue ||
        lir.locals()[3].ordinal() != voidThenValue.lirResult ||
        lir.locals()[3].carrier() != ZC_ASSERT_NONNULL(resultCarrier)) {
      return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
    }
  } else if (enumDestructure != zc::none) {
    // The enum parameter maps 1:1 but carries an aggregate (tag i32 + payload
    // integer) instead of the scalar i32 that localCarrier resolves for enum
    // types. The result and temp locals map 1:1, and one synthesized tag-temp
    // local (ordinal param + 3, carrier i32) has no MIR local.
    const auto& destructure = ZC_ASSERT_NONNULL(enumDestructure);
    if (lir.parameters().size() != 1 || lir.locals().size() != 3) {
      return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
    }
    auto payloadCarrier = integerCarrier(mir.resultType, types);
    if (payloadCarrier == zc::none) { payloadCarrier = boolCarrier(mir.resultType, types); }
    auto tagCarrier = ValueType::integer(IntegerBitWidth::Bit32);
    if (payloadCarrier == zc::none || tagCarrier == zc::none) {
      return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
    }
    zc::Vector<AggregateField> aggFields;
    aggFields.add(AggregateField(zc::str("tag"), zc::mv(ZC_REQUIRE_NONNULL(tagCarrier))));
    aggFields.add(AggregateField(zc::str("payload"), zc::mv(ZC_ASSERT_NONNULL(payloadCarrier))));
    auto enumAggregate = ValueType::aggregate(zc::mv(aggFields));
    if (enumAggregate == zc::none) {
      return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
    }
    if (lir.parameters()[0].ordinal() != destructure.paramOrdinal ||
        lir.parameters()[0].carrier() != ZC_ASSERT_NONNULL(enumAggregate)) {
      return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
    }
    // Body locals: result (ordinal param+1), temp (ordinal param+2), tag temp
    // (ordinal param+3, synthesized).
    const auto resultCarrier = integerCarrier(mir.resultType, types);
    if (resultCarrier == zc::none || lir.locals()[0].ordinal() != destructure.paramOrdinal + 1 ||
        lir.locals()[0].carrier() != ZC_ASSERT_NONNULL(resultCarrier) ||
        lir.locals()[1].ordinal() != destructure.paramOrdinal + 2 ||
        lir.locals()[2].ordinal() != destructure.tagTempOrdinal ||
        lir.locals()[2].carrier() !=
            ZC_ASSERT_NONNULL(ValueType::integer(IntegerBitWidth::Bit32))) {
      return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
    }
  } else if (errorUnwrapCall != zc::none) {
    // The 4 trailing locals map 1:1 by ordinal, but the first trailing local
    // (the MIR error-union temporary) carries an integer in LIR (the scalar
    // call result) instead of the error-union aggregate.  The tag temp (i32),
    // bool temp (Bit1), and result (integer) carry their natural carriers.
    const auto& unwrap = ZC_ASSERT_NONNULL(errorUnwrapCall);
    uint32_t parameterCount = 0;
    for (const auto& local : mir.locals) {
      if (local.kind == mir::MirLocalKind::Parameter) ++parameterCount;
    }
    if (lir.parameters().size() != parameterCount || lir.locals().size() != 4) {
      return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
    }
    for (uint32_t p = 0; p < parameterCount; ++p) {
      if (lir.parameters()[p].ordinal() != mir.locals[p].id.ordinal()) {
        return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
      }
      const auto carrier = localCarrier(mir.locals[p], mir, types);
      if (carrier == zc::none || lir.parameters()[p].carrier() != ZC_ASSERT_NONNULL(carrier)) {
        return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
      }
    }
    auto resultCarrier = integerCarrier(mir.resultType, types);
    if (resultCarrier == zc::none) { resultCarrier = boolCarrier(mir.resultType, types); }
    auto tagCarrier = ValueType::integer(IntegerBitWidth::Bit32);
    auto boolCarrierValue = ValueType::integer(IntegerBitWidth::Bit1);
    if (resultCarrier == zc::none || tagCarrier == zc::none || boolCarrierValue == zc::none ||
        lir.locals()[0].ordinal() != unwrap.scalarResultOrdinal ||
        lir.locals()[0].carrier() != ZC_ASSERT_NONNULL(resultCarrier) ||
        lir.locals()[1].ordinal() != unwrap.tagTempOrdinal ||
        lir.locals()[1].carrier() != ZC_ASSERT_NONNULL(tagCarrier) ||
        lir.locals()[2].ordinal() != unwrap.boolTempOrdinal ||
        lir.locals()[2].carrier() != ZC_ASSERT_NONNULL(boolCarrierValue) ||
        lir.locals()[3].ordinal() != unwrap.resultOrdinal ||
        lir.locals()[3].carrier() != ZC_ASSERT_NONNULL(resultCarrier)) {
      return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
    }
  } else if (emptyBodyVoid) {
    // A void-body destructor declares no LIR slots: the empty-body shape has no
    // MIR locals, and the void-let-body shape folds its dead scalar local away.
    if (lir.parameters().size() != 0 || lir.locals().size() != 0) {
      return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
    }
  } else {
    uint32_t parameterCount = 0;
    for (const auto& local : mir.locals) {
      if (local.kind == mir::MirLocalKind::Parameter) ++parameterCount;
    }
    if (lir.parameters().size() != parameterCount ||
        lir.locals().size() != mir.locals.size() - parameterCount) {
      return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
    }
    for (uint32_t i = 0; i < mir.locals.size(); ++i) {
      const auto& source = mir.locals[i];
      const Local& actual =
          i < parameterCount ? lir.parameters()[i] : lir.locals()[i - parameterCount];
      if (actual.ordinal() != source.id.ordinal()) {
        return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
      }
      const auto carrier = localCarrier(source, mir, types);
      if (carrier == zc::none || actual.carrier() != ZC_ASSERT_NONNULL(carrier)) {
        return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
      }
    }
  }

  // Per-block effect correspondence in lockstep.
  for (uint32_t b = 0; b < mir.blocks.size(); ++b) {
    const auto& mirBlock = mir.blocks[b];
    const BasicBlock& lirBlock = lir.blocks()[b];
    uint32_t lirStatement = 0;

    if (receiverVoidThenValueCall != zc::none) {
      // The void-then-value caller has no lockstep mapping: the unit call carries
      // no destination, the value call's destination is dense-renumbered, and the
      // MIR unit temporary has no LIR slot. Validate the statements and the
      // terminator for each block bespoke, then bypass the generic arms.
      const auto& v = ZC_ASSERT_NONNULL(receiverVoidThenValueCall);
      const auto statements = lirBlock.statements();
      const Terminator& terminator = lirBlock.terminator();
      if (b == 0) {
        const auto& aggregate =
            mir.blocks[0].statements[1].assignmentValue().value.nominalAggregateValue();
        const auto& ownerElement = aggregate.elements[0].operand;
        auto ownerCarrier = integerCarrier(ownerElement.constantValue().type, types);
        const auto& mCall = mir.blocks[0].terminator.callValue();
        auto argumentCarrier = integerCarrier(mCall.arguments[1].constantValue().type, types);
        if (ownerCarrier == zc::none || argumentCarrier == zc::none || statements.size() != 2 ||
            statements[0].kind() != StatementKind::Assign ||
            statements[0].destinationOrdinal() != v.lirOwner ||
            !sameConstant(statements[0].value(), ownerElement, ZC_ASSERT_NONNULL(ownerCarrier)) ||
            statements[1].kind() != StatementKind::TakeAddress ||
            statements[1].destinationOrdinal() != v.lirBorrowMut ||
            statements[1].sourceOrdinal() != v.lirOwner ||
            terminator.kind() != TerminatorKind::Call || terminator.hasCallDestination() ||
            terminator.calleeIndex() >= moduleFunctions.size() ||
            moduleFunctions[terminator.calleeIndex()].owner() != mCall.callee ||
            terminator.callArguments().size() != 2 ||
            terminator.callNormalTarget().ordinal() != 2 ||
            terminator.callArguments()[0].isConstant() ||
            terminator.callArguments()[0].localOrdinal() != v.lirBorrowMut ||
            lirSlotCarrier(moduleFunctions[functionIndex], v.lirBorrowMut) == nullptr ||
            !sameConstant(terminator.callArguments()[1], mCall.arguments[1],
                          ZC_ASSERT_NONNULL(argumentCarrier))) {
          return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1);
        }
      } else if (b == 1) {
        const auto& mCall = mir.blocks[1].terminator.callValue();
        if (statements.size() != 1 || statements[0].kind() != StatementKind::TakeAddress ||
            statements[0].destinationOrdinal() != v.lirBorrowSh ||
            statements[0].sourceOrdinal() != v.lirOwner ||
            terminator.kind() != TerminatorKind::Call || !terminator.hasCallDestination() ||
            terminator.callDestinationOrdinal() != v.lirResult ||
            terminator.calleeIndex() >= moduleFunctions.size() ||
            moduleFunctions[terminator.calleeIndex()].owner() != mCall.callee ||
            terminator.callArguments().size() != 1 ||
            terminator.callNormalTarget().ordinal() != 3 ||
            terminator.callArguments()[0].isConstant() ||
            terminator.callArguments()[0].localOrdinal() != v.lirBorrowSh ||
            lirSlotCarrier(moduleFunctions[functionIndex], v.lirBorrowSh) == nullptr) {
          return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1);
        }
      } else {
        if (b != 2 || statements.size() != 0 || terminator.kind() != TerminatorKind::ReturnLocal ||
            terminator.returnLocalOrdinal() != v.lirResult) {
          return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1);
        }
      }
      continue;
    }

    if (receiverFieldArithmetic != zc::none) {
      // The receiver-field arithmetic body has no lockstep statement mapping:
      // the StorageLive pair carries no LIR effect, the projected field read is
      // a synthesized LoadField, and the arithmetic assign maps to the trailing
      // Arithmetic statement.
      const auto& fieldArithmetic = ZC_ASSERT_NONNULL(receiverFieldArithmetic);
      const auto& mAssignment = mirBlock.statements[1].assignmentValue();
      const auto& mArithmetic = mAssignment.value.arithmeticValue();
      const mir::MirOperand& mField =
          fieldArithmetic.fieldIsLeft ? mArithmetic.left : mArithmetic.right;
      const mir::MirOperand& mConstant =
          fieldArithmetic.fieldIsLeft ? mArithmetic.right : mArithmetic.left;
      const Operand& fieldOperand = fieldArithmetic.fieldIsLeft ? lirBlock.statements()[1].left()
                                                                : lirBlock.statements()[1].right();
      const Operand& constantOperand = fieldArithmetic.fieldIsLeft
                                           ? lirBlock.statements()[1].right()
                                           : lirBlock.statements()[1].left();
      const auto resultCarrier = integerCarrier(mir.resultType, types);
      if (resultCarrier == zc::none || lirBlock.statements().size() != 2 ||
          lirBlock.statements()[0].kind() != StatementKind::LoadField ||
          lirBlock.statements()[0].source().isConstant() ||
          lirBlock.statements()[0].destinationOrdinal() != fieldArithmetic.fieldOrdinal ||
          lirBlock.statements()[0].basePointerOrdinal() != fieldArithmetic.receiverOrdinal ||
          lirBlock.statements()[0].fieldOffsetBytes() != 0 ||
          lirBlock.statements()[1].kind() != StatementKind::Arithmetic ||
          lirBlock.statements()[1].destinationOrdinal() != fieldArithmetic.resultOrdinal ||
          lirBlock.statements()[1].arithmeticOp() != arithmeticOp(mArithmetic.op) ||
          fieldOperand.isConstant() ||
          fieldOperand.localOrdinal() != fieldArithmetic.fieldOrdinal ||
          mField.kind() == mir::MirOperandKind::Constant ||
          !sameConstant(constantOperand, mConstant, ZC_ASSERT_NONNULL(resultCarrier))) {
        return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1);
      }
      lirStatement = 2;
    } else if (enumDestructure != zc::none && b == 0) {
      // The entry block synthesizes an ExtractField (tag from the enum
      // parameter) before the Compare; the MIR entry block has only
      // StorageLive/StorageLive/Assign(Comparison). Validate the two LIR
      // statements bespoke, then bypass the generic lockstep loop.
      const auto& destructure = ZC_ASSERT_NONNULL(enumDestructure);
      const auto& mAssignment = mirBlock.statements[2].assignmentValue();
      const auto& mComparison = mAssignment.value.comparisonValue();
      auto tagCarrier = ValueType::integer(IntegerBitWidth::Bit32);
      if (tagCarrier == zc::none || lirBlock.statements().size() != 2 ||
          lirBlock.statements()[0].kind() != StatementKind::ExtractField ||
          lirBlock.statements()[0].destinationOrdinal() != destructure.tagTempOrdinal ||
          lirBlock.statements()[0].sourceOrdinal() != destructure.paramOrdinal ||
          lirBlock.statements()[0].fieldIndex() != 0 ||
          lirBlock.statements()[1].kind() != StatementKind::Compare ||
          lirBlock.statements()[1].destinationOrdinal() != mir.locals[2].id.ordinal() ||
          lirBlock.statements()[1].comparisonOp() != comparisonOp(mComparison.op) ||
          lirBlock.statements()[1].left().isConstant() ||
          lirBlock.statements()[1].left().localOrdinal() != destructure.tagTempOrdinal ||
          !sameConstant(lirBlock.statements()[1].right(), mComparison.right,
                        ZC_ASSERT_NONNULL(tagCarrier))) {
        return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1);
      }
      lirStatement = 2;
    } else if (errorUnwrapCall != zc::none && (b == 1 || b == 3)) {
      // The error-unwrap test block (b==1) has four MIR statements
      // (StorageLive, Assign ExtractField, StorageLive, Assign Comparison)
      // but the LIR simplifies the tag test to a single trivially-true
      // Compare (the callee is a constant return, so the error tag is always
      // zero).  The success block (b==3) has three MIR statements
      // (StorageLive, Assign ExtractField, StorageDead) but the LIR copies
      // the scalar call result directly in one Assign.  Validate the LIR
      // statements bespoke, then bypass the generic lockstep.
      const auto& unwrap = ZC_ASSERT_NONNULL(errorUnwrapCall);
      if (b == 1) {
        if (lirBlock.statements().size() != 1 ||
            lirBlock.statements()[0].kind() != StatementKind::Compare ||
            lirBlock.statements()[0].destinationOrdinal() != unwrap.boolTempOrdinal ||
            lirBlock.statements()[0].comparisonOp() != ComparisonOp::Eq ||
            !lirBlock.statements()[0].left().isConstant() ||
            lirBlock.statements()[0].left().constantValue().carrier().kind() !=
                ValueTypeKind::Integer ||
            lirBlock.statements()[0].left().constantValue().carrier().integerWidth() !=
                IntegerBitWidth::Bit1 ||
            lirBlock.statements()[0].left().constantValue().bits() != 1 ||
            !lirBlock.statements()[0].right().isConstant() ||
            lirBlock.statements()[0].right().constantValue().carrier().kind() !=
                ValueTypeKind::Integer ||
            lirBlock.statements()[0].right().constantValue().carrier().integerWidth() !=
                IntegerBitWidth::Bit1 ||
            lirBlock.statements()[0].right().constantValue().bits() != 1) {
          return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1);
        }
        lirStatement = 1;
      } else {
        if (lirBlock.statements().size() != 1 ||
            lirBlock.statements()[0].kind() != StatementKind::Assign ||
            lirBlock.statements()[0].destinationOrdinal() != unwrap.resultOrdinal ||
            lirBlock.statements()[0].value().isConstant() ||
            lirBlock.statements()[0].value().localOrdinal() != unwrap.scalarResultOrdinal) {
          return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1);
        }
        lirStatement = 1;
      }
    } else {
      for (uint32_t s = 0; s < mirBlock.statements.size(); ++s) {
        const auto& mirStatement = mirBlock.statements[s];
        const uint32_t statementIndex = s + 1;
        switch (mirStatement.kind()) {
          case mir::MirStatementKind::StorageLive:
          case mir::MirStatementKind::StorageDead:
          case mir::MirStatementKind::UnsafeScopeBoundary:
            // Liveness and unsafe boundaries have no LIR effect in this subset.
            break;
          case mir::MirStatementKind::Assign: {
            const auto& assignment = mirStatement.assignmentValue();
            // The folded initializer is consumed by the return terminator.
            if (folded && &assignment == fold.assignment) break;
            // A void-let-body destructor folds its dead scalar local away: the
            // StorageLive and Assign have no LIR effect.
            if (emptyBodyVoid && mir.locals.size() == 1 &&
                assignment.destination.local() == mir.locals[0].id &&
                assignment.destination.projections().size() == 0 &&
                assignment.value.kind() == mir::MirRvalueKind::Use &&
                assignment.value.useValue().operand.kind() == mir::MirOperandKind::Constant) {
              break;
            }
            if (lirStatement >= lirBlock.statements().size()) {
              return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1,
                           statementIndex);
            }
            const Statement& actual = lirBlock.statements()[lirStatement];
            if ((receiverFieldWriteRead != zc::none ||
                 receiverParameterFieldWriteVoid != zc::none) &&
                assignment.destination.projections().size() == 2) {
              // A mutating receiver field write maps to one StoreField through
              // the receiver pointer at offset zero. The write-read shape stores
              // a constant and reads the field back; the unit-write shape stores
              // an ordinary-parameter local-use and returns no value.
              const auto& projections = assignment.destination.projections();
              const auto fieldCarrier = integerCarrier(assignment.destination.resultType(), types);
              if (fieldCarrier == zc::none || actual.kind() != StatementKind::StoreField ||
                  actual.fieldOffsetBytes() != 0 ||
                  projections[0].kind() != mir::MirProjectionKind::Dereference ||
                  projections[1].kind() != mir::MirProjectionKind::Field) {
                return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1,
                             statementIndex);
              }
              if (receiverFieldWriteRead != zc::none) {
                const auto& writeRead = ZC_ASSERT_NONNULL(receiverFieldWriteRead);
                if (assignment.initialization != mir::MirInitializationKind::Overwrite ||
                    assignment.value.kind() != mir::MirRvalueKind::Use ||
                    assignment.value.useValue().operand.kind() != mir::MirOperandKind::Constant ||
                    actual.basePointerOrdinal() != writeRead.receiverOrdinal ||
                    actual.source().isConstant() ||
                    !sameConstant(actual.storedValue(), assignment.value.useValue().operand,
                                  ZC_ASSERT_NONNULL(fieldCarrier))) {
                  return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1,
                               statementIndex);
                }
              } else {
                const auto& write = ZC_ASSERT_NONNULL(receiverParameterFieldWriteVoid);
                if (assignment.initialization != mir::MirInitializationKind::Overwrite ||
                    assignment.value.kind() != mir::MirRvalueKind::Use ||
                    assignment.value.useValue().operand.kind() == mir::MirOperandKind::Constant ||
                    actual.basePointerOrdinal() != write.receiverOrdinal ||
                    actual.storedValue().isConstant() ||
                    actual.storedValue().localOrdinal() != write.parameterOrdinal) {
                  return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1,
                               statementIndex);
                }
              }
              ++lirStatement;
              break;
            }
            if (assignment.destination.projections().size() != 0) {
              return fault(TranslationFaultKind::PlaceMappingMismatch, functionIndex, b + 1, b + 1,
                           statementIndex);
            }
            const uint32_t destinationOrdinal = assignment.destination.local().ordinal();
            if (actual.destinationOrdinal() != destinationOrdinal) {
              return fault(TranslationFaultKind::PlaceMappingMismatch, functionIndex, b + 1, b + 1,
                           statementIndex);
            }
            const ValueType* destinationCarrier = nullptr;
            if (actual.value().isConstant()) {
              if (actual.value().isFloatConstant()) {
                destinationCarrier = &actual.value().floatConstantValue().carrier();
              } else {
                destinationCarrier = &actual.value().constantValue().carrier();
              }
            } else {
              destinationCarrier = lirSlotCarrier(lir, destinationOrdinal);
            }
            if (destinationCarrier == nullptr) {
              return fault(TranslationFaultKind::SlotSetMismatch, functionIndex, b + 1, b + 1,
                           statementIndex);
            }
            switch (assignment.value.kind()) {
              case mir::MirRvalueKind::Use: {
                if (actual.kind() != StatementKind::Assign) {
                  return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1,
                               statementIndex);
                }
                if (!sameConstant(actual.value(), assignment.value.useValue().operand,
                                  *destinationCarrier)) {
                  return fault(TranslationFaultKind::ConstantMismatch, functionIndex, b + 1, b + 1,
                               statementIndex);
                }
                break;
              }
              case mir::MirRvalueKind::Comparison: {
                if (actual.kind() != StatementKind::Compare) {
                  return fault(TranslationFaultKind::OperatorMismatch, functionIndex, b + 1, b + 1,
                               statementIndex);
                }
                const auto& comparison = assignment.value.comparisonValue();
                if (actual.comparisonOp() != comparisonOp(comparison.op)) {
                  return fault(TranslationFaultKind::OperatorMismatch, functionIndex, b + 1, b + 1,
                               statementIndex);
                }
                // Comparison operands share one operand carrier, resolved from
                // the MIR constant type or place result type (integer, bool,
                // float, or enum in the admitted subset), distinct from the
                // one-bit result slot.
                zc::Maybe<ValueType> leafCarrier;
                if (comparison.left.kind() == mir::MirOperandKind::Constant) {
                  leafCarrier = integerCarrier(comparison.left.constantValue().type, types);
                  if (leafCarrier == zc::none) {
                    leafCarrier = boolCarrier(comparison.left.constantValue().type, types);
                  }
                  if (leafCarrier == zc::none) {
                    leafCarrier = floatCarrier(comparison.left.constantValue().type, types);
                  }
                  if (leafCarrier == zc::none) {
                    leafCarrier = enumCarrier(comparison.left.constantValue().type, types);
                  }
                } else {
                  leafCarrier = integerCarrier(comparison.left.place().resultType(), types);
                  if (leafCarrier == zc::none) {
                    leafCarrier = boolCarrier(comparison.left.place().resultType(), types);
                  }
                  if (leafCarrier == zc::none) {
                    leafCarrier = floatCarrier(comparison.left.place().resultType(), types);
                  }
                  if (leafCarrier == zc::none) {
                    leafCarrier = enumCarrier(comparison.left.place().resultType(), types);
                  }
                }
                if (leafCarrier == zc::none) {
                  return fault(TranslationFaultKind::SlotSetMismatch, functionIndex, b + 1, b + 1,
                               statementIndex);
                }
                // The receiver-conditional-call shape folds a single-field
                // aggregate owner local to a scalar in LIR, so a field
                // projection on the owner becomes a bare local use. Accept
                // the folded left operand when the MIR place has exactly one
                // field projection whose root local matches the LIR local use.
                // The MIR builder stores the comparison result type (bool) as
                // the operand place result type, so the leaf carrier resolved
                // from the MIR place is Bit1; the LIR slot carrier is the
                // correct operand carrier and drives the right-operand check.
                bool leftOk;
                zc::Maybe<ValueType> foldedLeafCarrier;
                if (comparison.left.kind() != mir::MirOperandKind::Constant &&
                    comparison.left.place().projections().size() == 1 &&
                    comparison.left.place().projections()[0].kind() ==
                        mir::MirProjectionKind::Field &&
                    !actual.left().isConstant() &&
                    actual.left().localOrdinal() == comparison.left.place().local().ordinal()) {
                  const auto* slotCarrier = lirSlotCarrier(lir, actual.left().localOrdinal());
                  if (slotCarrier != nullptr) {
                    foldedLeafCarrier = *slotCarrier;
                    leftOk = true;
                  } else {
                    leftOk = false;
                  }
                } else {
                  leftOk =
                      sameConstant(actual.left(), comparison.left, ZC_ASSERT_NONNULL(leafCarrier));
                }
                const ValueType& effectiveCarrier = foldedLeafCarrier != zc::none
                                                        ? ZC_ASSERT_NONNULL(foldedLeafCarrier)
                                                        : ZC_ASSERT_NONNULL(leafCarrier);
                if (!leftOk || !sameConstant(actual.right(), comparison.right, effectiveCarrier)) {
                  return fault(TranslationFaultKind::ConstantMismatch, functionIndex, b + 1, b + 1,
                               statementIndex);
                }
                break;
              }
              case mir::MirRvalueKind::NominalAggregate: {
                // The receiver-call owner slot folds a one-element aggregate of
                // a scalar constant into a plain integer Assign of that element;
                // any other materialized aggregate stays outside the subset.
                if (actual.kind() != StatementKind::Assign) {
                  return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1,
                               statementIndex);
                }
                const auto& aggregate = assignment.value.nominalAggregateValue();
                if (aggregate.elements.size() != 1 ||
                    aggregate.elements[0].operand.kind() != mir::MirOperandKind::Constant) {
                  return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1,
                               statementIndex);
                }
                const auto elementCarrier =
                    integerCarrier(aggregate.elements[0].operand.constantValue().type, types);
                if (elementCarrier == zc::none ||
                    !sameConstant(actual.value(), aggregate.elements[0].operand,
                                  ZC_ASSERT_NONNULL(elementCarrier))) {
                  return fault(TranslationFaultKind::ConstantMismatch, functionIndex, b + 1, b + 1,
                               statementIndex);
                }
                break;
              }
              case mir::MirRvalueKind::Arithmetic: {
                if (actual.kind() != StatementKind::Arithmetic) {
                  return fault(TranslationFaultKind::OperatorMismatch, functionIndex, b + 1, b + 1,
                               statementIndex);
                }
                const auto& arithmetic = assignment.value.arithmeticValue();
                if (actual.arithmeticOp() != arithmeticOp(arithmetic.op)) {
                  return fault(TranslationFaultKind::OperatorMismatch, functionIndex, b + 1, b + 1,
                               statementIndex);
                }
                // An arithmetic result shares the carrier of both operands. The
                // carrier is resolved from the declared destination slot, not the
                // statement's left operand, which may be a constant. BitAnd and
                // BitOr admit a one-bit carrier because the logical short-circuit
                // operators (`&&`, `||`) lower to them on bool operands. Float
                // carriers admit only the five IEEE-754 arithmetic operations
                // (Add, Sub, Mul, Div, Rem); bitwise and shift operations have no
                // float semantics.
                const bool admitsBit1 = arithmetic.op == mir::MirArithmeticOperator::BitAnd ||
                                        arithmetic.op == mir::MirArithmeticOperator::BitOr;
                const bool admitsFloat = arithmetic.op == mir::MirArithmeticOperator::Add ||
                                         arithmetic.op == mir::MirArithmeticOperator::Sub ||
                                         arithmetic.op == mir::MirArithmeticOperator::Mul ||
                                         arithmetic.op == mir::MirArithmeticOperator::Div ||
                                         arithmetic.op == mir::MirArithmeticOperator::Rem;
                const ValueType* resultCarrier = lirSlotCarrier(lir, destinationOrdinal);
                if (resultCarrier == nullptr) {
                  return fault(TranslationFaultKind::SlotSetMismatch, functionIndex, b + 1, b + 1,
                               statementIndex);
                }
                if (resultCarrier->kind() == ValueTypeKind::Float) {
                  if (!admitsFloat) {
                    return fault(TranslationFaultKind::SlotSetMismatch, functionIndex, b + 1, b + 1,
                                 statementIndex);
                  }
                } else if (resultCarrier->kind() != ValueTypeKind::Integer ||
                           (resultCarrier->integerWidth() == IntegerBitWidth::Bit1 &&
                            !admitsBit1)) {
                  return fault(TranslationFaultKind::SlotSetMismatch, functionIndex, b + 1, b + 1,
                               statementIndex);
                }
                auto resolveLeaf = [&](const mir::MirOperand& leaf) -> zc::Maybe<ValueType> {
                  if (leaf.kind() == mir::MirOperandKind::Constant) {
                    auto carrier = integerCarrier(leaf.constantValue().type, types);
                    if (carrier == zc::none) {
                      carrier = boolCarrier(leaf.constantValue().type, types);
                    }
                    if (carrier == zc::none) {
                      carrier = floatCarrier(leaf.constantValue().type, types);
                    }
                    if (carrier == zc::none) {
                      carrier = enumCarrier(leaf.constantValue().type, types);
                    }
                    return carrier;
                  }
                  auto carrier = integerCarrier(leaf.place().resultType(), types);
                  if (carrier == zc::none) {
                    carrier = boolCarrier(leaf.place().resultType(), types);
                  }
                  if (carrier == zc::none) {
                    carrier = floatCarrier(leaf.place().resultType(), types);
                  }
                  if (carrier == zc::none) {
                    carrier = enumCarrier(leaf.place().resultType(), types);
                  }
                  return carrier;
                };
                auto leftCarrier = resolveLeaf(arithmetic.left);
                auto rightCarrier = resolveLeaf(arithmetic.right);
                if (leftCarrier == zc::none || rightCarrier == zc::none ||
                    ZC_REQUIRE_NONNULL(leftCarrier) != *resultCarrier ||
                    ZC_REQUIRE_NONNULL(rightCarrier) != *resultCarrier) {
                  return fault(TranslationFaultKind::SlotSetMismatch, functionIndex, b + 1, b + 1,
                               statementIndex);
                }
                if (!sameConstant(actual.left(), arithmetic.left,
                                  ZC_REQUIRE_NONNULL(leftCarrier)) ||
                    !sameConstant(actual.right(), arithmetic.right,
                                  ZC_REQUIRE_NONNULL(rightCarrier))) {
                  return fault(TranslationFaultKind::ConstantMismatch, functionIndex, b + 1, b + 1,
                               statementIndex);
                }
                break;
              }
              case mir::MirRvalueKind::Erase:
                // A concrete-to-dyn erase lowers to a TakeAddress + VtableAddr
                // pair in LIR (the existential carrier splits into data and
                // vtable pointers). The 1:2 statement mapping is outside the
                // validator's 1:1 correspondence model; the LIR verifier
                // independently validates the lowered structure.
                break;
              case mir::MirRvalueKind::ErrorUnion:
                // Error-union value construction has no LIR lowering in the
                // admitted slice.
                return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1,
                             statementIndex);
              case mir::MirRvalueKind::ExtractField: {
                if (actual.kind() != StatementKind::ExtractField) {
                  return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1,
                               statementIndex);
                }
                const auto& extract = assignment.value.extractFieldValue();
                if (extract.source.kind() == mir::MirOperandKind::Constant ||
                    extract.source.place().projections().size() != 0 ||
                    actual.sourceOrdinal() != extract.source.place().local().ordinal() ||
                    actual.fieldIndex() != extract.fieldIndex) {
                  return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1,
                               statementIndex);
                }
                break;
              }
            }
            ++lirStatement;
            break;
          }
          case mir::MirStatementKind::BorrowCreation: {
            // A receiver borrow maps to TakeAddress of the whole owner slot into
            // the borrow temporary; both shared and mutable borrows share the one
            // pointer carrier, so the MIR borrow kind only has to be one of the
            // two receiver modes.
            if (lirStatement >= lirBlock.statements().size()) {
              return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1,
                           statementIndex);
            }
            const Statement& actual = lirBlock.statements()[lirStatement];
            const auto& borrow = mirStatement.borrowCreationValue();
            if ((borrow.kind != mir::MirBorrowKind::Shared &&
                 borrow.kind != mir::MirBorrowKind::Mutable) ||
                actual.kind() != StatementKind::TakeAddress || actual.source().isConstant() ||
                actual.destinationOrdinal() != borrow.destination.local().ordinal() ||
                actual.sourceOrdinal() != borrow.source.local().ordinal() ||
                borrow.destination.projections().size() != 0 ||
                borrow.source.projections().size() != 0) {
              return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1,
                           statementIndex);
            }
            ++lirStatement;
            break;
          }
          case mir::MirStatementKind::SetDiscriminant:
          case mir::MirStatementKind::Deinitialize:
            return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1,
                         statementIndex);
        }
      }
      if (receiverFieldRead != zc::none) {
        // The projected receiver return has no MIR statement; LIR emits exactly
        // one LoadField into the synthesized result slot.
        if (lirStatement != 0 || lirBlock.statements().size() != 1) {
          return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1, 1);
        }
        const Statement& load = lirBlock.statements()[0];
        const auto& read = ZC_ASSERT_NONNULL(receiverFieldRead);
        const auto baseCarrier = localCarrier(mir.locals[0], mir, types);
        const auto resultCarrier = integerCarrier(mir.resultType, types);
        if (baseCarrier == zc::none || resultCarrier == zc::none ||
            load.kind() != StatementKind::LoadField || load.source().isConstant() ||
            load.destinationOrdinal() != read.resultOrdinal ||
            load.basePointerOrdinal() != read.receiverOrdinal || load.fieldOffsetBytes() != 0 ||
            lir.parameters()[0].carrier() != ZC_ASSERT_NONNULL(baseCarrier) ||
            lir.locals()[0].carrier() != ZC_ASSERT_NONNULL(resultCarrier)) {
          return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1, 1);
        }
      } else if (rawDereferenceRead != zc::none) {
        // The two unsafe-scope boundary statements have no LIR effect; LIR
        // emits exactly one LoadField at offset zero into the synthesized
        // result slot.
        if (lirStatement != 0 || lirBlock.statements().size() != 1) {
          return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1, 1);
        }
        const Statement& load = lirBlock.statements()[0];
        const auto& read = ZC_ASSERT_NONNULL(rawDereferenceRead);
        const auto baseCarrier = localCarrier(mir.locals[0], mir, types);
        const auto resultCarrier = integerCarrier(mir.resultType, types);
        if (baseCarrier == zc::none || resultCarrier == zc::none ||
            load.kind() != StatementKind::LoadField || load.source().isConstant() ||
            load.destinationOrdinal() != read.resultOrdinal ||
            load.basePointerOrdinal() != read.paramOrdinal || load.fieldOffsetBytes() != 0 ||
            lir.parameters()[0].carrier() != ZC_ASSERT_NONNULL(baseCarrier) ||
            lir.locals()[0].carrier() != ZC_ASSERT_NONNULL(resultCarrier)) {
          return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1, 1);
        }
      } else if (receiverFieldWriteRead != zc::none) {
        // The projected write consumed the leading StoreField; the projected
        // return still has no MIR statement, so the trailing LIR statement is
        // exactly one LoadField into the synthesized result slot.
        if (lirStatement != 1 || lirBlock.statements().size() != 2) {
          return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1,
                       lirStatement + 1);
        }
        const Statement& load = lirBlock.statements()[1];
        const auto& writeRead = ZC_ASSERT_NONNULL(receiverFieldWriteRead);
        const auto baseCarrier = localCarrier(mir.locals[0], mir, types);
        const auto resultCarrier = integerCarrier(mir.resultType, types);
        if (baseCarrier == zc::none || resultCarrier == zc::none ||
            load.kind() != StatementKind::LoadField || load.source().isConstant() ||
            load.destinationOrdinal() != writeRead.resultOrdinal ||
            load.basePointerOrdinal() != writeRead.receiverOrdinal ||
            load.fieldOffsetBytes() != 0 ||
            lir.parameters()[0].carrier() != ZC_ASSERT_NONNULL(baseCarrier) ||
            lir.locals()[0].carrier() != ZC_ASSERT_NONNULL(resultCarrier)) {
          return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1, 2);
        }
      } else if (lirStatement != lirBlock.statements().size()) {
        return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1,
                     lirStatement + 1);
      }
    }

    // Terminator correspondence under the dense block bijection.
    const Terminator& lirTerminator = lirBlock.terminator();
    switch (mirBlock.terminator.kind()) {
      case mir::MirTerminatorKind::Return: {
        const auto& returned = mirBlock.terminator.returnValue().value;
        if (returned == zc::none) {
          if ((receiverParameterFieldWriteVoid != zc::none || emptyBodyVoid) &&
              lirTerminator.kind() == TerminatorKind::ReturnVoid) {
            break;  // A value-less Return of a Unit method maps to ReturnVoid.
          }
          return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1);
        }
        const mir::MirOperand& operand = ZC_ASSERT_NONNULL(returned);
        if (folded &&
            (fold.kind == FoldKind::ScalarConstant || fold.kind == FoldKind::DirectConstant)) {
          // The local-fold resolves through the folded initializer; a direct
          // constant return compares the returned operand itself.
          const mir::MirOperand& wanted = fold.kind == FoldKind::DirectConstant
                                              ? operand
                                              : fold.assignment->value.useValue().operand;
          // A string constant return lowers to ReturnString carrying the UTF-8
          // bytes; every other direct constant lowers to ReturnInteger.
          const auto wantedString = wanted.kind() == mir::MirOperandKind::Constant
                                        ? wanted.constantValue().value.stringValue()
                                        : zc::none;
          if (wantedString != zc::none) {
            if (lirTerminator.kind() != TerminatorKind::ReturnString) {
              return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1);
            }
            const auto wantedBytes = ZC_ASSERT_NONNULL(wantedString);
            const auto actualBytes = lirTerminator.returnStringValue().bytes();
            if (actualBytes.size() != wantedBytes.size()) {
              return fault(TranslationFaultKind::ConstantMismatch, functionIndex, b + 1, b + 1);
            }
            for (uint32_t i = 0; i < wantedBytes.size(); ++i) {
              if (actualBytes[i] != wantedBytes[i]) {
                return fault(TranslationFaultKind::ConstantMismatch, functionIndex, b + 1, b + 1);
              }
            }
          } else {
            if (lirTerminator.kind() != TerminatorKind::ReturnInteger) {
              return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1);
            }
            if (!sameConstant(Operand::constant(lirTerminator.returnIntegerValue()), wanted,
                              ZC_ASSERT_NONNULL(expectedReturnCarrier))) {
              return fault(TranslationFaultKind::ConstantMismatch, functionIndex, b + 1, b + 1);
            }
          }
        } else if (folded && fold.kind == FoldKind::Aggregate) {
          const auto& aggregate = fold.assignment->value.nominalAggregateValue();
          if (!fold.hasFieldProjection) {
            if (lirTerminator.kind() != TerminatorKind::ReturnAggregate) {
              return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1);
            }
            const auto slots = lirTerminator.returnAggregateSlots();
            if (slots.size() != aggregate.elements.size()) {
              return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1);
            }
            for (uint32_t e = 0; e < slots.size(); ++e) {
              const auto elementType = constantType(aggregate.elements[e].operand);
              if (elementType == zc::none) {
                return fault(TranslationFaultKind::ConstantMismatch, functionIndex, b + 1, b + 1);
              }
              const auto carrier = integerCarrier(ZC_ASSERT_NONNULL(elementType), types);
              if (carrier == zc::none ||
                  !sameConstant(Operand::constant(slots[e]), aggregate.elements[e].operand,
                                ZC_ASSERT_NONNULL(carrier))) {
                return fault(TranslationFaultKind::ConstantMismatch, functionIndex, b + 1, b + 1);
              }
            }
          } else {
            if (lirTerminator.kind() != TerminatorKind::ReturnInteger) {
              return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1);
            }
            bool found = false;
            for (const auto& element : aggregate.elements) {
              if (fold.projectedField != zc::none &&
                  element.field == ZC_ASSERT_NONNULL(fold.projectedField)) {
                const auto elementType = constantType(element.operand);
                if (elementType == zc::none) {
                  return fault(TranslationFaultKind::ConstantMismatch, functionIndex, b + 1, b + 1);
                }
                const auto carrier = integerCarrier(ZC_ASSERT_NONNULL(elementType), types);
                if (carrier == zc::none ||
                    !sameConstant(Operand::constant(lirTerminator.returnIntegerValue()),
                                  element.operand, ZC_ASSERT_NONNULL(carrier))) {
                  return fault(TranslationFaultKind::ConstantMismatch, functionIndex, b + 1, b + 1);
                }
                found = true;
              }
            }
            if (!found)
              return fault(TranslationFaultKind::PlaceMappingMismatch, functionIndex, b + 1, b + 1);
          }
        } else if (receiverFieldRead != zc::none) {
          if (lirTerminator.kind() != TerminatorKind::ReturnLocal ||
              lirTerminator.returnLocalOrdinal() !=
                  ZC_ASSERT_NONNULL(receiverFieldRead).resultOrdinal) {
            return fault(TranslationFaultKind::PlaceMappingMismatch, functionIndex, b + 1, b + 1);
          }
        } else if (rawDereferenceRead != zc::none) {
          if (lirTerminator.kind() != TerminatorKind::ReturnLocal ||
              lirTerminator.returnLocalOrdinal() !=
                  ZC_ASSERT_NONNULL(rawDereferenceRead).resultOrdinal) {
            return fault(TranslationFaultKind::PlaceMappingMismatch, functionIndex, b + 1, b + 1);
          }
        } else if (receiverFieldWriteRead != zc::none) {
          if (lirTerminator.kind() != TerminatorKind::ReturnLocal ||
              lirTerminator.returnLocalOrdinal() !=
                  ZC_ASSERT_NONNULL(receiverFieldWriteRead).resultOrdinal) {
            return fault(TranslationFaultKind::PlaceMappingMismatch, functionIndex, b + 1, b + 1);
          }
        } else if (receiverFieldArithmetic != zc::none) {
          if (lirTerminator.kind() != TerminatorKind::ReturnLocal ||
              lirTerminator.returnLocalOrdinal() !=
                  ZC_ASSERT_NONNULL(receiverFieldArithmetic).resultOrdinal) {
            return fault(TranslationFaultKind::PlaceMappingMismatch, functionIndex, b + 1, b + 1);
          }
        } else {
          if (lirTerminator.kind() != TerminatorKind::ReturnLocal) {
            return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1);
          }
          if (operand.kind() == mir::MirOperandKind::Constant ||
              operand.place().projections().size() != 0) {
            return fault(TranslationFaultKind::PlaceMappingMismatch, functionIndex, b + 1, b + 1);
          }
          if (lirTerminator.returnLocalOrdinal() != operand.place().local().ordinal()) {
            return fault(TranslationFaultKind::PlaceMappingMismatch, functionIndex, b + 1, b + 1);
          }
        }
        break;
      }
      case mir::MirTerminatorKind::Goto: {
        if (lirTerminator.kind() != TerminatorKind::Goto) {
          return fault(TranslationFaultKind::EdgeTargetMismatch, functionIndex, b + 1, b + 1);
        }
        if (lirTerminator.gotoTarget().ordinal() !=
            mirBlock.terminator.gotoValue().target.ordinal()) {
          return fault(TranslationFaultKind::EdgeTargetMismatch, functionIndex, b + 1, b + 1);
        }
        break;
      }
      case mir::MirTerminatorKind::SwitchInt: {
        if (lirTerminator.kind() != TerminatorKind::CondBranch) {
          return fault(TranslationFaultKind::EdgeTargetMismatch, functionIndex, b + 1, b + 1);
        }
        const auto& switchInt = mirBlock.terminator.switchIntValue();
        if (switchInt.arms.size() < 1) {
          return fault(TranslationFaultKind::EdgeTargetMismatch, functionIndex, b + 1, b + 1);
        }
        if (switchInt.discriminant.kind() == mir::MirOperandKind::Constant ||
            switchInt.discriminant.place().projections().size() != 0) {
          return fault(TranslationFaultKind::PlaceMappingMismatch, functionIndex, b + 1, b + 1);
        }
        if (lirTerminator.conditionOrdinal() != switchInt.discriminant.place().local().ordinal()) {
          return fault(TranslationFaultKind::PlaceMappingMismatch, functionIndex, b + 1, b + 1);
        }
        // Polarity and edge correspondence. The arm mapped to the LIR true
        // target must carry the boolean value `true`; the false target is the
        // MIR default. Reading the arm value (rather than trusting its
        // position) is what proves polarity is preserved instead of inheriting
        // it from the MIR producer's shape rail.
        //
        // Admitted shapes: a one-true-arm loop (arms[0] -> true target, every
        // other value falling through to the default), or a two-arm true/false
        // diamond where arms[1] is the literal false arm sharing the default.
        {
          const auto trueValue = switchInt.arms[0].value.booleanValue();
          if (trueValue == zc::none || !ZC_ASSERT_NONNULL(trueValue)) {
            return fault(TranslationFaultKind::EdgeTargetMismatch, functionIndex, b + 1, b + 1);
          }
          for (uint32_t a = 1; a < switchInt.arms.size(); ++a) {
            const auto armValue = switchInt.arms[a].value.booleanValue();
            if (armValue == zc::none || ZC_ASSERT_NONNULL(armValue)) {
              return fault(TranslationFaultKind::EdgeTargetMismatch, functionIndex, b + 1, b + 1);
            }
          }
        }
        if (lirTerminator.condTrueTarget().ordinal() != switchInt.arms[0].target.ordinal() ||
            lirTerminator.condFalseTarget().ordinal() != switchInt.defaultTarget.ordinal()) {
          return fault(TranslationFaultKind::EdgeTargetMismatch, functionIndex, b + 1, b + 1);
        }
        for (uint32_t a = 1; a < switchInt.arms.size(); ++a) {
          if (switchInt.arms[a].target.ordinal() != switchInt.defaultTarget.ordinal()) {
            return fault(TranslationFaultKind::EdgeTargetMismatch, functionIndex, b + 1, b + 1);
          }
        }
        break;
      }
      case mir::MirTerminatorKind::Call: {
        const auto& call = mirBlock.terminator.callValue();
        if (lirTerminator.kind() != TerminatorKind::Call) {
          return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1);
        }
        // The stored callee index must resolve to the LIR function whose MIR
        // owner is the MIR call's callee owner.
        const uint32_t calleeIndex = lirTerminator.calleeIndex();
        if (calleeIndex >= moduleFunctions.size()) {
          return fault(TranslationFaultKind::CallCalleeMismatch, functionIndex, b + 1, b + 1);
        }
        if (moduleFunctions[calleeIndex].owner() != call.callee) {
          return fault(TranslationFaultKind::CallCalleeMismatch, functionIndex, b + 1, b + 1);
        }
        // The destination place maps to the call destination slot.
        if (call.destination.projections().size() != 0 ||
            lirTerminator.callDestinationOrdinal() != call.destination.local().ordinal()) {
          return fault(TranslationFaultKind::PlaceMappingMismatch, functionIndex, b + 1, b + 1);
        }
        // The normal continuation maps under the block bijection.
        if (lirTerminator.callNormalTarget().ordinal() != call.normalTarget.ordinal()) {
          return fault(TranslationFaultKind::EdgeTargetMismatch, functionIndex, b + 1, b + 1);
        }
        // Argument count and per-argument correspondence. Each argument is a
        // constant (matching constant bits and carrier) or a bare place-use
        // (matching the referenced local ordinal); `sameConstant` resolves both.
        const auto arguments = lirTerminator.callArguments();
        if (arguments.size() != call.arguments.size()) {
          return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1);
        }
        // Borrow-mode/call-effect agreement for receiver calls: when a prior
        // BorrowCreation produced the call's first place-use argument, a
        // mutable receiver call must activate that borrow temporary and a
        // shared receiver call must carry no activation. A first argument with
        // no incoming borrow is an ordinary direct-call operand, not a
        // receiver, and is checked by the generic argument rules below.
        if (call.arguments.size() >= 1 &&
            call.arguments[0].kind() != mir::MirOperandKind::Constant) {
          const auto receiverTemporary = call.arguments[0].place().local();
          zc::Maybe<mir::MirBorrowKind> receiverBorrow;
          for (const auto& prior : mirBlock.statements) {
            if (prior.kind() != mir::MirStatementKind::BorrowCreation) continue;
            const auto& priorBorrow = prior.borrowCreationValue();
            if (priorBorrow.destination.local() == receiverTemporary) {
              receiverBorrow = priorBorrow.kind;
            }
          }
          if (receiverBorrow != zc::none) {
            const auto activated = call.effect.activatedMutableReceiver();
            const bool mutableBorrow =
                ZC_ASSERT_NONNULL(receiverBorrow) == mir::MirBorrowKind::Mutable;
            if (mutableBorrow !=
                    (call.effect.kind() == mir::MirCallEffectKind::ActivateMutableReceiver) ||
                (mutableBorrow &&
                 (activated == zc::none || ZC_ASSERT_NONNULL(activated) != receiverTemporary))) {
              return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1);
            }
          }
        }
        for (uint32_t a = 0; a < arguments.size(); ++a) {
          const mir::MirOperand& sourceArgument = call.arguments[a];
          const identity::SemanticTypeId sourceType =
              sourceArgument.kind() == mir::MirOperandKind::Constant
                  ? sourceArgument.constantValue().type
                  : sourceArgument.place().resultType();
          auto carrier = integerCarrier(sourceType, types);
          if (carrier == zc::none) { carrier = boolCarrier(sourceType, types); }
          if (carrier == zc::none) { carrier = enumCarrier(sourceType, types); }
          if (carrier == zc::none) { carrier = pointerCarrier(sourceType, types); }
          if (carrier == zc::none ||
              !sameConstant(arguments[a], sourceArgument, ZC_ASSERT_NONNULL(carrier))) {
            return fault(TranslationFaultKind::ConstantMismatch, functionIndex, b + 1, b + 1);
          }
          if (!arguments[a].isConstant() &&
              lirSlotCarrier(moduleFunctions[functionIndex], arguments[a].localOrdinal()) ==
                  nullptr) {
            return fault(TranslationFaultKind::PlaceMappingMismatch, functionIndex, b + 1, b + 1);
          }
        }
        break;
      }
      case mir::MirTerminatorKind::IndirectCall: {
        const auto& indirect = mirBlock.terminator.indirectCallValue();
        if (lirTerminator.kind() != TerminatorKind::IndirectCall) {
          return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1);
        }
        // The destination place maps to the call destination slot.
        if (indirect.destination.projections().size() != 0 ||
            lirTerminator.callDestinationOrdinal() != indirect.destination.local().ordinal()) {
          return fault(TranslationFaultKind::PlaceMappingMismatch, functionIndex, b + 1, b + 1);
        }
        // The normal continuation maps under the block bijection.
        if (lirTerminator.callNormalTarget().ordinal() != indirect.normalTarget.ordinal()) {
          return fault(TranslationFaultKind::EdgeTargetMismatch, functionIndex, b + 1, b + 1);
        }
        // The LIR argument bundle is [receiver, ...arguments]: the dyn receiver
        // is passed as the first call argument, followed by every explicit MIR
        // argument in order.
        const auto lirArguments = lirTerminator.callArguments();
        if (lirArguments.size() != indirect.arguments.size() + 1) {
          return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1);
        }
        for (uint32_t a = 0; a < indirect.arguments.size(); ++a) {
          const mir::MirOperand& sourceArgument = indirect.arguments[a];
          const identity::SemanticTypeId sourceType =
              sourceArgument.kind() == mir::MirOperandKind::Constant
                  ? sourceArgument.constantValue().type
                  : sourceArgument.place().resultType();
          auto carrier = integerCarrier(sourceType, types);
          if (carrier == zc::none) { carrier = boolCarrier(sourceType, types); }
          if (carrier == zc::none) { carrier = enumCarrier(sourceType, types); }
          if (carrier == zc::none) { carrier = pointerCarrier(sourceType, types); }
          if (carrier == zc::none ||
              !sameConstant(lirArguments[a + 1], sourceArgument, ZC_ASSERT_NONNULL(carrier))) {
            return fault(TranslationFaultKind::ConstantMismatch, functionIndex, b + 1, b + 1);
          }
        }
        break;
      }
      case mir::MirTerminatorKind::Unreachable:
        if (lirTerminator.kind() != TerminatorKind::Unreachable) {
          return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1);
        }
        break;
    }
  }

  return zc::none;
}

// By-value aggregate call pair (module-level shape). Outer none means the two
// MIR functions are not this shape (the caller falls back to per-pair
// validation); an outer value carries either a valid inner-none result or the
// first fault. The nominal argument is flattened in LIR: the caller aggregate
// constants become the call's integer arguments and the callee's projected
// field selects one flattened parameter slot, so the field order is taken
// independently here from the caller's NominalAggregate rvalue.
struct ByValueAggregateShape final {
  size_t callerIndex = 0;
  size_t calleeIndex = 0;
  identity::SemanticTypeId resultType;
  identity::SemanticTypeId structType;
  identity::DefId projectedField;
  zc::ArrayPtr<const mir::MirNominalAggregateElement> elements;
  uint32_t mirResultOrdinal = 0;
  // LIR renumbers the call result temporary to the sole dense body-local slot
  // (the aggregate user local folds into the call arguments and is dropped).
  static constexpr uint32_t kLirResultOrdinal = 1;
};

zc::Maybe<ByValueAggregateShape> matchByValueAggregateCall(
    zc::ArrayPtr<const MirFunction* const> mirFunctions) noexcept {
  if (mirFunctions.size() != 2) { return zc::none; }
  size_t callerIndex = 0;
  size_t calleeIndex = 0;
  bool foundCaller = false;
  bool foundCallee = false;
  for (size_t index = 0; index < mirFunctions.size(); ++index) {
    const MirFunction& fn = *mirFunctions[index];
    bool isCaller = fn.blocks.size() == 2 && fn.locals.size() == 2 &&
                    fn.locals[0].kind == mir::MirLocalKind::UserLocal &&
                    fn.locals[1].kind == mir::MirLocalKind::Temporary;
    bool isCallee = fn.blocks.size() == 1 && fn.locals.size() == 1 &&
                    fn.locals[0].kind == mir::MirLocalKind::Parameter;
    if (isCaller) {
      if (foundCaller) { return zc::none; }
      callerIndex = index;
      foundCaller = true;
    } else if (isCallee) {
      if (foundCallee) { return zc::none; }
      calleeIndex = index;
      foundCallee = true;
    }
  }
  if (!foundCaller || !foundCallee) { return zc::none; }
  const MirFunction& caller = *mirFunctions[callerIndex];
  const MirFunction& callee = *mirFunctions[calleeIndex];

  // Caller structural shape.
  const auto& aggregateLocal = caller.locals[0];
  const auto& resultLocal = caller.locals[1];
  const auto& entry = caller.blocks[0];
  const auto& continuation = caller.blocks[1];
  if (resultLocal.type != caller.resultType || aggregateLocal.type != callee.locals[0].type ||
      caller.resultType != callee.resultType || entry.id.ordinal() != 1 ||
      continuation.id.ordinal() != 2 || entry.statements.size() != 3 ||
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
  const auto& assignment = entry.statements[1].assignmentValue();
  if (assignment.destination.local() != aggregateLocal.id ||
      assignment.destination.projections().size() != 0 ||
      assignment.value.kind() != mir::MirRvalueKind::NominalAggregate) {
    return zc::none;
  }
  const auto& aggregate = assignment.value.nominalAggregateValue();
  if (aggregate.type != aggregateLocal.type || aggregate.elements.size() < 2) { return zc::none; }
  // Defense in depth: two elements may never name the same projected field;
  // the checker rejects the duplicate property name, but the validated shape
  // must not admit a last-wins pair on its own either.
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
  {
    const auto& value = ZC_ASSERT_NONNULL(continuationReturn);
    if (value.kind() == mir::MirOperandKind::Constant || value.place().local() != resultLocal.id ||
        value.place().projections().size() != 0) {
      return zc::none;
    }
  }

  // Callee structural shape.
  const auto& calleeParameter = callee.locals[0];
  const auto& calleeBlock = callee.blocks[0];
  if (calleeBlock.id.ordinal() != 1 || calleeBlock.statements.size() != 0 ||
      calleeBlock.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& calleeReturn = calleeBlock.terminator.returnValue().value;
  if (calleeReturn == zc::none) { return zc::none; }
  zc::Maybe<identity::DefId> projectedField;
  {
    const auto& value = ZC_ASSERT_NONNULL(calleeReturn);
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

  // The projected field must be one of the aggregate elements; every element is
  // an integer constant of the (i32-only) result carrier.
  bool projectedPresent = false;
  for (const auto& element : aggregate.elements) {
    if (element.operand.kind() != mir::MirOperandKind::Constant) { return zc::none; }
    if (element.field == ZC_ASSERT_NONNULL(projectedField)) { projectedPresent = true; }
  }
  if (!projectedPresent) { return zc::none; }

  ByValueAggregateShape shape;
  shape.callerIndex = callerIndex;
  shape.calleeIndex = calleeIndex;
  shape.resultType = caller.resultType;
  shape.structType = aggregateLocal.type;
  shape.projectedField = ZC_ASSERT_NONNULL(projectedField);
  shape.elements = aggregate.elements.asPtr();
  shape.mirResultOrdinal = resultLocal.id.ordinal();
  return zc::mv(shape);
}

zc::Maybe<TranslationFinding> validateByValueAggregateCall(
    const ByValueAggregateShape& shape, zc::ArrayPtr<const MirFunction* const> mirFunctions,
    const Module& lirModule, const type::SemanticTypeStore& types) noexcept {
  const auto functions = lirModule.functions();
  if (functions.size() != 2) { return fault(TranslationFaultKind::FunctionSetMismatch, 0); }
  auto carrier = integerCarrier(shape.resultType, types);
  if (carrier == zc::none) { return fault(TranslationFaultKind::SlotSetMismatch, 0); }
  const auto carrierValue = ZC_ASSERT_NONNULL(carrier);

  // Resolve the LIR function indices by MIR owner (the producer's callee index
  // is positional; owner matching is independent of module order).
  const MirFunction& caller = *mirFunctions[shape.callerIndex];
  const MirFunction& callee = *mirFunctions[shape.calleeIndex];
  zc::Maybe<uint32_t> lirCaller;
  zc::Maybe<uint32_t> lirCallee;
  for (uint32_t index = 0; index < functions.size(); ++index) {
    if (functions[index].owner() == caller.owner) {
      if (lirCaller != zc::none) { return fault(TranslationFaultKind::FunctionSetMismatch, index); }
      lirCaller = index;
    }
    if (functions[index].owner() == callee.owner) {
      if (lirCallee != zc::none) { return fault(TranslationFaultKind::FunctionSetMismatch, index); }
      lirCallee = index;
    }
  }
  if (lirCaller == zc::none || lirCallee == zc::none) {
    return fault(TranslationFaultKind::FunctionSetMismatch, 0);
  }
  const uint32_t callerLirIndex = ZC_ASSERT_NONNULL(lirCaller);
  const uint32_t calleeLirIndex = ZC_ASSERT_NONNULL(lirCallee);

  // Projected field ordinal (one-based parameter slot) in aggregate order.
  uint32_t projectedOrdinal = 0;
  for (uint32_t index = 0; index < shape.elements.size(); ++index) {
    if (shape.elements[index].field == shape.projectedField) { projectedOrdinal = index + 1; }
  }
  if (projectedOrdinal == 0) { return fault(TranslationFaultKind::PlaceMappingMismatch, 0); }

  // Caller LIR: zero parameters, one result body local, entry Call with one
  // constant argument per aggregate element into the result slot, continuation
  // ReturnLocal.
  {
    const Function& lirCallerFn = functions[callerLirIndex];
    if (lirCallerFn.returnCarrier() != carrierValue) {
      return fault(TranslationFaultKind::SlotSetMismatch, callerLirIndex);
    }
    if (lirCallerFn.parameters().size() != 0 || lirCallerFn.locals().size() != 1 ||
        lirCallerFn.locals()[0].ordinal() != ByValueAggregateShape::kLirResultOrdinal ||
        lirCallerFn.locals()[0].carrier() != carrierValue) {
      return fault(TranslationFaultKind::SlotSetMismatch, callerLirIndex);
    }
    if (lirCallerFn.blocks().size() != 2) {
      return fault(TranslationFaultKind::BlockBijectionMismatch, callerLirIndex);
    }
    const auto& lirEntry = lirCallerFn.blocks()[0];
    const auto& lirCont = lirCallerFn.blocks()[1];
    if (lirEntry.id().ordinal() != 1 || lirCont.id().ordinal() != 2 ||
        lirEntry.statements().size() != 0 || lirCont.statements().size() != 0) {
      return fault(TranslationFaultKind::BlockBijectionMismatch, callerLirIndex);
    }
    const auto& entryTerminator = lirEntry.terminator();
    if (entryTerminator.kind() != TerminatorKind::Call || !entryTerminator.hasCallDestination() ||
        entryTerminator.calleeIndex() >= functions.size() ||
        functions[entryTerminator.calleeIndex()].owner() != callee.owner ||
        entryTerminator.callDestinationOrdinal() != ByValueAggregateShape::kLirResultOrdinal ||
        entryTerminator.callNormalTarget() != lirCont.id() ||
        entryTerminator.callArguments().size() != shape.elements.size()) {
      return fault(TranslationFaultKind::EffectMismatch, callerLirIndex, 1, 1);
    }
    for (uint32_t index = 0; index < shape.elements.size(); ++index) {
      const auto& actual = entryTerminator.callArguments()[index];
      if (!sameConstant(actual, shape.elements[index].operand, carrierValue)) {
        return fault(TranslationFaultKind::PlaceMappingMismatch, callerLirIndex, 1, 1);
      }
    }
    if (lirCont.terminator().kind() != TerminatorKind::ReturnLocal ||
        lirCont.terminator().returnLocalOrdinal() != ByValueAggregateShape::kLirResultOrdinal) {
      return fault(TranslationFaultKind::EffectMismatch, callerLirIndex, 2, 2);
    }
  }

  // Callee LIR: one integer parameter slot per flattened field, no body locals,
  // single block returning the projected field's slot.
  {
    const Function& lirCalleeFn = functions[calleeLirIndex];
    if (lirCalleeFn.returnCarrier() != carrierValue) {
      return fault(TranslationFaultKind::SlotSetMismatch, calleeLirIndex);
    }
    if (lirCalleeFn.locals().size() != 0 ||
        lirCalleeFn.parameters().size() != shape.elements.size() ||
        lirCalleeFn.blocks().size() != 1) {
      return fault(TranslationFaultKind::SlotSetMismatch, calleeLirIndex);
    }
    for (uint32_t index = 0; index < shape.elements.size(); ++index) {
      if (lirCalleeFn.parameters()[index].ordinal() != index + 1 ||
          lirCalleeFn.parameters()[index].carrier() != carrierValue) {
        return fault(TranslationFaultKind::SlotSetMismatch, calleeLirIndex);
      }
    }
    const auto& block = lirCalleeFn.blocks()[0];
    if (block.id().ordinal() != 1 || block.statements().size() != 0 ||
        block.terminator().kind() != TerminatorKind::ReturnLocal ||
        block.terminator().returnLocalOrdinal() != projectedOrdinal) {
      return fault(TranslationFaultKind::EffectMismatch, calleeLirIndex, 1, 1);
    }
  }
  return zc::none;
}

// Indirect-call-with-leaf triple (module-level shape). The caller constructs a
// one-field aggregate and calls a callee that takes a dyn-trait receiver; the
// callee loads the method pointer from the vtable and indirect-calls the leaf,
// which returns a constant. The split aggregate calling convention adds a
// vtable pointer parameter to the callee and renumbers the result local, so
// the triple is validated as a unit rather than per-pair.
struct IndirectCallWithLeafShape final {
  size_t callerIndex = 0;
  size_t calleeIndex = 0;
  size_t leafIndex = 0;
  identity::SemanticTypeId resultType;
  identity::SemanticTypeId aggregateType;
  identity::DefId interface;
  // LIR slot ordinals (the producer numbers them densely).
  static constexpr uint32_t kCallerAggregateOrdinal = 1;
  static constexpr uint32_t kCallerResultOrdinal = 2;
  static constexpr uint32_t kCallerDataPtrOrdinal = 3;
  static constexpr uint32_t kCallerVtablePtrOrdinal = 4;
  static constexpr uint32_t kCalleeDataPtrOrdinal = 1;
  static constexpr uint32_t kCalleeVtablePtrOrdinal = 2;
  static constexpr uint32_t kCalleeResultOrdinal = 3;
  static constexpr uint32_t kCalleeFnPtrOrdinal = 4;
  static constexpr uint32_t kLeafReceiverOrdinal = 1;
};

zc::Maybe<IndirectCallWithLeafShape> matchIndirectCallWithLeaf(
    zc::ArrayPtr<const MirFunction* const> mirFunctions) noexcept {
  if (mirFunctions.size() != 3) { return zc::none; }
  size_t callerIndex = 0;
  size_t calleeIndex = 0;
  size_t leafIndex = 0;
  bool foundCaller = false;
  bool foundCallee = false;
  bool foundLeaf = false;
  for (size_t index = 0; index < mirFunctions.size(); ++index) {
    const MirFunction& fn = *mirFunctions[index];
    bool isLeaf = fn.sourceDefinitionKind == identity::DefinitionKind::Method &&
                  fn.locals.size() == 1 && fn.blocks.size() == 1 &&
                  fn.locals[0].kind == mir::MirLocalKind::Parameter;
    bool isCallee = fn.sourceDefinitionKind == identity::DefinitionKind::Function &&
                    fn.locals.size() == 2 && fn.blocks.size() == 2 &&
                    fn.locals[0].kind == mir::MirLocalKind::Parameter &&
                    fn.locals[1].kind == mir::MirLocalKind::Temporary &&
                    fn.blocks[0].terminator.kind() == mir::MirTerminatorKind::IndirectCall;
    bool isCaller = fn.sourceDefinitionKind == identity::DefinitionKind::Function &&
                    fn.locals.size() == 2 && fn.blocks.size() == 2 &&
                    fn.locals[0].kind == mir::MirLocalKind::UserLocal &&
                    fn.locals[1].kind == mir::MirLocalKind::Temporary &&
                    fn.blocks[0].terminator.kind() == mir::MirTerminatorKind::Call;
    if (isLeaf) {
      if (foundLeaf) { return zc::none; }
      leafIndex = index;
      foundLeaf = true;
    } else if (isCallee) {
      if (foundCallee) { return zc::none; }
      calleeIndex = index;
      foundCallee = true;
    } else if (isCaller) {
      if (foundCaller) { return zc::none; }
      callerIndex = index;
      foundCaller = true;
    }
  }
  if (!foundCaller || !foundCallee || !foundLeaf) { return zc::none; }

  const MirFunction& caller = *mirFunctions[callerIndex];
  const MirFunction& callee = *mirFunctions[calleeIndex];
  const MirFunction& leaf = *mirFunctions[leafIndex];

  // Result types must all agree.
  if (caller.resultType != callee.resultType || callee.resultType != leaf.resultType) {
    return zc::none;
  }

  // Leaf: one block, no statements, constant return.
  const auto& leafBlock = leaf.blocks[0];
  if (leafBlock.id.ordinal() != 1 || leafBlock.statements.size() != 0 ||
      leafBlock.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& leafReturn = leafBlock.terminator.returnValue().value;
  if (leafReturn == zc::none) { return zc::none; }
  {
    const auto& value = ZC_ASSERT_NONNULL(leafReturn);
    if (value.kind() != mir::MirOperandKind::Constant ||
        value.constantValue().type != leaf.resultType) {
      return zc::none;
    }
  }

  // Callee: entry StorageLive(result) + IndirectCall, continuation Return(result).
  const auto& calleeParam = callee.locals[0];
  const auto& calleeResult = callee.locals[1];
  if (calleeResult.type != callee.resultType) { return zc::none; }
  const auto& calleeEntry = callee.blocks[0];
  const auto& calleeCont = callee.blocks[1];
  if (calleeEntry.id.ordinal() != 1 || calleeCont.id.ordinal() != 2 ||
      calleeEntry.statements.size() != 1 ||
      calleeEntry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      calleeEntry.statements[0].storageLocal() != calleeResult.id ||
      calleeEntry.terminator.kind() != mir::MirTerminatorKind::IndirectCall ||
      calleeCont.statements.size() != 0 ||
      calleeCont.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& indirect = calleeEntry.terminator.indirectCallValue();
  if (indirect.arguments.size() != 0 ||
      indirect.effect.kind() != mir::MirCallEffectKind::NoActivation ||
      indirect.destination.local() != calleeResult.id ||
      indirect.destination.projections().size() != 0 || indirect.normalTarget != calleeCont.id ||
      indirect.unwindTarget != zc::none) {
    return zc::none;
  }
  if (indirect.receiver.kind() == mir::MirOperandKind::Constant ||
      indirect.receiver.place().local() != calleeParam.id ||
      indirect.receiver.place().projections().size() != 0) {
    return zc::none;
  }
  {
    const auto& value = ZC_ASSERT_NONNULL(calleeCont.terminator.returnValue().value);
    if (value.kind() == mir::MirOperandKind::Constant || value.place().local() != calleeResult.id ||
        value.place().projections().size() != 0) {
      return zc::none;
    }
  }

  // Caller: entry StorageLive(aggregate) + Assign(aggregate=NominalAggregate) +
  // StorageLive(result) + Call, continuation Return(result).
  const auto& callerAggregate = caller.locals[0];
  const auto& callerResult = caller.locals[1];
  if (callerResult.type != caller.resultType) { return zc::none; }
  const auto& callerEntry = caller.blocks[0];
  const auto& callerCont = caller.blocks[1];
  if (callerEntry.id.ordinal() != 1 || callerCont.id.ordinal() != 2 ||
      callerEntry.statements.size() != 3 ||
      callerEntry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      callerEntry.statements[0].storageLocal() != callerAggregate.id ||
      callerEntry.statements[1].kind() != mir::MirStatementKind::Assign ||
      callerEntry.statements[2].kind() != mir::MirStatementKind::StorageLive ||
      callerEntry.statements[2].storageLocal() != callerResult.id ||
      callerEntry.terminator.kind() != mir::MirTerminatorKind::Call ||
      callerCont.statements.size() != 0 ||
      callerCont.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& assignment = callerEntry.statements[1].assignmentValue();
  if (assignment.destination.local() != callerAggregate.id ||
      assignment.destination.projections().size() != 0 ||
      assignment.value.kind() != mir::MirRvalueKind::NominalAggregate) {
    return zc::none;
  }
  const auto& aggregate = assignment.value.nominalAggregateValue();
  if (aggregate.type != callerAggregate.type || aggregate.elements.size() != 1) { return zc::none; }
  if (aggregate.elements[0].operand.kind() != mir::MirOperandKind::Constant) { return zc::none; }
  const auto& mirCall = callerEntry.terminator.callValue();
  if (mirCall.callee != callee.owner || mirCall.arguments.size() != 1 ||
      mirCall.effect.kind() != mir::MirCallEffectKind::NoActivation ||
      mirCall.destination.local() != callerResult.id ||
      mirCall.destination.projections().size() != 0 || mirCall.normalTarget != callerCont.id ||
      mirCall.unwindTarget != zc::none) {
    return zc::none;
  }
  const auto& callArg = mirCall.arguments[0];
  if (callArg.kind() == mir::MirOperandKind::Constant ||
      callArg.place().local() != callerAggregate.id || callArg.place().projections().size() != 0) {
    return zc::none;
  }
  {
    const auto& value = ZC_ASSERT_NONNULL(callerCont.terminator.returnValue().value);
    if (value.kind() == mir::MirOperandKind::Constant || value.place().local() != callerResult.id ||
        value.place().projections().size() != 0) {
      return zc::none;
    }
  }

  IndirectCallWithLeafShape shape;
  shape.callerIndex = callerIndex;
  shape.calleeIndex = calleeIndex;
  shape.leafIndex = leafIndex;
  shape.resultType = caller.resultType;
  shape.aggregateType = callerAggregate.type;
  shape.interface = indirect.interface;
  return zc::mv(shape);
}

zc::Maybe<TranslationFinding> validateIndirectCallWithLeaf(
    const IndirectCallWithLeafShape& shape, zc::ArrayPtr<const MirFunction* const> mirFunctions,
    const Module& lirModule, const type::SemanticTypeStore& types) noexcept {
  const auto functions = lirModule.functions();
  if (functions.size() != 3) { return fault(TranslationFaultKind::FunctionSetMismatch, 0); }
  auto resultCarrier = integerCarrier(shape.resultType, types);
  if (resultCarrier == zc::none) { return fault(TranslationFaultKind::SlotSetMismatch, 0); }
  const auto resultCarrierValue = ZC_ASSERT_NONNULL(resultCarrier);
  const auto pointerCarrierValue = ValueType::pointer(0);

  // Resolve LIR function indices by MIR owner.
  const MirFunction& caller = *mirFunctions[shape.callerIndex];
  const MirFunction& callee = *mirFunctions[shape.calleeIndex];
  const MirFunction& leaf = *mirFunctions[shape.leafIndex];
  zc::Maybe<uint32_t> lirCaller;
  zc::Maybe<uint32_t> lirCallee;
  zc::Maybe<uint32_t> lirLeaf;
  for (uint32_t index = 0; index < functions.size(); ++index) {
    if (functions[index].owner() == caller.owner) {
      if (lirCaller != zc::none) { return fault(TranslationFaultKind::FunctionSetMismatch, index); }
      lirCaller = index;
    }
    if (functions[index].owner() == callee.owner) {
      if (lirCallee != zc::none) { return fault(TranslationFaultKind::FunctionSetMismatch, index); }
      lirCallee = index;
    }
    if (functions[index].owner() == leaf.owner) {
      if (lirLeaf != zc::none) { return fault(TranslationFaultKind::FunctionSetMismatch, index); }
      lirLeaf = index;
    }
  }
  if (lirCaller == zc::none || lirCallee == zc::none || lirLeaf == zc::none) {
    return fault(TranslationFaultKind::FunctionSetMismatch, 0);
  }
  const uint32_t callerLirIndex = ZC_ASSERT_NONNULL(lirCaller);
  const uint32_t calleeLirIndex = ZC_ASSERT_NONNULL(lirCallee);
  const uint32_t leafLirIndex = ZC_ASSERT_NONNULL(lirLeaf);

  // Vtable: one vtable, ordinal 1, interface matches, sole function index is leaf.
  const auto vtables = lirModule.vtables();
  if (vtables.size() != 1 || vtables[0].ordinal() != 1 ||
      vtables[0].interface() != shape.interface || vtables[0].functionIndices().size() != 1 ||
      vtables[0].functionIndices()[0] != leafLirIndex) {
    return fault(TranslationFaultKind::SlotSetMismatch, 0);
  }

  // Derive the aggregate element carrier from the MIR element's constant type.
  const auto& mirAggregate =
      caller.blocks[0].statements[1].assignmentValue().value.nominalAggregateValue();
  const auto& mirElement = mirAggregate.elements[0].operand;
  auto elementCarrier = integerCarrier(mirElement.constantValue().type, types);
  if (elementCarrier == zc::none) { return fault(TranslationFaultKind::SlotSetMismatch, 0); }
  const auto elementCarrierValue = ZC_ASSERT_NONNULL(elementCarrier);

  // Caller LIR: zero parameters, four body locals (aggregate, result, data ptr,
  // vtable ptr), entry Assign+TakeAddress+VtableAddr then Call, continuation
  // ReturnLocal.
  {
    const Function& lirCallerFn = functions[callerLirIndex];
    if (lirCallerFn.returnCarrier() != resultCarrierValue) {
      return fault(TranslationFaultKind::SlotSetMismatch, callerLirIndex);
    }
    if (lirCallerFn.parameters().size() != 0 || lirCallerFn.locals().size() != 4) {
      return fault(TranslationFaultKind::SlotSetMismatch, callerLirIndex);
    }
    const auto& locals = lirCallerFn.locals();
    if (locals[0].ordinal() != IndirectCallWithLeafShape::kCallerAggregateOrdinal ||
        locals[0].carrier().kind() != ValueTypeKind::Aggregate ||
        locals[0].carrier().aggregateFields().size() != 1 ||
        locals[0].carrier().aggregateFields()[0].type != elementCarrierValue ||
        locals[1].ordinal() != IndirectCallWithLeafShape::kCallerResultOrdinal ||
        locals[1].carrier() != resultCarrierValue ||
        locals[2].ordinal() != IndirectCallWithLeafShape::kCallerDataPtrOrdinal ||
        locals[2].carrier() != pointerCarrierValue ||
        locals[3].ordinal() != IndirectCallWithLeafShape::kCallerVtablePtrOrdinal ||
        locals[3].carrier() != pointerCarrierValue) {
      return fault(TranslationFaultKind::SlotSetMismatch, callerLirIndex);
    }
    if (lirCallerFn.blocks().size() != 2) {
      return fault(TranslationFaultKind::BlockBijectionMismatch, callerLirIndex);
    }
    const auto& entry = lirCallerFn.blocks()[0];
    const auto& cont = lirCallerFn.blocks()[1];
    if (entry.id().ordinal() != 1 || cont.id().ordinal() != 2 || entry.statements().size() != 3 ||
        cont.statements().size() != 0) {
      return fault(TranslationFaultKind::BlockBijectionMismatch, callerLirIndex);
    }
    // Entry statements: Assign(aggregate=aggregateConst), TakeAddress(dataPtr,
    // aggregate), VtableAddr(vtablePtr, 1).
    const auto& stmts = entry.statements();
    if (stmts[0].kind() != StatementKind::Assign ||
        stmts[0].destinationOrdinal() != IndirectCallWithLeafShape::kCallerAggregateOrdinal ||
        !stmts[0].value().isConstant() || !stmts[0].value().isAggregateConstant()) {
      return fault(TranslationFaultKind::EffectMismatch, callerLirIndex, 1, 1, 1);
    }
    const auto& aggConst = stmts[0].value().aggregateConstantValue();
    if (aggConst.carrier() != locals[0].carrier() || aggConst.fields().size() != 1 ||
        !sameConstant(aggConst.fields()[0], mirElement, elementCarrierValue)) {
      return fault(TranslationFaultKind::ConstantMismatch, callerLirIndex, 1, 1, 1);
    }
    if (stmts[1].kind() != StatementKind::TakeAddress ||
        stmts[1].destinationOrdinal() != IndirectCallWithLeafShape::kCallerDataPtrOrdinal ||
        stmts[1].sourceOrdinal() != IndirectCallWithLeafShape::kCallerAggregateOrdinal) {
      return fault(TranslationFaultKind::EffectMismatch, callerLirIndex, 1, 1, 2);
    }
    if (stmts[2].kind() != StatementKind::VtableAddr ||
        stmts[2].destinationOrdinal() != IndirectCallWithLeafShape::kCallerVtablePtrOrdinal ||
        stmts[2].vtableOrdinal() != 1) {
      return fault(TranslationFaultKind::EffectMismatch, callerLirIndex, 1, 1, 3);
    }
    // Entry terminator: Call(callee, dest=result, args=[dataPtr, vtablePtr],
    // target=cont).
    const auto& term = entry.terminator();
    if (term.kind() != TerminatorKind::Call || !term.hasCallDestination() ||
        term.calleeIndex() >= functions.size() ||
        functions[term.calleeIndex()].owner() != callee.owner ||
        term.callDestinationOrdinal() != IndirectCallWithLeafShape::kCallerResultOrdinal ||
        term.callNormalTarget() != cont.id() || term.callArguments().size() != 2 ||
        term.callArguments()[0].isConstant() ||
        term.callArguments()[0].localOrdinal() !=
            IndirectCallWithLeafShape::kCallerDataPtrOrdinal ||
        term.callArguments()[1].isConstant() ||
        term.callArguments()[1].localOrdinal() !=
            IndirectCallWithLeafShape::kCallerVtablePtrOrdinal) {
      return fault(TranslationFaultKind::EffectMismatch, callerLirIndex, 1, 1);
    }
    // Continuation terminator: ReturnLocal(result).
    if (cont.terminator().kind() != TerminatorKind::ReturnLocal ||
        cont.terminator().returnLocalOrdinal() != IndirectCallWithLeafShape::kCallerResultOrdinal) {
      return fault(TranslationFaultKind::EffectMismatch, callerLirIndex, 2, 2);
    }
  }

  // Callee LIR: two pointer parameters (data ptr, vtable ptr), two body locals
  // (result, fn ptr), entry VtableSlotLoad then IndirectCall, continuation
  // ReturnLocal.
  {
    const Function& lirCalleeFn = functions[calleeLirIndex];
    if (lirCalleeFn.returnCarrier() != resultCarrierValue) {
      return fault(TranslationFaultKind::SlotSetMismatch, calleeLirIndex);
    }
    if (lirCalleeFn.parameters().size() != 2 || lirCalleeFn.locals().size() != 2) {
      return fault(TranslationFaultKind::SlotSetMismatch, calleeLirIndex);
    }
    const auto& params = lirCalleeFn.parameters();
    const auto& locals = lirCalleeFn.locals();
    if (params[0].ordinal() != IndirectCallWithLeafShape::kCalleeDataPtrOrdinal ||
        params[0].carrier() != pointerCarrierValue ||
        params[1].ordinal() != IndirectCallWithLeafShape::kCalleeVtablePtrOrdinal ||
        params[1].carrier() != pointerCarrierValue ||
        locals[0].ordinal() != IndirectCallWithLeafShape::kCalleeResultOrdinal ||
        locals[0].carrier() != resultCarrierValue ||
        locals[1].ordinal() != IndirectCallWithLeafShape::kCalleeFnPtrOrdinal ||
        locals[1].carrier() != pointerCarrierValue) {
      return fault(TranslationFaultKind::SlotSetMismatch, calleeLirIndex);
    }
    if (lirCalleeFn.blocks().size() != 2) {
      return fault(TranslationFaultKind::BlockBijectionMismatch, calleeLirIndex);
    }
    const auto& entry = lirCalleeFn.blocks()[0];
    const auto& cont = lirCalleeFn.blocks()[1];
    if (entry.id().ordinal() != 1 || cont.id().ordinal() != 2 || entry.statements().size() != 1 ||
        cont.statements().size() != 0) {
      return fault(TranslationFaultKind::BlockBijectionMismatch, calleeLirIndex);
    }
    // Entry statement: VtableSlotLoad(fnPtr, vtablePtr, slot 0).
    const auto& stmt = entry.statements()[0];
    if (stmt.kind() != StatementKind::VtableSlotLoad ||
        stmt.destinationOrdinal() != IndirectCallWithLeafShape::kCalleeFnPtrOrdinal ||
        stmt.basePointerOrdinal() != IndirectCallWithLeafShape::kCalleeVtablePtrOrdinal ||
        stmt.vtableSlotIndex() != 0) {
      return fault(TranslationFaultKind::EffectMismatch, calleeLirIndex, 1, 1, 1);
    }
    // Entry terminator: IndirectCall(fnPtr, dest=result, args=[dataPtr],
    // returnCarrier=result, paramCarriers=[pointer], target=cont).
    const auto& term = entry.terminator();
    if (term.kind() != TerminatorKind::IndirectCall ||
        term.indirectCallPointerOrdinal() != IndirectCallWithLeafShape::kCalleeFnPtrOrdinal ||
        term.callDestinationOrdinal() != IndirectCallWithLeafShape::kCalleeResultOrdinal ||
        term.callNormalTarget() != cont.id() || term.callArguments().size() != 1 ||
        term.indirectReturnCarrier() != resultCarrierValue ||
        term.indirectParameterCarriers().size() != 1 ||
        term.indirectParameterCarriers()[0] != pointerCarrierValue) {
      return fault(TranslationFaultKind::EffectMismatch, calleeLirIndex, 1, 1);
    }
    if (term.callArguments()[0].isConstant() ||
        term.callArguments()[0].localOrdinal() !=
            IndirectCallWithLeafShape::kCalleeDataPtrOrdinal) {
      return fault(TranslationFaultKind::EffectMismatch, calleeLirIndex, 1, 1);
    }
    // Continuation terminator: ReturnLocal(result).
    if (cont.terminator().kind() != TerminatorKind::ReturnLocal ||
        cont.terminator().returnLocalOrdinal() != IndirectCallWithLeafShape::kCalleeResultOrdinal) {
      return fault(TranslationFaultKind::EffectMismatch, calleeLirIndex, 2, 2);
    }
  }

  // Leaf LIR: one pointer parameter, no body locals, single block returning the
  // integer constant the MIR leaf returns.
  {
    const Function& lirLeafFn = functions[leafLirIndex];
    if (lirLeafFn.returnCarrier() != resultCarrierValue) {
      return fault(TranslationFaultKind::SlotSetMismatch, leafLirIndex);
    }
    if (lirLeafFn.parameters().size() != 1 || lirLeafFn.locals().size() != 0) {
      return fault(TranslationFaultKind::SlotSetMismatch, leafLirIndex);
    }
    if (lirLeafFn.parameters()[0].ordinal() != IndirectCallWithLeafShape::kLeafReceiverOrdinal ||
        lirLeafFn.parameters()[0].carrier() != pointerCarrierValue) {
      return fault(TranslationFaultKind::SlotSetMismatch, leafLirIndex);
    }
    if (lirLeafFn.blocks().size() != 1) {
      return fault(TranslationFaultKind::BlockBijectionMismatch, leafLirIndex);
    }
    const auto& block = lirLeafFn.blocks()[0];
    if (block.id().ordinal() != 1 || block.statements().size() != 0 ||
        block.terminator().kind() != TerminatorKind::ReturnInteger) {
      return fault(TranslationFaultKind::EffectMismatch, leafLirIndex, 1, 1);
    }
    const auto& mirLeafReturn = ZC_ASSERT_NONNULL(leaf.blocks[0].terminator.returnValue().value);
    if (!sameConstant(Operand::constant(block.terminator().returnIntegerValue()), mirLeafReturn,
                      resultCarrierValue)) {
      return fault(TranslationFaultKind::ConstantMismatch, leafLirIndex, 1, 1);
    }
  }

  return zc::none;
}

// Stored-erase caller/callee/leaf triple: the caller constructs a concrete
// aggregate, erases it into an existential local, and passes the existential
// to a dyn-parameter callee that dispatches through a vtable to a constant
// leaf. The caller has three MIR locals (existential, concrete, result) and
// five entry statements; the LIR caller has six locals and seven entry
// statements because the Erase rvalue lowers to a TakeAddress + VtableAddr +
// InsertField + InsertField + ExtractField + ExtractField sequence.
struct ErasedAggregateCallShape final {
  size_t callerIndex = 0;
  size_t calleeIndex = 0;
  size_t leafIndex = 0;
  identity::SemanticTypeId resultType;
  identity::SemanticTypeId concreteType;
  identity::DefId interface;
  // LIR slot ordinals (the producer numbers them densely).
  static constexpr uint32_t kCallerExistentialOrdinal = 1;
  static constexpr uint32_t kCallerConcreteOrdinal = 2;
  static constexpr uint32_t kCallerResultOrdinal = 3;
  static constexpr uint32_t kCallerDataPtrOrdinal = 4;
  static constexpr uint32_t kCallerVtablePtrOrdinal = 5;
  static constexpr uint32_t kCallerExistentialTmpOrdinal = 6;
  static constexpr uint32_t kCalleeDataPtrOrdinal = 1;
  static constexpr uint32_t kCalleeVtablePtrOrdinal = 2;
  static constexpr uint32_t kCalleeResultOrdinal = 3;
  static constexpr uint32_t kCalleeFnPtrOrdinal = 4;
  static constexpr uint32_t kLeafReceiverOrdinal = 1;
};

zc::Maybe<ErasedAggregateCallShape> matchErasedAggregateCall(
    zc::ArrayPtr<const MirFunction* const> mirFunctions) noexcept {
  if (mirFunctions.size() != 3) { return zc::none; }
  size_t callerIndex = 0;
  size_t calleeIndex = 0;
  size_t leafIndex = 0;
  bool foundCaller = false;
  bool foundCallee = false;
  bool foundLeaf = false;
  for (size_t index = 0; index < mirFunctions.size(); ++index) {
    const MirFunction& fn = *mirFunctions[index];
    bool isLeaf = fn.sourceDefinitionKind == identity::DefinitionKind::Method &&
                  fn.locals.size() == 1 && fn.blocks.size() == 1 &&
                  fn.locals[0].kind == mir::MirLocalKind::Parameter;
    bool isCallee = fn.sourceDefinitionKind == identity::DefinitionKind::Function &&
                    fn.locals.size() == 2 && fn.blocks.size() == 2 &&
                    fn.locals[0].kind == mir::MirLocalKind::Parameter &&
                    fn.locals[1].kind == mir::MirLocalKind::Temporary &&
                    fn.blocks[0].terminator.kind() == mir::MirTerminatorKind::IndirectCall;
    bool isCaller = fn.sourceDefinitionKind == identity::DefinitionKind::Function &&
                    fn.locals.size() == 3 && fn.blocks.size() == 2 &&
                    fn.locals[0].kind == mir::MirLocalKind::UserLocal &&
                    fn.locals[1].kind == mir::MirLocalKind::Temporary &&
                    fn.locals[2].kind == mir::MirLocalKind::Temporary &&
                    fn.blocks[0].terminator.kind() == mir::MirTerminatorKind::Call;
    if (isLeaf) {
      if (foundLeaf) { return zc::none; }
      leafIndex = index;
      foundLeaf = true;
    } else if (isCallee) {
      if (foundCallee) { return zc::none; }
      calleeIndex = index;
      foundCallee = true;
    } else if (isCaller) {
      if (foundCaller) { return zc::none; }
      callerIndex = index;
      foundCaller = true;
    }
  }
  if (!foundCaller || !foundCallee || !foundLeaf) { return zc::none; }

  const MirFunction& caller = *mirFunctions[callerIndex];
  const MirFunction& callee = *mirFunctions[calleeIndex];
  const MirFunction& leaf = *mirFunctions[leafIndex];

  // Result types must all agree.
  if (caller.resultType != callee.resultType || callee.resultType != leaf.resultType) {
    return zc::none;
  }

  // Leaf: one block, no statements, constant return.
  const auto& leafBlock = leaf.blocks[0];
  if (leafBlock.id.ordinal() != 1 || leafBlock.statements.size() != 0 ||
      leafBlock.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& leafReturn = leafBlock.terminator.returnValue().value;
  if (leafReturn == zc::none) { return zc::none; }
  {
    const auto& value = ZC_ASSERT_NONNULL(leafReturn);
    if (value.kind() != mir::MirOperandKind::Constant ||
        value.constantValue().type != leaf.resultType) {
      return zc::none;
    }
  }

  // Callee: entry StorageLive(result) + IndirectCall, continuation Return(result).
  const auto& calleeParam = callee.locals[0];
  const auto& calleeResult = callee.locals[1];
  if (calleeResult.type != callee.resultType) { return zc::none; }
  const auto& calleeEntry = callee.blocks[0];
  const auto& calleeCont = callee.blocks[1];
  if (calleeEntry.id.ordinal() != 1 || calleeCont.id.ordinal() != 2 ||
      calleeEntry.statements.size() != 1 ||
      calleeEntry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      calleeEntry.statements[0].storageLocal() != calleeResult.id ||
      calleeEntry.terminator.kind() != mir::MirTerminatorKind::IndirectCall ||
      calleeCont.statements.size() != 0 ||
      calleeCont.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& indirect = calleeEntry.terminator.indirectCallValue();
  if (indirect.arguments.size() != 0 ||
      indirect.effect.kind() != mir::MirCallEffectKind::NoActivation ||
      indirect.destination.local() != calleeResult.id ||
      indirect.destination.projections().size() != 0 || indirect.normalTarget != calleeCont.id ||
      indirect.unwindTarget != zc::none) {
    return zc::none;
  }
  if (indirect.receiver.kind() == mir::MirOperandKind::Constant ||
      indirect.receiver.place().local() != calleeParam.id ||
      indirect.receiver.place().projections().size() != 0) {
    return zc::none;
  }
  {
    const auto& value = ZC_ASSERT_NONNULL(calleeCont.terminator.returnValue().value);
    if (value.kind() == mir::MirOperandKind::Constant || value.place().local() != calleeResult.id ||
        value.place().projections().size() != 0) {
      return zc::none;
    }
  }

  // Caller: entry StorageLive(concrete) + Assign(concrete=NominalAggregate) +
  // StorageLive(existential) + Assign(existential=Erase(concrete)) +
  // StorageLive(result) + Call, continuation Return(result).
  const auto& callerExistential = caller.locals[0];
  const auto& callerConcrete = caller.locals[1];
  const auto& callerResult = caller.locals[2];
  if (callerResult.type != caller.resultType) { return zc::none; }
  const auto& callerEntry = caller.blocks[0];
  const auto& callerCont = caller.blocks[1];
  if (callerEntry.id.ordinal() != 1 || callerCont.id.ordinal() != 2 ||
      callerEntry.statements.size() != 5 ||
      callerEntry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      callerEntry.statements[0].storageLocal() != callerConcrete.id ||
      callerEntry.statements[1].kind() != mir::MirStatementKind::Assign ||
      callerEntry.statements[2].kind() != mir::MirStatementKind::StorageLive ||
      callerEntry.statements[2].storageLocal() != callerExistential.id ||
      callerEntry.statements[3].kind() != mir::MirStatementKind::Assign ||
      callerEntry.statements[4].kind() != mir::MirStatementKind::StorageLive ||
      callerEntry.statements[4].storageLocal() != callerResult.id ||
      callerEntry.terminator.kind() != mir::MirTerminatorKind::Call ||
      callerCont.statements.size() != 0 ||
      callerCont.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& concreteAssign = callerEntry.statements[1].assignmentValue();
  if (concreteAssign.destination.local() != callerConcrete.id ||
      concreteAssign.destination.projections().size() != 0 ||
      concreteAssign.value.kind() != mir::MirRvalueKind::NominalAggregate) {
    return zc::none;
  }
  const auto& aggregate = concreteAssign.value.nominalAggregateValue();
  if (aggregate.type != callerConcrete.type || aggregate.elements.size() != 1) { return zc::none; }
  if (aggregate.elements[0].operand.kind() != mir::MirOperandKind::Constant) { return zc::none; }
  const auto& eraseAssign = callerEntry.statements[3].assignmentValue();
  if (eraseAssign.destination.local() != callerExistential.id ||
      eraseAssign.destination.projections().size() != 0 ||
      eraseAssign.value.kind() != mir::MirRvalueKind::Erase) {
    return zc::none;
  }
  const auto& erase = eraseAssign.value.eraseValue();
  if (erase.sourceType != callerConcrete.type || erase.targetType != callerExistential.type ||
      erase.source.kind() != mir::MirOperandKind::Move ||
      erase.source.place().local() != callerConcrete.id ||
      erase.source.place().projections().size() != 0) {
    return zc::none;
  }
  const auto& mirCall = callerEntry.terminator.callValue();
  if (mirCall.callee != callee.owner || mirCall.arguments.size() != 1 ||
      mirCall.effect.kind() != mir::MirCallEffectKind::NoActivation ||
      mirCall.destination.local() != callerResult.id ||
      mirCall.destination.projections().size() != 0 || mirCall.normalTarget != callerCont.id ||
      mirCall.unwindTarget != zc::none) {
    return zc::none;
  }
  const auto& callArg = mirCall.arguments[0];
  if (callArg.kind() == mir::MirOperandKind::Constant ||
      callArg.place().local() != callerExistential.id ||
      callArg.place().projections().size() != 0) {
    return zc::none;
  }
  {
    const auto& value = ZC_ASSERT_NONNULL(callerCont.terminator.returnValue().value);
    if (value.kind() == mir::MirOperandKind::Constant || value.place().local() != callerResult.id ||
        value.place().projections().size() != 0) {
      return zc::none;
    }
  }

  ErasedAggregateCallShape shape;
  shape.callerIndex = callerIndex;
  shape.calleeIndex = calleeIndex;
  shape.leafIndex = leafIndex;
  shape.resultType = caller.resultType;
  shape.concreteType = callerConcrete.type;
  shape.interface = indirect.interface;
  return zc::mv(shape);
}

zc::Maybe<TranslationFinding> validateErasedAggregateCall(
    const ErasedAggregateCallShape& shape, zc::ArrayPtr<const MirFunction* const> mirFunctions,
    const Module& lirModule, const type::SemanticTypeStore& types) noexcept {
  const auto functions = lirModule.functions();
  if (functions.size() != 3) { return fault(TranslationFaultKind::FunctionSetMismatch, 0); }
  auto resultCarrier = integerCarrier(shape.resultType, types);
  if (resultCarrier == zc::none) { return fault(TranslationFaultKind::SlotSetMismatch, 0); }
  const auto resultCarrierValue = ZC_ASSERT_NONNULL(resultCarrier);
  const auto pointerCarrierValue = ValueType::pointer(0);

  // Resolve LIR function indices by MIR owner.
  const MirFunction& caller = *mirFunctions[shape.callerIndex];
  const MirFunction& callee = *mirFunctions[shape.calleeIndex];
  const MirFunction& leaf = *mirFunctions[shape.leafIndex];
  zc::Maybe<uint32_t> lirCaller;
  zc::Maybe<uint32_t> lirCallee;
  zc::Maybe<uint32_t> lirLeaf;
  for (uint32_t index = 0; index < functions.size(); ++index) {
    if (functions[index].owner() == caller.owner) {
      if (lirCaller != zc::none) { return fault(TranslationFaultKind::FunctionSetMismatch, index); }
      lirCaller = index;
    }
    if (functions[index].owner() == callee.owner) {
      if (lirCallee != zc::none) { return fault(TranslationFaultKind::FunctionSetMismatch, index); }
      lirCallee = index;
    }
    if (functions[index].owner() == leaf.owner) {
      if (lirLeaf != zc::none) { return fault(TranslationFaultKind::FunctionSetMismatch, index); }
      lirLeaf = index;
    }
  }
  if (lirCaller == zc::none || lirCallee == zc::none || lirLeaf == zc::none) {
    return fault(TranslationFaultKind::FunctionSetMismatch, 0);
  }
  const uint32_t callerLirIndex = ZC_ASSERT_NONNULL(lirCaller);
  const uint32_t calleeLirIndex = ZC_ASSERT_NONNULL(lirCallee);
  const uint32_t leafLirIndex = ZC_ASSERT_NONNULL(lirLeaf);

  // Vtable: one vtable, ordinal 1, interface matches, sole function index is leaf.
  const auto vtables = lirModule.vtables();
  if (vtables.size() != 1 || vtables[0].ordinal() != 1 ||
      vtables[0].interface() != shape.interface || vtables[0].functionIndices().size() != 1 ||
      vtables[0].functionIndices()[0] != leafLirIndex) {
    return fault(TranslationFaultKind::SlotSetMismatch, 0);
  }

  // Derive the aggregate element carrier from the MIR element's constant type.
  const auto& mirAggregate =
      caller.blocks[0].statements[1].assignmentValue().value.nominalAggregateValue();
  const auto& mirElement = mirAggregate.elements[0].operand;
  auto elementCarrier = integerCarrier(mirElement.constantValue().type, types);
  if (elementCarrier == zc::none) { return fault(TranslationFaultKind::SlotSetMismatch, 0); }
  const auto elementCarrierValue = ZC_ASSERT_NONNULL(elementCarrier);

  // Caller LIR: zero parameters, six body locals (existential, concrete,
  // result, data ptr, vtable ptr, existential tmp), entry
  // Assign+TakeAddress+VtableAddr+InsertField+InsertField+ExtractField+
  // ExtractField then Call, continuation ReturnLocal.
  {
    const Function& lirCallerFn = functions[callerLirIndex];
    if (lirCallerFn.returnCarrier() != resultCarrierValue) {
      return fault(TranslationFaultKind::SlotSetMismatch, callerLirIndex);
    }
    if (lirCallerFn.parameters().size() != 0 || lirCallerFn.locals().size() != 6) {
      return fault(TranslationFaultKind::SlotSetMismatch, callerLirIndex);
    }
    const auto& locals = lirCallerFn.locals();
    if (locals[0].ordinal() != ErasedAggregateCallShape::kCallerExistentialOrdinal ||
        locals[0].carrier().kind() != ValueTypeKind::Aggregate ||
        locals[0].carrier().aggregateFields().size() != 2 ||
        locals[1].ordinal() != ErasedAggregateCallShape::kCallerConcreteOrdinal ||
        locals[1].carrier().kind() != ValueTypeKind::Aggregate ||
        locals[1].carrier().aggregateFields().size() != 1 ||
        locals[1].carrier().aggregateFields()[0].type != elementCarrierValue ||
        locals[2].ordinal() != ErasedAggregateCallShape::kCallerResultOrdinal ||
        locals[2].carrier() != resultCarrierValue ||
        locals[3].ordinal() != ErasedAggregateCallShape::kCallerDataPtrOrdinal ||
        locals[3].carrier() != pointerCarrierValue ||
        locals[4].ordinal() != ErasedAggregateCallShape::kCallerVtablePtrOrdinal ||
        locals[4].carrier() != pointerCarrierValue ||
        locals[5].ordinal() != ErasedAggregateCallShape::kCallerExistentialTmpOrdinal ||
        locals[5].carrier().kind() != ValueTypeKind::Aggregate ||
        locals[5].carrier().aggregateFields().size() != 2) {
      return fault(TranslationFaultKind::SlotSetMismatch, callerLirIndex);
    }
    if (lirCallerFn.blocks().size() != 2) {
      return fault(TranslationFaultKind::BlockBijectionMismatch, callerLirIndex);
    }
    const auto& entry = lirCallerFn.blocks()[0];
    const auto& cont = lirCallerFn.blocks()[1];
    if (entry.id().ordinal() != 1 || cont.id().ordinal() != 2 || entry.statements().size() != 7 ||
        cont.statements().size() != 0) {
      return fault(TranslationFaultKind::BlockBijectionMismatch, callerLirIndex);
    }
    const auto& stmts = entry.statements();
    // Statement 0: Assign(concrete=aggregateConst).
    if (stmts[0].kind() != StatementKind::Assign ||
        stmts[0].destinationOrdinal() != ErasedAggregateCallShape::kCallerConcreteOrdinal ||
        !stmts[0].value().isConstant() || !stmts[0].value().isAggregateConstant()) {
      return fault(TranslationFaultKind::EffectMismatch, callerLirIndex, 1, 1, 1);
    }
    const auto& aggConst = stmts[0].value().aggregateConstantValue();
    if (aggConst.carrier() != locals[1].carrier() || aggConst.fields().size() != 1 ||
        !sameConstant(aggConst.fields()[0], mirElement, elementCarrierValue)) {
      return fault(TranslationFaultKind::ConstantMismatch, callerLirIndex, 1, 1, 1);
    }
    // Statement 1: TakeAddress(dataPtr, concrete).
    if (stmts[1].kind() != StatementKind::TakeAddress ||
        stmts[1].destinationOrdinal() != ErasedAggregateCallShape::kCallerDataPtrOrdinal ||
        stmts[1].sourceOrdinal() != ErasedAggregateCallShape::kCallerConcreteOrdinal) {
      return fault(TranslationFaultKind::EffectMismatch, callerLirIndex, 1, 1, 2);
    }
    // Statement 2: VtableAddr(vtablePtr, 1).
    if (stmts[2].kind() != StatementKind::VtableAddr ||
        stmts[2].destinationOrdinal() != ErasedAggregateCallShape::kCallerVtablePtrOrdinal ||
        stmts[2].vtableOrdinal() != 1) {
      return fault(TranslationFaultKind::EffectMismatch, callerLirIndex, 1, 1, 3);
    }
    // Statement 3: InsertField(existentialTmp, existential, 0, dataPtr).
    if (stmts[3].kind() != StatementKind::InsertField ||
        stmts[3].destinationOrdinal() != ErasedAggregateCallShape::kCallerExistentialTmpOrdinal ||
        stmts[3].sourceOrdinal() != ErasedAggregateCallShape::kCallerExistentialOrdinal ||
        stmts[3].fieldIndex() != 0 || stmts[3].storedValue().isConstant() ||
        stmts[3].storedValue().localOrdinal() != ErasedAggregateCallShape::kCallerDataPtrOrdinal) {
      return fault(TranslationFaultKind::EffectMismatch, callerLirIndex, 1, 1, 4);
    }
    // Statement 4: InsertField(existential, existentialTmp, 1, vtablePtr).
    if (stmts[4].kind() != StatementKind::InsertField ||
        stmts[4].destinationOrdinal() != ErasedAggregateCallShape::kCallerExistentialOrdinal ||
        stmts[4].sourceOrdinal() != ErasedAggregateCallShape::kCallerExistentialTmpOrdinal ||
        stmts[4].fieldIndex() != 1 || stmts[4].storedValue().isConstant() ||
        stmts[4].storedValue().localOrdinal() !=
            ErasedAggregateCallShape::kCallerVtablePtrOrdinal) {
      return fault(TranslationFaultKind::EffectMismatch, callerLirIndex, 1, 1, 5);
    }
    // Statement 5: ExtractField(dataPtr, existential, 0).
    if (stmts[5].kind() != StatementKind::ExtractField ||
        stmts[5].destinationOrdinal() != ErasedAggregateCallShape::kCallerDataPtrOrdinal ||
        stmts[5].sourceOrdinal() != ErasedAggregateCallShape::kCallerExistentialOrdinal ||
        stmts[5].fieldIndex() != 0) {
      return fault(TranslationFaultKind::EffectMismatch, callerLirIndex, 1, 1, 6);
    }
    // Statement 6: ExtractField(vtablePtr, existential, 1).
    if (stmts[6].kind() != StatementKind::ExtractField ||
        stmts[6].destinationOrdinal() != ErasedAggregateCallShape::kCallerVtablePtrOrdinal ||
        stmts[6].sourceOrdinal() != ErasedAggregateCallShape::kCallerExistentialOrdinal ||
        stmts[6].fieldIndex() != 1) {
      return fault(TranslationFaultKind::EffectMismatch, callerLirIndex, 1, 1, 7);
    }
    // Entry terminator: Call(callee, dest=result, args=[dataPtr, vtablePtr],
    // target=cont).
    const auto& term = entry.terminator();
    if (term.kind() != TerminatorKind::Call || !term.hasCallDestination() ||
        term.calleeIndex() >= functions.size() ||
        functions[term.calleeIndex()].owner() != callee.owner ||
        term.callDestinationOrdinal() != ErasedAggregateCallShape::kCallerResultOrdinal ||
        term.callNormalTarget() != cont.id() || term.callArguments().size() != 2 ||
        term.callArguments()[0].isConstant() ||
        term.callArguments()[0].localOrdinal() != ErasedAggregateCallShape::kCallerDataPtrOrdinal ||
        term.callArguments()[1].isConstant() ||
        term.callArguments()[1].localOrdinal() !=
            ErasedAggregateCallShape::kCallerVtablePtrOrdinal) {
      return fault(TranslationFaultKind::EffectMismatch, callerLirIndex, 1, 1);
    }
    // Continuation terminator: ReturnLocal(result).
    if (cont.terminator().kind() != TerminatorKind::ReturnLocal ||
        cont.terminator().returnLocalOrdinal() != ErasedAggregateCallShape::kCallerResultOrdinal) {
      return fault(TranslationFaultKind::EffectMismatch, callerLirIndex, 2, 2);
    }
  }

  // Callee LIR: two pointer parameters (data ptr, vtable ptr), two body locals
  // (result, fn ptr), entry VtableSlotLoad then IndirectCall, continuation
  // ReturnLocal.
  {
    const Function& lirCalleeFn = functions[calleeLirIndex];
    if (lirCalleeFn.returnCarrier() != resultCarrierValue) {
      return fault(TranslationFaultKind::SlotSetMismatch, calleeLirIndex);
    }
    if (lirCalleeFn.parameters().size() != 2 || lirCalleeFn.locals().size() != 2) {
      return fault(TranslationFaultKind::SlotSetMismatch, calleeLirIndex);
    }
    const auto& params = lirCalleeFn.parameters();
    const auto& locals = lirCalleeFn.locals();
    if (params[0].ordinal() != ErasedAggregateCallShape::kCalleeDataPtrOrdinal ||
        params[0].carrier() != pointerCarrierValue ||
        params[1].ordinal() != ErasedAggregateCallShape::kCalleeVtablePtrOrdinal ||
        params[1].carrier() != pointerCarrierValue ||
        locals[0].ordinal() != ErasedAggregateCallShape::kCalleeResultOrdinal ||
        locals[0].carrier() != resultCarrierValue ||
        locals[1].ordinal() != ErasedAggregateCallShape::kCalleeFnPtrOrdinal ||
        locals[1].carrier() != pointerCarrierValue) {
      return fault(TranslationFaultKind::SlotSetMismatch, calleeLirIndex);
    }
    if (lirCalleeFn.blocks().size() != 2) {
      return fault(TranslationFaultKind::BlockBijectionMismatch, calleeLirIndex);
    }
    const auto& entry = lirCalleeFn.blocks()[0];
    const auto& cont = lirCalleeFn.blocks()[1];
    if (entry.id().ordinal() != 1 || cont.id().ordinal() != 2 || entry.statements().size() != 1 ||
        cont.statements().size() != 0) {
      return fault(TranslationFaultKind::BlockBijectionMismatch, calleeLirIndex);
    }
    // Entry statement: VtableSlotLoad(fnPtr, vtablePtr, slot 0).
    const auto& stmt = entry.statements()[0];
    if (stmt.kind() != StatementKind::VtableSlotLoad ||
        stmt.destinationOrdinal() != ErasedAggregateCallShape::kCalleeFnPtrOrdinal ||
        stmt.basePointerOrdinal() != ErasedAggregateCallShape::kCalleeVtablePtrOrdinal ||
        stmt.vtableSlotIndex() != 0) {
      return fault(TranslationFaultKind::EffectMismatch, calleeLirIndex, 1, 1, 1);
    }
    // Entry terminator: IndirectCall(fnPtr, dest=result, args=[dataPtr],
    // returnCarrier=result, paramCarriers=[pointer], target=cont).
    const auto& term = entry.terminator();
    if (term.kind() != TerminatorKind::IndirectCall ||
        term.indirectCallPointerOrdinal() != ErasedAggregateCallShape::kCalleeFnPtrOrdinal ||
        term.callDestinationOrdinal() != ErasedAggregateCallShape::kCalleeResultOrdinal ||
        term.callNormalTarget() != cont.id() || term.callArguments().size() != 1 ||
        term.indirectReturnCarrier() != resultCarrierValue ||
        term.indirectParameterCarriers().size() != 1 ||
        term.indirectParameterCarriers()[0] != pointerCarrierValue) {
      return fault(TranslationFaultKind::EffectMismatch, calleeLirIndex, 1, 1);
    }
    if (term.callArguments()[0].isConstant() ||
        term.callArguments()[0].localOrdinal() != ErasedAggregateCallShape::kCalleeDataPtrOrdinal) {
      return fault(TranslationFaultKind::EffectMismatch, calleeLirIndex, 1, 1);
    }
    // Continuation terminator: ReturnLocal(result).
    if (cont.terminator().kind() != TerminatorKind::ReturnLocal ||
        cont.terminator().returnLocalOrdinal() != ErasedAggregateCallShape::kCalleeResultOrdinal) {
      return fault(TranslationFaultKind::EffectMismatch, calleeLirIndex, 2, 2);
    }
  }

  // Leaf LIR: one pointer parameter, no body locals, single block returning the
  // integer constant the MIR leaf returns.
  {
    const Function& lirLeafFn = functions[leafLirIndex];
    if (lirLeafFn.returnCarrier() != resultCarrierValue) {
      return fault(TranslationFaultKind::SlotSetMismatch, leafLirIndex);
    }
    if (lirLeafFn.parameters().size() != 1 || lirLeafFn.locals().size() != 0) {
      return fault(TranslationFaultKind::SlotSetMismatch, leafLirIndex);
    }
    if (lirLeafFn.parameters()[0].ordinal() != ErasedAggregateCallShape::kLeafReceiverOrdinal ||
        lirLeafFn.parameters()[0].carrier() != pointerCarrierValue) {
      return fault(TranslationFaultKind::SlotSetMismatch, leafLirIndex);
    }
    if (lirLeafFn.blocks().size() != 1) {
      return fault(TranslationFaultKind::BlockBijectionMismatch, leafLirIndex);
    }
    const auto& block = lirLeafFn.blocks()[0];
    if (block.id().ordinal() != 1 || block.statements().size() != 0 ||
        block.terminator().kind() != TerminatorKind::ReturnInteger) {
      return fault(TranslationFaultKind::EffectMismatch, leafLirIndex, 1, 1);
    }
    const auto& mirLeafReturn = ZC_ASSERT_NONNULL(leaf.blocks[0].terminator.returnValue().value);
    if (!sameConstant(Operand::constant(block.terminator().returnIntegerValue()), mirLeafReturn,
                      resultCarrierValue)) {
      return fault(TranslationFaultKind::ConstantMismatch, leafLirIndex, 1, 1);
    }
  }

  return zc::none;
}

// Existential return: a function returns a concrete-to-dyn erasure. The MIR
// shape has two Temporary locals (concrete aggregate, existential result), one
// block, four statements (StorageLive, Assign(NominalAggregate), StorageLive,
// Assign(Erase)), and a Return terminator. The LIR shape promotes the concrete
// aggregate to a module-level global constant so the returned data pointer
// outlives the call, then builds the fat pointer {dataPtr, vtablePtr} with
// GlobalAddr + VtableAddr + InsertField + InsertField and returns it with
// ReturnLocal. The 1:1 MIR-to-LIR statement correspondence does not hold, so
// this shape is validated as a unit before validatePair.
struct ExistentialReturnShape final {
  identity::SemanticTypeId concreteType;
  identity::SemanticTypeId existentialType;
  // LIR slot ordinals (the producer numbers them densely).
  static constexpr uint32_t kExistentialOrdinal = 1;
  static constexpr uint32_t kDataPtrOrdinal = 2;
  static constexpr uint32_t kVtablePtrOrdinal = 3;
  static constexpr uint32_t kExistentialTmpOrdinal = 4;
};

zc::Maybe<ExistentialReturnShape> matchExistentialReturn(const MirFunction& function) noexcept {
  if (function.kind != mir::MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.sourceScopes.size() != 1 || function.locals.size() != 2 ||
      function.blocks.size() != 1) {
    return zc::none;
  }
  const auto& concreteLocal = function.locals[0];
  const auto& resultLocal = function.locals[1];
  if (concreteLocal.kind != mir::MirLocalKind::Temporary ||
      resultLocal.kind != mir::MirLocalKind::Temporary) {
    return zc::none;
  }
  const auto& block = function.blocks[0];
  if (block.id.ordinal() != 1 || block.statements.size() != 4 ||
      block.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      block.statements[0].storageLocal() != concreteLocal.id ||
      block.statements[1].kind() != mir::MirStatementKind::Assign ||
      block.statements[2].kind() != mir::MirStatementKind::StorageLive ||
      block.statements[2].storageLocal() != resultLocal.id ||
      block.statements[3].kind() != mir::MirStatementKind::Assign ||
      block.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  // Statement 1: concrete aggregate construction into the temporary.
  const auto& aggregateAssign = block.statements[1].assignmentValue();
  if (aggregateAssign.destination.local() != concreteLocal.id ||
      aggregateAssign.destination.projections().size() != 0 ||
      aggregateAssign.value.kind() != mir::MirRvalueKind::NominalAggregate) {
    return zc::none;
  }
  const auto& aggregate = aggregateAssign.value.nominalAggregateValue();
  if (aggregate.type != concreteLocal.type || aggregate.elements.size() != 1) { return zc::none; }
  if (aggregate.elements[0].operand.kind() != mir::MirOperandKind::Constant) { return zc::none; }
  // Statement 3: erase the concrete temporary into the existential result.
  const auto& eraseAssign = block.statements[3].assignmentValue();
  if (eraseAssign.destination.local() != resultLocal.id ||
      eraseAssign.destination.projections().size() != 0 ||
      eraseAssign.value.kind() != mir::MirRvalueKind::Erase) {
    return zc::none;
  }
  const auto& erase = eraseAssign.value.eraseValue();
  if (erase.sourceType != concreteLocal.type || erase.targetType != resultLocal.type ||
      erase.source.kind() != mir::MirOperandKind::Move ||
      erase.source.place().local() != concreteLocal.id ||
      erase.source.place().projections().size() != 0) {
    return zc::none;
  }
  // Terminator: return the existential result (a place-use with no projections).
  const auto& returnValue = block.terminator.returnValue().value;
  if (returnValue == zc::none) { return zc::none; }
  ZC_IF_SOME(value, returnValue) {
    if (value.kind() == mir::MirOperandKind::Constant || value.place().local() != resultLocal.id ||
        value.place().projections().size() != 0) {
      return zc::none;
    }
  }

  ExistentialReturnShape shape;
  shape.concreteType = concreteLocal.type;
  shape.existentialType = resultLocal.type;
  return zc::mv(shape);
}

zc::Maybe<TranslationFinding> validateExistentialReturn(
    const ExistentialReturnShape& shape, const MirFunction& mir, const Module& lirModule,
    const type::SemanticTypeStore& types) noexcept {
  const auto functions = lirModule.functions();
  if (functions.size() != 1) { return fault(TranslationFaultKind::FunctionSetMismatch, 0); }
  const auto& lir = functions[0];
  if (lir.owner() != mir.owner) { return fault(TranslationFaultKind::FunctionSetMismatch, 0); }
  const auto vtables = lirModule.vtables();
  if (vtables.size() != 1) { return fault(TranslationFaultKind::FunctionSetMismatch, 0); }
  const auto globals = lirModule.globals();
  if (globals.size() != 1) { return fault(TranslationFaultKind::FunctionSetMismatch, 0); }

  // Existential carrier: aggregate {dataPtr, vtablePtr}, both opaque pointers.
  const auto pointerCarrier = ValueType::pointer(0);
  const auto& returnCarrier = lir.returnCarrier();
  if (returnCarrier.kind() != ValueTypeKind::Aggregate) {
    return fault(TranslationFaultKind::SlotSetMismatch, 0);
  }
  const auto returnFields = returnCarrier.aggregateFields();
  if (returnFields.size() != 2 || returnFields[0].type != pointerCarrier ||
      returnFields[1].type != pointerCarrier) {
    return fault(TranslationFaultKind::SlotSetMismatch, 0);
  }

  // No parameters, four body locals.
  if (lir.parameters().size() != 0) { return fault(TranslationFaultKind::SlotSetMismatch, 0); }
  const auto locals = lir.locals();
  if (locals.size() != 4) { return fault(TranslationFaultKind::SlotSetMismatch, 0); }
  if (locals[0].ordinal() != ExistentialReturnShape::kExistentialOrdinal ||
      locals[0].carrier() != returnCarrier) {
    return fault(TranslationFaultKind::SlotSetMismatch, 0);
  }
  if (locals[1].ordinal() != ExistentialReturnShape::kDataPtrOrdinal ||
      locals[1].carrier() != pointerCarrier) {
    return fault(TranslationFaultKind::SlotSetMismatch, 0);
  }
  if (locals[2].ordinal() != ExistentialReturnShape::kVtablePtrOrdinal ||
      locals[2].carrier() != pointerCarrier) {
    return fault(TranslationFaultKind::SlotSetMismatch, 0);
  }
  if (locals[3].ordinal() != ExistentialReturnShape::kExistentialTmpOrdinal ||
      locals[3].carrier() != returnCarrier) {
    return fault(TranslationFaultKind::SlotSetMismatch, 0);
  }

  // One block, four statements, ReturnLocal terminator.
  const auto blocks = lir.blocks();
  if (blocks.size() != 1) { return fault(TranslationFaultKind::BlockBijectionMismatch, 0); }
  const auto& block = blocks[0];
  if (block.id().ordinal() != 1 || block.statements().size() != 4) {
    return fault(TranslationFaultKind::EffectMismatch, 0, 1, 1);
  }
  // Statement 1: GlobalAddr(dataPtr, global=1).
  const auto& s0 = block.statements()[0];
  if (s0.kind() != StatementKind::GlobalAddr ||
      s0.destinationOrdinal() != ExistentialReturnShape::kDataPtrOrdinal ||
      s0.globalOrdinal() != 1) {
    return fault(TranslationFaultKind::EffectMismatch, 0, 1, 1);
  }
  // Statement 2: VtableAddr(vtablePtr, vtable=1).
  const auto& s1 = block.statements()[1];
  if (s1.kind() != StatementKind::VtableAddr ||
      s1.destinationOrdinal() != ExistentialReturnShape::kVtablePtrOrdinal ||
      s1.vtableOrdinal() != 1) {
    return fault(TranslationFaultKind::EffectMismatch, 0, 1, 2);
  }
  // Statement 3: InsertField(tmp, existential, field=0, dataPtr).
  const auto& s2 = block.statements()[2];
  if (s2.kind() != StatementKind::InsertField ||
      s2.destinationOrdinal() != ExistentialReturnShape::kExistentialTmpOrdinal ||
      s2.sourceOrdinal() != ExistentialReturnShape::kExistentialOrdinal || s2.fieldIndex() != 0 ||
      s2.storedValue().isConstant() ||
      s2.storedValue().localOrdinal() != ExistentialReturnShape::kDataPtrOrdinal) {
    return fault(TranslationFaultKind::EffectMismatch, 0, 1, 3);
  }
  // Statement 4: InsertField(existential, tmp, field=1, vtablePtr).
  const auto& s3 = block.statements()[3];
  if (s3.kind() != StatementKind::InsertField ||
      s3.destinationOrdinal() != ExistentialReturnShape::kExistentialOrdinal ||
      s3.sourceOrdinal() != ExistentialReturnShape::kExistentialTmpOrdinal ||
      s3.fieldIndex() != 1 || s3.storedValue().isConstant() ||
      s3.storedValue().localOrdinal() != ExistentialReturnShape::kVtablePtrOrdinal) {
    return fault(TranslationFaultKind::EffectMismatch, 0, 1, 4);
  }
  // Terminator: ReturnLocal(existential).
  if (block.terminator().kind() != TerminatorKind::ReturnLocal ||
      block.terminator().returnLocalOrdinal() != ExistentialReturnShape::kExistentialOrdinal) {
    return fault(TranslationFaultKind::EffectMismatch, 0, 1, 1);
  }

  // Vtable 1: ordinal 1, empty function indices (the impl method is not in
  // this single-function module).
  const auto& vtable = vtables[0];
  if (vtable.ordinal() != 1 || vtable.functionIndices().size() != 0) {
    return fault(TranslationFaultKind::FunctionSetMismatch, 0);
  }

  // Global 1: ordinal 1, aggregate constant with one integer field matching
  // the MIR aggregate element's constant.
  const auto& global = globals[0];
  if (global.ordinal() != 1) { return fault(TranslationFaultKind::FunctionSetMismatch, 0); }
  const auto& aggregateConstant = global.value();
  const auto& constantCarrier = aggregateConstant.carrier();
  if (constantCarrier.kind() != ValueTypeKind::Aggregate) {
    return fault(TranslationFaultKind::ConstantMismatch, 0, 1, 1);
  }
  const auto constantFields = constantCarrier.aggregateFields();
  if (constantFields.size() != 1) { return fault(TranslationFaultKind::ConstantMismatch, 0, 1, 1); }
  // The field carrier must match the MIR aggregate element's carrier.
  const auto& mirElement =
      mir.blocks[0].statements[1].assignmentValue().value.nominalAggregateValue().elements[0];
  auto elementCarrier = integerCarrier(mirElement.operand.constantValue().type, types);
  if (elementCarrier == zc::none) { return fault(TranslationFaultKind::SlotSetMismatch, 0); }
  if (constantFields[0].type != ZC_ASSERT_NONNULL(elementCarrier)) {
    return fault(TranslationFaultKind::ConstantMismatch, 0, 1, 1);
  }
  // The constant value must match the MIR aggregate element's constant.
  const auto constantOperands = aggregateConstant.fields();
  if (constantOperands.size() != 1 || !constantOperands[0].isConstant() ||
      constantOperands[0].isAggregateConstant() || constantOperands[0].isFloatConstant()) {
    return fault(TranslationFaultKind::ConstantMismatch, 0, 1, 1);
  }
  if (!sameConstant(constantOperands[0], mirElement.operand, ZC_ASSERT_NONNULL(elementCarrier))) {
    return fault(TranslationFaultKind::ConstantMismatch, 0, 1, 1);
  }

  return zc::none;
}

struct ExistentialCallResultShape final {
  size_t callerIndex = 0;
  size_t existentialCalleeIndex = 0;
  size_t indirectCalleeIndex = 0;
  size_t leafIndex = 0;
  identity::SemanticTypeId resultType;
  identity::SemanticTypeId concreteType;
  identity::DefId interface;
  // LIR slot ordinals (the producer numbers them densely).
  static constexpr uint32_t kCallerExistentialOrdinal = 1;
  static constexpr uint32_t kCallerResultOrdinal = 2;
  static constexpr uint32_t kCallerDataPtrOrdinal = 3;
  static constexpr uint32_t kCallerVtablePtrOrdinal = 4;
  static constexpr uint32_t kExistentialResultOrdinal = 1;
  static constexpr uint32_t kExistentialDataPtrOrdinal = 2;
  static constexpr uint32_t kExistentialVtablePtrOrdinal = 3;
  static constexpr uint32_t kExistentialTmpOrdinal = 4;
  static constexpr uint32_t kCalleeDataPtrOrdinal = 1;
  static constexpr uint32_t kCalleeVtablePtrOrdinal = 2;
  static constexpr uint32_t kCalleeResultOrdinal = 3;
  static constexpr uint32_t kCalleeFnPtrOrdinal = 4;
  static constexpr uint32_t kLeafReceiverOrdinal = 1;
};

zc::Maybe<ExistentialCallResultShape> matchExistentialCallResult(
    zc::ArrayPtr<const MirFunction* const> mirFunctions) noexcept {
  if (mirFunctions.size() != 4) { return zc::none; }
  size_t callerIndex = 0;
  size_t existentialCalleeIndex = 0;
  size_t indirectCalleeIndex = 0;
  size_t leafIndex = 0;
  bool foundCaller = false;
  bool foundExistentialCallee = false;
  bool foundIndirectCallee = false;
  bool foundLeaf = false;
  for (size_t index = 0; index < mirFunctions.size(); ++index) {
    const MirFunction& fn = *mirFunctions[index];
    bool isLeaf = fn.sourceDefinitionKind == identity::DefinitionKind::Method &&
                  fn.locals.size() == 1 && fn.blocks.size() == 1 &&
                  fn.locals[0].kind == mir::MirLocalKind::Parameter;
    bool isIndirectCallee = fn.sourceDefinitionKind == identity::DefinitionKind::Function &&
                            fn.locals.size() == 2 && fn.blocks.size() == 2 &&
                            fn.locals[0].kind == mir::MirLocalKind::Parameter &&
                            fn.locals[1].kind == mir::MirLocalKind::Temporary &&
                            fn.blocks[0].terminator.kind() == mir::MirTerminatorKind::IndirectCall;
    bool isExistentialCallee =
        fn.sourceDefinitionKind == identity::DefinitionKind::Function && fn.locals.size() == 2 &&
        fn.blocks.size() == 1 && fn.locals[0].kind == mir::MirLocalKind::Temporary &&
        fn.locals[1].kind == mir::MirLocalKind::Temporary && fn.blocks[0].statements.size() == 4 &&
        fn.blocks[0].statements[1].kind() == mir::MirStatementKind::Assign &&
        fn.blocks[0].statements[3].kind() == mir::MirStatementKind::Assign;
    bool isCaller = fn.sourceDefinitionKind == identity::DefinitionKind::Function &&
                    fn.locals.size() == 2 && fn.blocks.size() == 3 &&
                    fn.locals[0].kind == mir::MirLocalKind::UserLocal &&
                    fn.locals[1].kind == mir::MirLocalKind::Temporary &&
                    fn.blocks[0].terminator.kind() == mir::MirTerminatorKind::Call &&
                    fn.blocks[1].terminator.kind() == mir::MirTerminatorKind::Call;
    if (isLeaf) {
      if (foundLeaf) { return zc::none; }
      leafIndex = index;
      foundLeaf = true;
    } else if (isIndirectCallee) {
      if (foundIndirectCallee) { return zc::none; }
      indirectCalleeIndex = index;
      foundIndirectCallee = true;
    } else if (isExistentialCallee) {
      if (foundExistentialCallee) { return zc::none; }
      existentialCalleeIndex = index;
      foundExistentialCallee = true;
    } else if (isCaller) {
      if (foundCaller) { return zc::none; }
      callerIndex = index;
      foundCaller = true;
    }
  }
  if (!foundCaller || !foundExistentialCallee || !foundIndirectCallee || !foundLeaf) {
    return zc::none;
  }

  const MirFunction& caller = *mirFunctions[callerIndex];
  const MirFunction& existentialCallee = *mirFunctions[existentialCalleeIndex];
  const MirFunction& indirectCallee = *mirFunctions[indirectCalleeIndex];
  const MirFunction& leaf = *mirFunctions[leafIndex];

  // Result types must all agree.
  if (caller.resultType != indirectCallee.resultType ||
      indirectCallee.resultType != leaf.resultType) {
    return zc::none;
  }

  // Leaf: one block, no statements, constant return.
  const auto& leafBlock = leaf.blocks[0];
  if (leafBlock.id.ordinal() != 1 || leafBlock.statements.size() != 0 ||
      leafBlock.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& leafReturn = leafBlock.terminator.returnValue().value;
  if (leafReturn == zc::none) { return zc::none; }
  {
    const auto& value = ZC_ASSERT_NONNULL(leafReturn);
    if (value.kind() != mir::MirOperandKind::Constant ||
        value.constantValue().type != leaf.resultType) {
      return zc::none;
    }
  }

  // Indirect callee: entry StorageLive(result) + IndirectCall, continuation
  // Return(result).
  const auto& calleeParam = indirectCallee.locals[0];
  const auto& calleeResult = indirectCallee.locals[1];
  if (calleeResult.type != indirectCallee.resultType) { return zc::none; }
  const auto& calleeEntry = indirectCallee.blocks[0];
  const auto& calleeCont = indirectCallee.blocks[1];
  if (calleeEntry.id.ordinal() != 1 || calleeCont.id.ordinal() != 2 ||
      calleeEntry.statements.size() != 1 ||
      calleeEntry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      calleeEntry.statements[0].storageLocal() != calleeResult.id ||
      calleeEntry.terminator.kind() != mir::MirTerminatorKind::IndirectCall ||
      calleeCont.statements.size() != 0 ||
      calleeCont.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& indirect = calleeEntry.terminator.indirectCallValue();
  if (indirect.arguments.size() != 0 ||
      indirect.effect.kind() != mir::MirCallEffectKind::NoActivation ||
      indirect.destination.local() != calleeResult.id ||
      indirect.destination.projections().size() != 0 || indirect.normalTarget != calleeCont.id ||
      indirect.unwindTarget != zc::none) {
    return zc::none;
  }
  if (indirect.receiver.kind() == mir::MirOperandKind::Constant ||
      indirect.receiver.place().local() != calleeParam.id ||
      indirect.receiver.place().projections().size() != 0) {
    return zc::none;
  }
  {
    const auto& value = ZC_ASSERT_NONNULL(calleeCont.terminator.returnValue().value);
    if (value.kind() == mir::MirOperandKind::Constant || value.place().local() != calleeResult.id ||
        value.place().projections().size() != 0) {
      return zc::none;
    }
  }

  // Existential callee: one block, StorageLive(concrete) + Assign(aggregate) +
  // StorageLive(result) + Assign(erase), terminator Return(result).
  const auto& concreteLocal = existentialCallee.locals[0];
  const auto& existentialResultLocal = existentialCallee.locals[1];
  const auto& existentialBlock = existentialCallee.blocks[0];
  if (existentialBlock.id.ordinal() != 1 ||
      existentialBlock.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      existentialBlock.statements[0].storageLocal() != concreteLocal.id ||
      existentialBlock.statements[2].kind() != mir::MirStatementKind::StorageLive ||
      existentialBlock.statements[2].storageLocal() != existentialResultLocal.id ||
      existentialBlock.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& aggAssign = existentialBlock.statements[1].assignmentValue();
  if (aggAssign.destination.local() != concreteLocal.id ||
      aggAssign.destination.projections().size() != 0 ||
      aggAssign.value.kind() != mir::MirRvalueKind::NominalAggregate) {
    return zc::none;
  }
  const auto& aggregate = aggAssign.value.nominalAggregateValue();
  if (aggregate.type != concreteLocal.type || aggregate.elements.size() != 1) { return zc::none; }
  if (aggregate.elements[0].operand.kind() != mir::MirOperandKind::Constant) { return zc::none; }
  const auto& eraseAssign = existentialBlock.statements[3].assignmentValue();
  if (eraseAssign.destination.local() != existentialResultLocal.id ||
      eraseAssign.destination.projections().size() != 0 ||
      eraseAssign.value.kind() != mir::MirRvalueKind::Erase) {
    return zc::none;
  }
  const auto& erase = eraseAssign.value.eraseValue();
  if (erase.sourceType != concreteLocal.type || erase.targetType != existentialResultLocal.type ||
      erase.source.kind() != mir::MirOperandKind::Move ||
      erase.source.place().local() != concreteLocal.id ||
      erase.source.place().projections().size() != 0) {
    return zc::none;
  }
  {
    const auto& value = ZC_ASSERT_NONNULL(existentialBlock.terminator.returnValue().value);
    if (value.kind() == mir::MirOperandKind::Constant ||
        value.place().local() != existentialResultLocal.id ||
        value.place().projections().size() != 0) {
      return zc::none;
    }
  }

  // Caller: three blocks. Block 1 calls the existential callee into the
  // existential user local. Block 2 passes the existential to the indirect
  // callee and stores the scalar result. Block 3 returns the result.
  const auto& callerExistential = caller.locals[0];
  const auto& callerResult = caller.locals[1];
  if (callerResult.type != caller.resultType) { return zc::none; }
  const auto& callerEntry = caller.blocks[0];
  const auto& callerMid = caller.blocks[1];
  const auto& callerCont = caller.blocks[2];
  if (callerEntry.id.ordinal() != 1 || callerMid.id.ordinal() != 2 ||
      callerCont.id.ordinal() != 3 || callerEntry.statements.size() != 1 ||
      callerEntry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      callerEntry.statements[0].storageLocal() != callerExistential.id ||
      callerEntry.terminator.kind() != mir::MirTerminatorKind::Call ||
      callerMid.statements.size() != 1 ||
      callerMid.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      callerMid.statements[0].storageLocal() != callerResult.id ||
      callerMid.terminator.kind() != mir::MirTerminatorKind::Call ||
      callerCont.statements.size() != 0 ||
      callerCont.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& initCall = callerEntry.terminator.callValue();
  if (initCall.callee != existentialCallee.owner || initCall.arguments.size() != 0 ||
      initCall.effect.kind() != mir::MirCallEffectKind::NoActivation ||
      initCall.destination.local() != callerExistential.id ||
      initCall.destination.projections().size() != 0 || initCall.normalTarget != callerMid.id ||
      initCall.unwindTarget != zc::none) {
    return zc::none;
  }
  const auto& returnCall = callerMid.terminator.callValue();
  if (returnCall.callee != indirectCallee.owner || returnCall.arguments.size() != 1 ||
      returnCall.effect.kind() != mir::MirCallEffectKind::NoActivation ||
      returnCall.destination.local() != callerResult.id ||
      returnCall.destination.projections().size() != 0 ||
      returnCall.normalTarget != callerCont.id || returnCall.unwindTarget != zc::none) {
    return zc::none;
  }
  const auto& callArg = returnCall.arguments[0];
  if (callArg.kind() == mir::MirOperandKind::Constant ||
      callArg.place().local() != callerExistential.id ||
      callArg.place().projections().size() != 0) {
    return zc::none;
  }
  {
    const auto& value = ZC_ASSERT_NONNULL(callerCont.terminator.returnValue().value);
    if (value.kind() == mir::MirOperandKind::Constant || value.place().local() != callerResult.id ||
        value.place().projections().size() != 0) {
      return zc::none;
    }
  }

  ExistentialCallResultShape shape;
  shape.callerIndex = callerIndex;
  shape.existentialCalleeIndex = existentialCalleeIndex;
  shape.indirectCalleeIndex = indirectCalleeIndex;
  shape.leafIndex = leafIndex;
  shape.resultType = caller.resultType;
  shape.concreteType = concreteLocal.type;
  shape.interface = indirect.interface;
  return zc::mv(shape);
}

zc::Maybe<TranslationFinding> validateExistentialCallResult(
    const ExistentialCallResultShape& shape, zc::ArrayPtr<const MirFunction* const> mirFunctions,
    const Module& lirModule, const type::SemanticTypeStore& types) noexcept {
  const auto functions = lirModule.functions();
  if (functions.size() != 4) { return fault(TranslationFaultKind::FunctionSetMismatch, 0); }
  auto resultCarrier = integerCarrier(shape.resultType, types);
  if (resultCarrier == zc::none) { return fault(TranslationFaultKind::SlotSetMismatch, 0); }
  const auto resultCarrierValue = ZC_ASSERT_NONNULL(resultCarrier);
  const auto pointerCarrierValue = ValueType::pointer(0);

  // Resolve LIR function indices by MIR owner.
  const MirFunction& caller = *mirFunctions[shape.callerIndex];
  const MirFunction& existentialCallee = *mirFunctions[shape.existentialCalleeIndex];
  const MirFunction& indirectCallee = *mirFunctions[shape.indirectCalleeIndex];
  const MirFunction& leaf = *mirFunctions[shape.leafIndex];
  zc::Maybe<uint32_t> lirCaller;
  zc::Maybe<uint32_t> lirExistentialCallee;
  zc::Maybe<uint32_t> lirIndirectCallee;
  zc::Maybe<uint32_t> lirLeaf;
  for (uint32_t index = 0; index < functions.size(); ++index) {
    if (functions[index].owner() == caller.owner) {
      if (lirCaller != zc::none) { return fault(TranslationFaultKind::FunctionSetMismatch, index); }
      lirCaller = index;
    }
    if (functions[index].owner() == existentialCallee.owner) {
      if (lirExistentialCallee != zc::none) {
        return fault(TranslationFaultKind::FunctionSetMismatch, index);
      }
      lirExistentialCallee = index;
    }
    if (functions[index].owner() == indirectCallee.owner) {
      if (lirIndirectCallee != zc::none) {
        return fault(TranslationFaultKind::FunctionSetMismatch, index);
      }
      lirIndirectCallee = index;
    }
    if (functions[index].owner() == leaf.owner) {
      if (lirLeaf != zc::none) { return fault(TranslationFaultKind::FunctionSetMismatch, index); }
      lirLeaf = index;
    }
  }
  if (lirCaller == zc::none || lirExistentialCallee == zc::none || lirIndirectCallee == zc::none ||
      lirLeaf == zc::none) {
    return fault(TranslationFaultKind::FunctionSetMismatch, 0);
  }
  const uint32_t callerLirIndex = ZC_ASSERT_NONNULL(lirCaller);
  const uint32_t existentialCalleeLirIndex = ZC_ASSERT_NONNULL(lirExistentialCallee);
  const uint32_t indirectCalleeLirIndex = ZC_ASSERT_NONNULL(lirIndirectCallee);
  const uint32_t leafLirIndex = ZC_ASSERT_NONNULL(lirLeaf);

  // Vtable: one vtable, ordinal 1, interface matches, sole function index is
  // the leaf.
  const auto vtables = lirModule.vtables();
  if (vtables.size() != 1 || vtables[0].ordinal() != 1 ||
      vtables[0].interface() != shape.interface || vtables[0].functionIndices().size() != 1 ||
      vtables[0].functionIndices()[0] != leafLirIndex) {
    return fault(TranslationFaultKind::SlotSetMismatch, 0);
  }

  // Global: one global, ordinal 1, aggregate constant matching the MIR
  // element.
  const auto globals = lirModule.globals();
  if (globals.size() != 1 || globals[0].ordinal() != 1) {
    return fault(TranslationFaultKind::SlotSetMismatch, 0);
  }
  const auto& mirAggregate =
      existentialCallee.blocks[0].statements[1].assignmentValue().value.nominalAggregateValue();
  const auto& mirElement = mirAggregate.elements[0].operand;
  auto elementCarrier = integerCarrier(mirElement.constantValue().type, types);
  if (elementCarrier == zc::none) { return fault(TranslationFaultKind::SlotSetMismatch, 0); }
  const auto elementCarrierValue = ZC_ASSERT_NONNULL(elementCarrier);
  const auto& globalConstant = globals[0].value();
  if (globalConstant.carrier().kind() != ValueTypeKind::Aggregate ||
      globalConstant.carrier().aggregateFields().size() != 1 ||
      globalConstant.carrier().aggregateFields()[0].type != elementCarrierValue ||
      globalConstant.fields().size() != 1 ||
      !sameConstant(globalConstant.fields()[0], mirElement, elementCarrierValue)) {
    return fault(TranslationFaultKind::ConstantMismatch, 0, 1, 1);
  }

  // Caller LIR: zero parameters, four body locals (existential, result, data
  // ptr, vtable ptr), three blocks. Block 1 calls the existential callee.
  // Block 2 extracts the fat-pointer fields and calls the indirect callee.
  // Block 3 returns the result.
  {
    const Function& lirCallerFn = functions[callerLirIndex];
    if (lirCallerFn.returnCarrier() != resultCarrierValue) {
      return fault(TranslationFaultKind::SlotSetMismatch, callerLirIndex);
    }
    if (lirCallerFn.parameters().size() != 0 || lirCallerFn.locals().size() != 4) {
      return fault(TranslationFaultKind::SlotSetMismatch, callerLirIndex);
    }
    const auto& locals = lirCallerFn.locals();
    if (locals[0].ordinal() != ExistentialCallResultShape::kCallerExistentialOrdinal ||
        locals[0].carrier().kind() != ValueTypeKind::Aggregate ||
        locals[0].carrier().aggregateFields().size() != 2 ||
        locals[1].ordinal() != ExistentialCallResultShape::kCallerResultOrdinal ||
        locals[1].carrier() != resultCarrierValue ||
        locals[2].ordinal() != ExistentialCallResultShape::kCallerDataPtrOrdinal ||
        locals[2].carrier() != pointerCarrierValue ||
        locals[3].ordinal() != ExistentialCallResultShape::kCallerVtablePtrOrdinal ||
        locals[3].carrier() != pointerCarrierValue) {
      return fault(TranslationFaultKind::SlotSetMismatch, callerLirIndex);
    }
    if (lirCallerFn.blocks().size() != 3) {
      return fault(TranslationFaultKind::BlockBijectionMismatch, callerLirIndex);
    }
    const auto& entry = lirCallerFn.blocks()[0];
    const auto& mid = lirCallerFn.blocks()[1];
    const auto& cont = lirCallerFn.blocks()[2];
    if (entry.id().ordinal() != 1 || mid.id().ordinal() != 2 || cont.id().ordinal() != 3 ||
        entry.statements().size() != 0 || mid.statements().size() != 2 ||
        cont.statements().size() != 0) {
      return fault(TranslationFaultKind::BlockBijectionMismatch, callerLirIndex);
    }
    // Block 1 terminator: Call(existentialCallee, dest=existential, args=[],
    // target=mid).
    const auto& entryTerm = entry.terminator();
    if (entryTerm.kind() != TerminatorKind::Call || !entryTerm.hasCallDestination() ||
        entryTerm.calleeIndex() >= functions.size() ||
        functions[entryTerm.calleeIndex()].owner() != existentialCallee.owner ||
        entryTerm.callDestinationOrdinal() !=
            ExistentialCallResultShape::kCallerExistentialOrdinal ||
        entryTerm.callNormalTarget() != mid.id() || entryTerm.callArguments().size() != 0) {
      return fault(TranslationFaultKind::EffectMismatch, callerLirIndex, 1, 1);
    }
    // Block 2 statements: ExtractField(dataPtr, existential, 0) and
    // ExtractField(vtablePtr, existential, 1).
    const auto& midStmts = mid.statements();
    if (midStmts[0].kind() != StatementKind::ExtractField ||
        midStmts[0].destinationOrdinal() != ExistentialCallResultShape::kCallerDataPtrOrdinal ||
        midStmts[0].sourceOrdinal() != ExistentialCallResultShape::kCallerExistentialOrdinal ||
        midStmts[0].fieldIndex() != 0 || midStmts[1].kind() != StatementKind::ExtractField ||
        midStmts[1].destinationOrdinal() != ExistentialCallResultShape::kCallerVtablePtrOrdinal ||
        midStmts[1].sourceOrdinal() != ExistentialCallResultShape::kCallerExistentialOrdinal ||
        midStmts[1].fieldIndex() != 1) {
      return fault(TranslationFaultKind::EffectMismatch, callerLirIndex, 1, 2, 1);
    }
    // Block 2 terminator: Call(indirectCallee, dest=result,
    // args=[dataPtr, vtablePtr], target=cont).
    const auto& midTerm = mid.terminator();
    if (midTerm.kind() != TerminatorKind::Call || !midTerm.hasCallDestination() ||
        midTerm.calleeIndex() >= functions.size() ||
        functions[midTerm.calleeIndex()].owner() != indirectCallee.owner ||
        midTerm.callDestinationOrdinal() != ExistentialCallResultShape::kCallerResultOrdinal ||
        midTerm.callNormalTarget() != cont.id() || midTerm.callArguments().size() != 2 ||
        midTerm.callArguments()[0].isConstant() ||
        midTerm.callArguments()[0].localOrdinal() !=
            ExistentialCallResultShape::kCallerDataPtrOrdinal ||
        midTerm.callArguments()[1].isConstant() ||
        midTerm.callArguments()[1].localOrdinal() !=
            ExistentialCallResultShape::kCallerVtablePtrOrdinal) {
      return fault(TranslationFaultKind::EffectMismatch, callerLirIndex, 1, 2);
    }
    // Block 3 terminator: ReturnLocal(result).
    if (cont.terminator().kind() != TerminatorKind::ReturnLocal ||
        cont.terminator().returnLocalOrdinal() !=
            ExistentialCallResultShape::kCallerResultOrdinal) {
      return fault(TranslationFaultKind::EffectMismatch, callerLirIndex, 1, 3);
    }
  }

  // Existential callee LIR: zero parameters, four body locals (result, data
  // ptr, vtable ptr, existential tmp), one block with GlobalAddr + VtableAddr
  // + InsertField + InsertField, terminator ReturnLocal(result).
  {
    const Function& lirExistentialFn = functions[existentialCalleeLirIndex];
    if (lirExistentialFn.returnCarrier().kind() != ValueTypeKind::Aggregate ||
        lirExistentialFn.returnCarrier().aggregateFields().size() != 2) {
      return fault(TranslationFaultKind::SlotSetMismatch, existentialCalleeLirIndex);
    }
    if (lirExistentialFn.parameters().size() != 0 || lirExistentialFn.locals().size() != 4) {
      return fault(TranslationFaultKind::SlotSetMismatch, existentialCalleeLirIndex);
    }
    const auto& locals = lirExistentialFn.locals();
    if (locals[0].ordinal() != ExistentialCallResultShape::kExistentialResultOrdinal ||
        locals[0].carrier().kind() != ValueTypeKind::Aggregate ||
        locals[0].carrier().aggregateFields().size() != 2 ||
        locals[1].ordinal() != ExistentialCallResultShape::kExistentialDataPtrOrdinal ||
        locals[1].carrier() != pointerCarrierValue ||
        locals[2].ordinal() != ExistentialCallResultShape::kExistentialVtablePtrOrdinal ||
        locals[2].carrier() != pointerCarrierValue ||
        locals[3].ordinal() != ExistentialCallResultShape::kExistentialTmpOrdinal ||
        locals[3].carrier().kind() != ValueTypeKind::Aggregate ||
        locals[3].carrier().aggregateFields().size() != 2) {
      return fault(TranslationFaultKind::SlotSetMismatch, existentialCalleeLirIndex);
    }
    if (lirExistentialFn.blocks().size() != 1) {
      return fault(TranslationFaultKind::BlockBijectionMismatch, existentialCalleeLirIndex);
    }
    const auto& block = lirExistentialFn.blocks()[0];
    if (block.id().ordinal() != 1 || block.statements().size() != 4) {
      return fault(TranslationFaultKind::BlockBijectionMismatch, existentialCalleeLirIndex);
    }
    const auto& stmts = block.statements();
    if (stmts[0].kind() != StatementKind::GlobalAddr ||
        stmts[0].destinationOrdinal() != ExistentialCallResultShape::kExistentialDataPtrOrdinal ||
        stmts[0].globalOrdinal() != 1 || stmts[1].kind() != StatementKind::VtableAddr ||
        stmts[1].destinationOrdinal() != ExistentialCallResultShape::kExistentialVtablePtrOrdinal ||
        stmts[1].vtableOrdinal() != 1 || stmts[2].kind() != StatementKind::InsertField ||
        stmts[2].destinationOrdinal() != ExistentialCallResultShape::kExistentialTmpOrdinal ||
        stmts[2].sourceOrdinal() != ExistentialCallResultShape::kExistentialResultOrdinal ||
        stmts[2].fieldIndex() != 0 || stmts[2].storedValue().isConstant() ||
        stmts[2].storedValue().localOrdinal() !=
            ExistentialCallResultShape::kExistentialDataPtrOrdinal ||
        stmts[3].kind() != StatementKind::InsertField ||
        stmts[3].destinationOrdinal() != ExistentialCallResultShape::kExistentialResultOrdinal ||
        stmts[3].sourceOrdinal() != ExistentialCallResultShape::kExistentialTmpOrdinal ||
        stmts[3].fieldIndex() != 1 || stmts[3].storedValue().isConstant() ||
        stmts[3].storedValue().localOrdinal() !=
            ExistentialCallResultShape::kExistentialVtablePtrOrdinal) {
      return fault(TranslationFaultKind::EffectMismatch, existentialCalleeLirIndex, 1, 1, 1);
    }
    if (block.terminator().kind() != TerminatorKind::ReturnLocal ||
        block.terminator().returnLocalOrdinal() !=
            ExistentialCallResultShape::kExistentialResultOrdinal) {
      return fault(TranslationFaultKind::EffectMismatch, existentialCalleeLirIndex, 1, 1);
    }
  }

  // Indirect callee LIR: two pointer parameters (data ptr, vtable ptr), two
  // body locals (result, fn ptr), entry VtableSlotLoad then IndirectCall,
  // continuation ReturnLocal.
  {
    const Function& lirCalleeFn = functions[indirectCalleeLirIndex];
    if (lirCalleeFn.returnCarrier() != resultCarrierValue) {
      return fault(TranslationFaultKind::SlotSetMismatch, indirectCalleeLirIndex);
    }
    if (lirCalleeFn.parameters().size() != 2 || lirCalleeFn.locals().size() != 2) {
      return fault(TranslationFaultKind::SlotSetMismatch, indirectCalleeLirIndex);
    }
    const auto& params = lirCalleeFn.parameters();
    const auto& locals = lirCalleeFn.locals();
    if (params[0].ordinal() != ExistentialCallResultShape::kCalleeDataPtrOrdinal ||
        params[0].carrier() != pointerCarrierValue ||
        params[1].ordinal() != ExistentialCallResultShape::kCalleeVtablePtrOrdinal ||
        params[1].carrier() != pointerCarrierValue ||
        locals[0].ordinal() != ExistentialCallResultShape::kCalleeResultOrdinal ||
        locals[0].carrier() != resultCarrierValue ||
        locals[1].ordinal() != ExistentialCallResultShape::kCalleeFnPtrOrdinal ||
        locals[1].carrier() != pointerCarrierValue) {
      return fault(TranslationFaultKind::SlotSetMismatch, indirectCalleeLirIndex);
    }
    if (lirCalleeFn.blocks().size() != 2) {
      return fault(TranslationFaultKind::BlockBijectionMismatch, indirectCalleeLirIndex);
    }
    const auto& entry = lirCalleeFn.blocks()[0];
    const auto& cont = lirCalleeFn.blocks()[1];
    if (entry.id().ordinal() != 1 || cont.id().ordinal() != 2 || entry.statements().size() != 1 ||
        cont.statements().size() != 0) {
      return fault(TranslationFaultKind::BlockBijectionMismatch, indirectCalleeLirIndex);
    }
    const auto& stmt = entry.statements()[0];
    if (stmt.kind() != StatementKind::VtableSlotLoad ||
        stmt.destinationOrdinal() != ExistentialCallResultShape::kCalleeFnPtrOrdinal ||
        stmt.basePointerOrdinal() != ExistentialCallResultShape::kCalleeVtablePtrOrdinal ||
        stmt.vtableSlotIndex() != 0) {
      return fault(TranslationFaultKind::EffectMismatch, indirectCalleeLirIndex, 1, 1, 1);
    }
    const auto& term = entry.terminator();
    if (term.kind() != TerminatorKind::IndirectCall || !term.hasCallDestination() ||
        term.indirectCallPointerOrdinal() != ExistentialCallResultShape::kCalleeFnPtrOrdinal ||
        term.callDestinationOrdinal() != ExistentialCallResultShape::kCalleeResultOrdinal ||
        term.callNormalTarget() != cont.id() || term.callArguments().size() != 1 ||
        term.callArguments()[0].isConstant() ||
        term.callArguments()[0].localOrdinal() !=
            ExistentialCallResultShape::kCalleeDataPtrOrdinal) {
      return fault(TranslationFaultKind::EffectMismatch, indirectCalleeLirIndex, 1, 1);
    }
    if (cont.terminator().kind() != TerminatorKind::ReturnLocal ||
        cont.terminator().returnLocalOrdinal() !=
            ExistentialCallResultShape::kCalleeResultOrdinal) {
      return fault(TranslationFaultKind::EffectMismatch, indirectCalleeLirIndex, 1, 2);
    }
  }

  // Leaf LIR: one pointer parameter (receiver), zero body locals, one block
  // with ReturnInteger.
  {
    const Function& lirLeafFn = functions[leafLirIndex];
    if (lirLeafFn.returnCarrier() != resultCarrierValue) {
      return fault(TranslationFaultKind::SlotSetMismatch, leafLirIndex);
    }
    if (lirLeafFn.parameters().size() != 1 || lirLeafFn.locals().size() != 0) {
      return fault(TranslationFaultKind::SlotSetMismatch, leafLirIndex);
    }
    const auto& params = lirLeafFn.parameters();
    if (params[0].ordinal() != ExistentialCallResultShape::kLeafReceiverOrdinal ||
        params[0].carrier() != pointerCarrierValue) {
      return fault(TranslationFaultKind::SlotSetMismatch, leafLirIndex);
    }
    if (lirLeafFn.blocks().size() != 1) {
      return fault(TranslationFaultKind::BlockBijectionMismatch, leafLirIndex);
    }
    const auto& block = lirLeafFn.blocks()[0];
    if (block.id().ordinal() != 1 || block.statements().size() != 0) {
      return fault(TranslationFaultKind::BlockBijectionMismatch, leafLirIndex);
    }
    const auto& term = block.terminator();
    if (term.kind() != TerminatorKind::ReturnInteger) {
      return fault(TranslationFaultKind::EffectMismatch, leafLirIndex, 1, 1);
    }
    const auto& leafMirReturn = ZC_ASSERT_NONNULL(leaf.blocks[0].terminator.returnValue().value);
    if (!sameConstant(Operand::constant(term.returnIntegerValue()), leafMirReturn,
                      resultCarrierValue)) {
      return fault(TranslationFaultKind::ConstantMismatch, 0, 1, 1);
    }
  }

  return zc::none;
}

}  // namespace

zc::Maybe<TranslationFinding> TranslationValidator::validate(
    const MirFunction& mirFunction, const Module& lirModule,
    const type::SemanticTypeStore& types) noexcept {
  const auto functions = lirModule.functions();
  if (functions.size() != 1) return fault(TranslationFaultKind::FunctionSetMismatch, 0);
  if (functions[0].owner() != mirFunction.owner) {
    return fault(TranslationFaultKind::FunctionSetMismatch, 0);
  }
  // The existential return is validated as a unit because the Erase rvalue
  // lowers to a GlobalAddr + VtableAddr + InsertField + InsertField sequence
  // that breaks the 1:1 MIR-to-LIR statement correspondence.
  {
    auto existentialShape = matchExistentialReturn(mirFunction);
    if (existentialShape != zc::none) {
      return validateExistentialReturn(ZC_ASSERT_NONNULL(existentialShape), mirFunction, lirModule,
                                       types);
    }
  }
  return validatePair(0, mirFunction, functions[0], types, functions);
}

zc::Maybe<TranslationFinding> TranslationValidator::validate(
    zc::ArrayPtr<const MirFunction* const> mirFunctions, const Module& lirModule,
    const type::SemanticTypeStore& types) noexcept {
  const auto functions = lirModule.functions();
  if (functions.size() != mirFunctions.size()) {
    return fault(TranslationFaultKind::FunctionSetMismatch, 0);
  }
  // The by-value aggregate call pair is validated as a unit because the
  // flattened field slots and the projected-field return only make sense across
  // the caller/callee pair. Any other module falls through to per-owner pair
  // validation.
  {
    auto byValueShape = matchByValueAggregateCall(mirFunctions);
    if (byValueShape != zc::none) {
      return validateByValueAggregateCall(ZC_ASSERT_NONNULL(byValueShape), mirFunctions, lirModule,
                                          types);
    }
  }
  // The indirect-call-with-leaf triple is validated as a unit because the
  // split aggregate calling convention adds a vtable pointer parameter and
  // renumbers the result local, so per-pair ordinal matching does not apply.
  {
    auto indirectShape = matchIndirectCallWithLeaf(mirFunctions);
    if (indirectShape != zc::none) {
      return validateIndirectCallWithLeaf(ZC_ASSERT_NONNULL(indirectShape), mirFunctions, lirModule,
                                          types);
    }
  }
  // The stored-erase caller/callee/leaf triple is validated as a unit because
  // the Erase rvalue lowers to a multi-statement existential construction
  // sequence that breaks the 1:1 MIR-to-LIR statement correspondence.
  {
    auto erasedShape = matchErasedAggregateCall(mirFunctions);
    if (erasedShape != zc::none) {
      return validateErasedAggregateCall(ZC_ASSERT_NONNULL(erasedShape), mirFunctions, lirModule,
                                         types);
    }
  }
  // The existential-call-result four-function module is validated as a unit
  // because the caller's existential-return call and the split aggregate
  // calling convention break the 1:1 MIR-to-LIR correspondence.
  {
    auto existentialCallShape = matchExistentialCallResult(mirFunctions);
    if (existentialCallShape != zc::none) {
      return validateExistentialCallResult(ZC_ASSERT_NONNULL(existentialCallShape), mirFunctions,
                                           lirModule, types);
    }
  }
  // The existential return is validated as a unit because the Erase rvalue
  // lowers to a GlobalAddr + VtableAddr + InsertField + InsertField sequence
  // that breaks the 1:1 MIR-to-LIR statement correspondence.
  if (mirFunctions.size() == 1) {
    auto existentialShape = matchExistentialReturn(*mirFunctions[0]);
    if (existentialShape != zc::none) {
      return validateExistentialReturn(ZC_ASSERT_NONNULL(existentialShape), *mirFunctions[0],
                                       lirModule, types);
    }
  }
  // Every MIR owner matches exactly one LIR owner.
  for (uint32_t m = 0; m < mirFunctions.size(); ++m) {
    const MirFunction& mirFunction = *mirFunctions[m];
    zc::Maybe<uint32_t> lirIndex;
    for (uint32_t l = 0; l < functions.size(); ++l) {
      if (functions[l].owner() == mirFunction.owner) {
        if (lirIndex != zc::none) { return fault(TranslationFaultKind::FunctionSetMismatch, l); }
        lirIndex = l;
      }
    }
    if (lirIndex == zc::none) { return fault(TranslationFaultKind::FunctionSetMismatch, m); }
    auto finding = validatePair(ZC_ASSERT_NONNULL(lirIndex), mirFunction,
                                functions[ZC_ASSERT_NONNULL(lirIndex)], types, functions);
    if (finding != zc::none) return finding;
  }
  return zc::none;
}

}  // namespace zomlang::compiler::lir
