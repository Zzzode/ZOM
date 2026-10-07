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

#include "compiler/lir/lir-module.h"

namespace zomlang::compiler::lir {

zc::Maybe<IntegerConstant> IntegerConstant::from(ValueType carrier, uint64_t bits) noexcept {
  if (carrier.kind() != ValueTypeKind::Integer) { return zc::none; }
  return IntegerConstant(carrier, bits);
}

zc::Maybe<FloatConstant> FloatConstant::from(ValueType carrier, uint64_t bits) noexcept {
  if (carrier.kind() != ValueTypeKind::Float) { return zc::none; }
  return FloatConstant(carrier, bits);
}

zc::Maybe<StringConstant> StringConstant::from(ValueType carrier,
                                               zc::Vector<uint8_t>&& bytes) noexcept {
  if (carrier.kind() != ValueTypeKind::Pointer) { return zc::none; }
  return StringConstant(carrier, zc::mv(bytes));
}

zc::Maybe<AggregateConstant> AggregateConstant::from(ValueType carrier,
                                                     zc::Vector<Operand>&& fields) noexcept {
  if (carrier.kind() != ValueTypeKind::Aggregate) { return zc::none; }
  auto fieldTypes = carrier.aggregateFields();
  if (fields.size() != fieldTypes.size()) { return zc::none; }
  for (size_t i = 0; i < fields.size(); ++i) {
    if (!fields[i].isConstant()) { return zc::none; }
    if (fields[i].constantCarrier() != fieldTypes[i].type) { return zc::none; }
  }
  return AggregateConstant(zc::mv(carrier), zc::mv(fields));
}

AggregateConstant::AggregateConstant(ValueType carrier, zc::Vector<Operand>&& fields) noexcept
    : carrierValue(zc::mv(carrier)), fieldsValue(zc::mv(fields)) {}

AggregateConstant::AggregateConstant(AggregateConstant&&) noexcept = default;
AggregateConstant& AggregateConstant::operator=(AggregateConstant&&) noexcept = default;
AggregateConstant::~AggregateConstant() = default;

Operand Operand::constant(IntegerConstant value) noexcept { return Operand(value); }
Operand Operand::constant(FloatConstant value) noexcept { return Operand(zc::mv(value)); }
Operand Operand::constant(AggregateConstant value) noexcept { return Operand(zc::mv(value)); }
Operand Operand::localUse(uint32_t localOrdinal) noexcept { return Operand(localOrdinal); }

Operand Operand::clone() const noexcept {
  if (!isConstantValue) { return Operand::localUse(localSlot); }
  if (constantSlot.is<FloatConstant>()) {
    return Operand::constant(constantSlot.get<FloatConstant>());
  }
  if (constantSlot.is<AggregateConstant>()) {
    const auto& aggregate = constantSlot.get<AggregateConstant>();
    zc::Vector<Operand> clonedFields;
    for (const auto& field : aggregate.fields()) { clonedFields.add(field.clone()); }
    return Operand::constant(
        ZC_REQUIRE_NONNULL(AggregateConstant::from(aggregate.carrier(), zc::mv(clonedFields))));
  }
  return Operand::constant(constantSlot.get<IntegerConstant>());
}

// A never-read placeholder constant for a localUse operand, which carries no
// integer constant. The i1 carrier always exists, so `from` cannot fail.
IntegerConstant Operand::fallbackConstant() noexcept {
  auto carrier = ValueType::integer(IntegerBitWidth::Bit1);
  auto value = IntegerConstant::from(ZC_ASSERT_NONNULL(carrier), 0);
  return ZC_ASSERT_NONNULL(value);
}

Statement Statement::assign(uint32_t destinationOrdinal, Operand value) noexcept {
  // Assign reads only leftValue; rightValue is a never-read placeholder.
  return Statement(StatementKind::Assign, destinationOrdinal, ComparisonOp::Eq, zc::mv(value),
                   Operand::localUse(0));
}

Statement Statement::compare(uint32_t destinationOrdinal, ComparisonOp op, Operand left,
                             Operand right) noexcept {
  return Statement(StatementKind::Compare, destinationOrdinal, op, zc::mv(left), zc::mv(right));
}

Statement Statement::arithmetic(uint32_t destinationOrdinal, ArithmeticOp op, Operand left,
                                Operand right) noexcept {
  return Statement(StatementKind::Arithmetic, destinationOrdinal, op, zc::mv(left), zc::mv(right));
}

Statement Statement::takeAddress(uint32_t destinationOrdinal, uint32_t sourceOrdinal) noexcept {
  // TakeAddress reads only leftValue; rightValue is a never-read placeholder.
  return Statement(StatementKind::TakeAddress, destinationOrdinal, ComparisonOp::Eq,
                   Operand::localUse(sourceOrdinal), Operand::localUse(0));
}

Statement Statement::loadField(uint32_t destinationOrdinal, uint32_t basePointerOrdinal,
                               uint32_t fieldOffsetBytes) noexcept {
  // LoadField reads only leftValue; rightValue is a never-read placeholder.
  return Statement(StatementKind::LoadField, destinationOrdinal, ComparisonOp::Eq,
                   Operand::localUse(basePointerOrdinal), Operand::localUse(0), fieldOffsetBytes);
}

Statement Statement::storeField(uint32_t basePointerOrdinal, Operand value,
                                uint32_t fieldOffsetBytes) noexcept {
  return Statement(StatementKind::StoreField, /*destinationOrdinal=*/0, ComparisonOp::Eq,
                   Operand::localUse(basePointerOrdinal), zc::mv(value), fieldOffsetBytes);
}

Statement Statement::extractField(uint32_t destinationOrdinal, uint32_t sourceOrdinal,
                                  uint32_t fieldIndex) noexcept {
  // ExtractField reads only leftValue; rightValue is a never-read placeholder.
  return Statement(StatementKind::ExtractField, destinationOrdinal, ComparisonOp::Eq,
                   Operand::localUse(sourceOrdinal), Operand::localUse(0), fieldIndex);
}

Statement Statement::insertField(uint32_t destinationOrdinal, uint32_t aggregateOrdinal,
                                 uint32_t fieldIndex, Operand value) noexcept {
  return Statement(StatementKind::InsertField, destinationOrdinal, ComparisonOp::Eq,
                   Operand::localUse(aggregateOrdinal), zc::mv(value), fieldIndex);
}

// A never-read placeholder constant for terminators that carry no integer
// constant. The i1 carrier always exists, so `from` cannot fail here.
IntegerConstant Terminator::fallbackConstant() noexcept {
  auto carrier = ValueType::integer(IntegerBitWidth::Bit1);
  auto value = IntegerConstant::from(ZC_ASSERT_NONNULL(carrier), 0);
  return ZC_ASSERT_NONNULL(value);
}

Terminator Terminator::returnInteger(IntegerConstant value) noexcept { return Terminator(value); }

Terminator Terminator::gotoBlock(LirBlockId target) noexcept {
  return Terminator(TerminatorKind::Goto, 0, target, target);
}

Terminator Terminator::condBranch(uint32_t conditionOrdinal, LirBlockId trueTarget,
                                  LirBlockId falseTarget) noexcept {
  return Terminator(TerminatorKind::CondBranch, conditionOrdinal, trueTarget, falseTarget);
}

Terminator Terminator::returnLocal(uint32_t localOrdinal) noexcept {
  return Terminator(TerminatorKind::ReturnLocal, localOrdinal, LirBlockId(), LirBlockId());
}

zc::Maybe<Terminator> Terminator::callFunction(uint32_t calleeIndex, uint32_t destinationOrdinal,
                                               zc::Vector<Operand>&& arguments,
                                               LirBlockId normalTarget) noexcept {
  // The argument vector stays within the call cap; an over-cap vector is not a
  // representable call. The factory enforces the cap itself so no caller can
  // construct an unbounded call. An empty vector is a valid zero-argument call.
  if (arguments.size() > kMaxCallArguments) { return zc::none; }
  return Terminator(calleeIndex, destinationOrdinal, zc::mv(arguments), normalTarget);
}

zc::Maybe<Terminator> Terminator::returnAggregate(zc::Vector<IntegerConstant>&& slots) noexcept {
  // A multi-slot direct return must carry at least one slot and stay within the
  // bundle cap; an empty or over-cap bundle is not a representable return.
  if (slots.size() == 0 || slots.size() > kMaxAggregateReturnSlots) { return zc::none; }
  return Terminator(zc::mv(slots));
}

zc::Maybe<Terminator> Terminator::callVoidFunction(uint32_t calleeIndex,
                                                   zc::Vector<Operand>&& arguments,
                                                   LirBlockId normalTarget) noexcept {
  // A discarded unit call obeys the same argument cap but carries no result
  // destination.
  if (arguments.size() > kMaxCallArguments) { return zc::none; }
  return Terminator(calleeIndex, zc::mv(arguments), normalTarget);
}

Terminator Terminator::returnVoid() noexcept { return Terminator(TerminatorKind::ReturnVoid); }

Terminator Terminator::returnString(StringConstant&& value) noexcept {
  return Terminator(zc::mv(value));
}

}  // namespace zomlang::compiler::lir
