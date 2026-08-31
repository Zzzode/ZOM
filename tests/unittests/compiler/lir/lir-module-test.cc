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
// See the License for the specific language governing permissions and
// limitations under the License.

// RFC 0021 first-layer aggregate return: a multi-slot direct return carries an
// ordered bundle of integer-constant slots (rendered later as a literal struct).
// This proves the LIR terminator preserves slot order and fails closed on an
// empty or over-cap bundle, without adding an aggregate SSA carrier kind. The
// emitted slot order is MIR/HIR element order; this layer does not claim ABI
// field ordering.

#include "compiler/lir/lir-module.h"

#include "compiler/lir/lir-store.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::lir {
namespace {

LirIntegerConstant i32Constant(uint64_t bits) {
  auto carrier = LirValueType::integer(IntegerBitWidth::Bit32);
  ZC_REQUIRE(carrier != zc::none);
  auto value = LirIntegerConstant::from(ZC_REQUIRE_NONNULL(carrier), bits);
  ZC_REQUIRE(value != zc::none);
  return ZC_REQUIRE_NONNULL(value);
}

zc::Vector<LirIntegerConstant> oneSlot(uint64_t bits) {
  zc::Vector<LirIntegerConstant> result(1);
  result.add(i32Constant(bits));
  return result;
}

ZC_TEST("LIR aggregate return preserves the ordered slot bundle") {
  zc::Vector<LirIntegerConstant> bundle;
  bundle.add(i32Constant(42));
  bundle.add(i32Constant(7));
  auto terminator = LirTerminator::returnAggregate(zc::mv(bundle));
  ZC_REQUIRE(terminator != zc::none);
  auto& value = ZC_REQUIRE_NONNULL(terminator);
  ZC_EXPECT(value.kind() == LirTerminatorKind::ReturnAggregate);
  auto returned = value.returnAggregateSlots();
  ZC_REQUIRE(returned.size() == 2);
  ZC_EXPECT(returned[0].bits() == 42);
  ZC_EXPECT(returned[1].bits() == 7);
  ZC_EXPECT(returned[0].carrier().kind() == LirValueTypeKind::Integer);
}

ZC_TEST("LIR aggregate return admits a single-slot bundle") {
  auto terminator = LirTerminator::returnAggregate(oneSlot(99));
  ZC_REQUIRE(terminator != zc::none);
  auto returned = ZC_REQUIRE_NONNULL(terminator).returnAggregateSlots();
  ZC_REQUIRE(returned.size() == 1);
  ZC_EXPECT(returned[0].bits() == 99);
}

ZC_TEST("LIR aggregate return fails closed on an empty bundle") {
  zc::Vector<LirIntegerConstant> empty;
  ZC_EXPECT(LirTerminator::returnAggregate(zc::mv(empty)) == zc::none);
}

ZC_TEST("LIR aggregate return fails closed above the slot cap") {
  zc::Vector<LirIntegerConstant> overCap(kMaxAggregateReturnSlots + 1);
  for (uint32_t index = 0; index <= kMaxAggregateReturnSlots; ++index) {
    overCap.add(i32Constant(index));
  }
  ZC_EXPECT(overCap.size() == kMaxAggregateReturnSlots + 1);
  ZC_EXPECT(LirTerminator::returnAggregate(zc::mv(overCap)) == zc::none);
}

ZC_TEST("LIR aggregate return admits exactly the slot cap") {
  zc::Vector<LirIntegerConstant> atCap(kMaxAggregateReturnSlots);
  for (uint32_t index = 0; index < kMaxAggregateReturnSlots; ++index) {
    atCap.add(i32Constant(index));
  }
  auto terminator = LirTerminator::returnAggregate(zc::mv(atCap));
  ZC_REQUIRE(terminator != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(terminator).returnAggregateSlots().size() ==
            kMaxAggregateReturnSlots);
}

ZC_TEST("LIR single-carrier return terminators are unchanged by the aggregate slot") {
  // The scalar return path stays byte-for-byte behaviorally identical: a
  // ReturnInteger terminator carries its constant and no aggregate slots.
  auto scalar = LirTerminator::returnInteger(i32Constant(5));
  ZC_EXPECT(scalar.kind() == LirTerminatorKind::ReturnInteger);
  ZC_EXPECT(scalar.returnIntegerValue().bits() == 5);
  ZC_EXPECT(scalar.returnAggregateSlots().size() == 0);
}

}  // namespace
}  // namespace zomlang::compiler::lir
