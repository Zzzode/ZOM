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

zc::Maybe<LirIntegerConstant> LirIntegerConstant::from(LirValueType carrier,
                                                       uint64_t bits) noexcept {
  if (carrier.kind() != LirValueTypeKind::Integer) { return zc::none; }
  return LirIntegerConstant(carrier, bits);
}

LirOperand LirOperand::constant(LirIntegerConstant value) noexcept { return LirOperand(value); }

LirStatement LirStatement::assign(uint32_t destinationOrdinal, LirOperand value) noexcept {
  return LirStatement(destinationOrdinal, value);
}

// A never-read placeholder constant for terminators that carry no integer
// constant. The i1 carrier always exists, so `from` cannot fail here.
LirIntegerConstant LirTerminator::fallbackConstant() noexcept {
  auto carrier = LirValueType::integer(IntegerBitWidth::Bit1);
  auto value = LirIntegerConstant::from(ZC_ASSERT_NONNULL(carrier), 0);
  return ZC_ASSERT_NONNULL(value);
}

LirTerminator LirTerminator::returnInteger(LirIntegerConstant value) noexcept {
  return LirTerminator(value);
}

LirTerminator LirTerminator::gotoBlock(LirBlockId target) noexcept {
  return LirTerminator(LirTerminatorKind::Goto, 0, target, target);
}

LirTerminator LirTerminator::condBranch(uint32_t conditionOrdinal, LirBlockId trueTarget,
                                        LirBlockId falseTarget) noexcept {
  return LirTerminator(LirTerminatorKind::CondBranch, conditionOrdinal, trueTarget, falseTarget);
}

LirTerminator LirTerminator::returnLocal(uint32_t localOrdinal) noexcept {
  return LirTerminator(LirTerminatorKind::ReturnLocal, localOrdinal, LirBlockId(), LirBlockId());
}

}  // namespace zomlang::compiler::lir
