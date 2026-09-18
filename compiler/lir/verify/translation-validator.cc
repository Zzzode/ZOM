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

zc::Maybe<IntegerConstant> constantFor(const mir::MirOperand& operand, ValueType carrier) noexcept {
  if (operand.kind() != mir::MirOperandKind::Constant) return zc::none;
  const auto integer = operand.constantValue().value.integerValue();
  if (integer == zc::none) return zc::none;
  auto bits = zeroExtendedBits(ZC_ASSERT_NONNULL(integer), carrier.integerWidth());
  if (bits == zc::none) return zc::none;
  return IntegerConstant::from(carrier, ZC_ASSERT_NONNULL(bits));
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

// Maps a MIR operand used in an expression position to the expected LIR
// operand: a constant of the given carrier, or a bare local use.
zc::Maybe<Operand> expectedOperand(const mir::MirOperand& operand, ValueType carrier) noexcept {
  if (operand.kind() == mir::MirOperandKind::Constant) {
    auto value = constantFor(operand, carrier);
    if (value == zc::none) return zc::none;
    return Operand::constant(ZC_ASSERT_NONNULL(value));
  }
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
                  fold.kind = FoldKind::ScalarConstant;
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
    const identity::SemanticTypeId fieldType =
        fold.hasFieldProjection && fold.projectedField != zc::none
            ? [&]() {
                for (const auto& element : aggregate.elements) {
                  if (element.field == ZC_ASSERT_NONNULL(fold.projectedField)) {
                    return element.operand.constantValue().type;
                  }
                }
                return aggregate.elements[0].operand.constantValue().type;
              }()
            : aggregate.elements[0].operand.constantValue().type;
    expectedReturnCarrier = integerCarrier(fieldType, types);
  } else {
    expectedReturnCarrier = integerCarrier(mir.resultType, types);
  }
  if (expectedReturnCarrier == zc::none ||
      lir.returnCarrier() != ZC_ASSERT_NONNULL(expectedReturnCarrier)) {
    return fault(TranslationFaultKind::SlotSetMismatch, functionIndex);
  }

  // Slot set. A folded function declares no slots; a materialized function maps
  // every MIR local one-to-one to a parameter or body-local slot with the
  // independently derived carrier.
  if (folded) {
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
      // The comparison temporary carries the bool result of its comparison;
      // every other local carries an integer in the admitted subset. The
      // conditional's boolean parameter is the one non-integer non-temporary
      // slot and resolves through the bool carrier.
      zc::Maybe<ValueType> carrier;
      if (source.kind == mir::MirLocalKind::Temporary) {
        carrier = boolCarrier(source.type, types);
      } else {
        carrier = integerCarrier(source.type, types);
        if (carrier == zc::none) carrier = boolCarrier(source.type, types);
      }
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
          if (assignment.destination.projections().size() != 0) {
            return fault(TranslationFaultKind::PlaceMappingMismatch, functionIndex, b + 1, b + 1,
                         statementIndex);
          }
          const uint32_t destinationOrdinal = assignment.destination.local().ordinal();
          if (actual.destinationOrdinal() != destinationOrdinal) {
            return fault(TranslationFaultKind::PlaceMappingMismatch, functionIndex, b + 1, b + 1,
                         statementIndex);
          }
          const ValueType* destinationCarrier = actual.value().isConstant()
                                                    ? &actual.value().constantValue().carrier()
                                                    : lirSlotCarrier(lir, destinationOrdinal);
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
              // the MIR constant type or place result type (integer in the
              // admitted subset), distinct from the one-bit result slot.
              zc::Maybe<ValueType> leafCarrier;
              if (comparison.left.kind() == mir::MirOperandKind::Constant) {
                leafCarrier = integerCarrier(comparison.left.constantValue().type, types);
              } else {
                leafCarrier = integerCarrier(comparison.left.place().resultType(), types);
                if (leafCarrier == zc::none) {
                  leafCarrier = boolCarrier(comparison.left.place().resultType(), types);
                }
              }
              if (leafCarrier == zc::none) {
                return fault(TranslationFaultKind::SlotSetMismatch, functionIndex, b + 1, b + 1,
                             statementIndex);
              }
              if (!sameConstant(actual.left(), comparison.left, ZC_ASSERT_NONNULL(leafCarrier)) ||
                  !sameConstant(actual.right(), comparison.right, ZC_ASSERT_NONNULL(leafCarrier))) {
                return fault(TranslationFaultKind::ConstantMismatch, functionIndex, b + 1, b + 1,
                             statementIndex);
              }
              break;
            }
            case mir::MirRvalueKind::NominalAggregate:
            case mir::MirRvalueKind::Arithmetic:
              // A materialized aggregate or arithmetic assignment has no LIR
              // effect in the admitted single-function subset.
              return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1,
                           statementIndex);
          }
          ++lirStatement;
          break;
        }
        case mir::MirStatementKind::BorrowCreation:
        case mir::MirStatementKind::SetDiscriminant:
        case mir::MirStatementKind::Deinitialize:
          return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1,
                       statementIndex);
      }
    }
    if (lirStatement != lirBlock.statements().size()) {
      return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1,
                   lirStatement + 1);
    }

    // Terminator correspondence under the dense block bijection.
    const Terminator& lirTerminator = lirBlock.terminator();
    switch (mirBlock.terminator.kind()) {
      case mir::MirTerminatorKind::Return: {
        const auto& returned = mirBlock.terminator.returnValue().value;
        if (returned == zc::none) {
          return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1);
        }
        const mir::MirOperand& operand = ZC_ASSERT_NONNULL(returned);
        if (folded &&
            (fold.kind == FoldKind::ScalarConstant || fold.kind == FoldKind::DirectConstant)) {
          if (lirTerminator.kind() != TerminatorKind::ReturnInteger) {
            return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1);
          }
          // The local-fold resolves through the folded initializer; a direct
          // constant return compares the returned operand itself.
          const mir::MirOperand& wanted = fold.kind == FoldKind::DirectConstant
                                              ? operand
                                              : fold.assignment->value.useValue().operand;
          if (!sameConstant(Operand::constant(lirTerminator.returnIntegerValue()), wanted,
                            ZC_ASSERT_NONNULL(expectedReturnCarrier))) {
            return fault(TranslationFaultKind::ConstantMismatch, functionIndex, b + 1, b + 1);
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
              const auto carrier =
                  integerCarrier(aggregate.elements[e].operand.constantValue().type, types);
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
                const auto carrier = integerCarrier(element.operand.constantValue().type, types);
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
        // The first arm is the true arm and maps to the LIR true target; the
        // default maps to the false target; every later arm must share the
        // default target (a one-true-arm loop or a two-arm true/false diamond).
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
        // Argument count and per-argument constant correspondence. The
        // admitted calls carry integer constants only; the one-argument call
        // reads its operand through the single-argument accessor.
        const uint32_t argumentCount =
            lirTerminator.callHasArgument()
                ? 1u
                : static_cast<uint32_t>(lirTerminator.callArguments().size());
        if (argumentCount != call.arguments.size()) {
          return fault(TranslationFaultKind::EffectMismatch, functionIndex, b + 1, b + 1);
        }
        for (uint32_t a = 0; a < argumentCount; ++a) {
          const Operand& actual = lirTerminator.callHasArgument()
                                      ? Operand::constant(lirTerminator.callArgument())
                                      : Operand::constant(lirTerminator.callArguments()[a]);
          const auto carrier = integerCarrier(call.arguments[a].constantValue().type, types);
          if (call.arguments[a].kind() != mir::MirOperandKind::Constant || carrier == zc::none ||
              !sameConstant(actual, call.arguments[a], ZC_ASSERT_NONNULL(carrier))) {
            return fault(TranslationFaultKind::ConstantMismatch, functionIndex, b + 1, b + 1);
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
