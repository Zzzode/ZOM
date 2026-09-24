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

Operand Operand::constant(IntegerConstant value) noexcept { return Operand(value); }
Operand Operand::localUse(uint32_t localOrdinal) noexcept { return Operand(localOrdinal); }

// A never-read placeholder constant for a localUse operand, which carries no
// integer constant. The i1 carrier always exists, so `from` cannot fail.
IntegerConstant Operand::fallbackConstant() noexcept {
  auto carrier = ValueType::integer(IntegerBitWidth::Bit1);
  auto value = IntegerConstant::from(ZC_ASSERT_NONNULL(carrier), 0);
  return ZC_ASSERT_NONNULL(value);
}

Statement Statement::assign(uint32_t destinationOrdinal, Operand value) noexcept {
  return Statement(StatementKind::Assign, destinationOrdinal, ComparisonOp::Eq, value, value);
}

Statement Statement::compare(uint32_t destinationOrdinal, ComparisonOp op, Operand left,
                             Operand right) noexcept {
  return Statement(StatementKind::Compare, destinationOrdinal, op, left, right);
}

Statement Statement::takeAddress(uint32_t destinationOrdinal, uint32_t sourceOrdinal) noexcept {
  Operand source = Operand::localUse(sourceOrdinal);
  return Statement(StatementKind::TakeAddress, destinationOrdinal, ComparisonOp::Eq, source,
                   source);
}

Statement Statement::loadField(uint32_t destinationOrdinal, uint32_t basePointerOrdinal,
                               uint32_t fieldOffsetBytes) noexcept {
  Operand base = Operand::localUse(basePointerOrdinal);
  return Statement(StatementKind::LoadField, destinationOrdinal, ComparisonOp::Eq, base, base,
                   fieldOffsetBytes);
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

}  // namespace zomlang::compiler::lir
