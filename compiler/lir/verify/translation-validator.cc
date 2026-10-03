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
  if (!data.is<type::semantic::ReferenceTypeData>()) return zc::none;
  // Both shared and mutable references lower to the opaque pointer carrier;
  // mutability is enforced by borrow-mode facts rather than the LIR carrier.
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
  if (!folded && receiverFieldRead == zc::none && mir.locals.size() == 1 &&
      mir.locals[0].kind == mir::MirLocalKind::Parameter && mir.blocks.size() == 1) {
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
      mir.locals.size() == 2 && mir.locals[0].kind == mir::MirLocalKind::Parameter &&
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
          if (receiverParameterFieldWriteVoid != zc::none &&
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
      case mir::MirTerminatorKind::Unreachable:
        return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1);
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

}  // namespace

zc::Maybe<TranslationFinding> TranslationValidator::validate(
    const MirFunction& mirFunction, const Module& lirModule,
    const type::SemanticTypeStore& types) noexcept {
  const auto functions = lirModule.functions();
  if (functions.size() != 1) return fault(TranslationFaultKind::FunctionSetMismatch, 0);
  if (functions[0].owner() != mirFunction.owner) {
    return fault(TranslationFaultKind::FunctionSetMismatch, 0);
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
