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

#include "compiler/hir/hir-module.h"
#include "compiler/lir/lir-store.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::lir {
namespace {

IntegerConstant i32Constant(uint64_t bits) {
  auto carrier = ValueType::integer(IntegerBitWidth::Bit32);
  ZC_REQUIRE(carrier != zc::none);
  auto value = IntegerConstant::from(ZC_REQUIRE_NONNULL(carrier), bits);
  ZC_REQUIRE(value != zc::none);
  return ZC_REQUIRE_NONNULL(value);
}

zc::Vector<IntegerConstant> oneSlot(uint64_t bits) {
  zc::Vector<IntegerConstant> result(1);
  result.add(i32Constant(bits));
  return result;
}

ZC_TEST("LIR aggregate return preserves the ordered slot bundle") {
  zc::Vector<IntegerConstant> bundle;
  bundle.add(i32Constant(42));
  bundle.add(i32Constant(7));
  auto terminator = Terminator::returnAggregate(zc::mv(bundle));
  ZC_REQUIRE(terminator != zc::none);
  auto& value = ZC_REQUIRE_NONNULL(terminator);
  ZC_EXPECT(value.kind() == TerminatorKind::ReturnAggregate);
  auto returned = value.returnAggregateSlots();
  ZC_REQUIRE(returned.size() == 2);
  ZC_EXPECT(returned[0].bits() == 42);
  ZC_EXPECT(returned[1].bits() == 7);
  ZC_EXPECT(returned[0].carrier().kind() == ValueTypeKind::Integer);
}

ZC_TEST("LIR aggregate return admits a single-slot bundle") {
  auto terminator = Terminator::returnAggregate(oneSlot(99));
  ZC_REQUIRE(terminator != zc::none);
  auto returned = ZC_REQUIRE_NONNULL(terminator).returnAggregateSlots();
  ZC_REQUIRE(returned.size() == 1);
  ZC_EXPECT(returned[0].bits() == 99);
}

ZC_TEST("LIR aggregate return fails closed on an empty bundle") {
  zc::Vector<IntegerConstant> empty;
  ZC_EXPECT(Terminator::returnAggregate(zc::mv(empty)) == zc::none);
}

ZC_TEST("LIR aggregate return fails closed above the slot cap") {
  zc::Vector<IntegerConstant> overCap(kMaxAggregateReturnSlots + 1);
  for (uint32_t index = 0; index <= kMaxAggregateReturnSlots; ++index) {
    overCap.add(i32Constant(index));
  }
  ZC_EXPECT(overCap.size() == kMaxAggregateReturnSlots + 1);
  ZC_EXPECT(Terminator::returnAggregate(zc::mv(overCap)) == zc::none);
}

ZC_TEST("LIR aggregate return admits exactly the slot cap") {
  zc::Vector<IntegerConstant> atCap(kMaxAggregateReturnSlots);
  for (uint32_t index = 0; index < kMaxAggregateReturnSlots; ++index) {
    atCap.add(i32Constant(index));
  }
  auto terminator = Terminator::returnAggregate(zc::mv(atCap));
  ZC_REQUIRE(terminator != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(terminator).returnAggregateSlots().size() ==
            kMaxAggregateReturnSlots);
}

ZC_TEST("LIR single-carrier return terminators are unchanged by the aggregate slot") {
  // The scalar return path stays byte-for-byte behaviorally identical: a
  // ReturnInteger terminator carries its constant and no aggregate slots.
  auto scalar = Terminator::returnInteger(i32Constant(5));
  ZC_EXPECT(scalar.kind() == TerminatorKind::ReturnInteger);
  ZC_EXPECT(scalar.returnIntegerValue().bits() == 5);
  ZC_EXPECT(scalar.returnAggregateSlots().size() == 0);
}

ZC_TEST("LIR string constant admits a pointer carrier and preserves bytes") {
  auto carrier = ValueType::pointer(0);
  zc::Vector<uint8_t> bytes;
  bytes.add(static_cast<uint8_t>('h'));
  bytes.add(static_cast<uint8_t>('e'));
  bytes.add(static_cast<uint8_t>('l'));
  bytes.add(static_cast<uint8_t>('l'));
  bytes.add(static_cast<uint8_t>('o'));
  auto value = StringConstant::from(carrier, zc::mv(bytes));
  ZC_REQUIRE(value != zc::none);
  auto& constant = ZC_REQUIRE_NONNULL(value);
  ZC_EXPECT(constant.carrier().kind() == ValueTypeKind::Pointer);
  auto returned = constant.bytes();
  ZC_REQUIRE(returned.size() == 5);
  ZC_EXPECT(returned[0] == static_cast<uint8_t>('h'));
  ZC_EXPECT(returned[4] == static_cast<uint8_t>('o'));
}

ZC_TEST("LIR string constant fails closed on a non-pointer carrier") {
  auto carrier = ZC_REQUIRE_NONNULL(ValueType::integer(IntegerBitWidth::Bit32));
  zc::Vector<uint8_t> bytes;
  bytes.add(static_cast<uint8_t>('x'));
  ZC_EXPECT(StringConstant::from(carrier, zc::mv(bytes)) == zc::none);
}

ZC_TEST("LIR string constant admits an empty byte vector") {
  auto carrier = ValueType::pointer(0);
  zc::Vector<uint8_t> bytes;
  auto value = StringConstant::from(carrier, zc::mv(bytes));
  ZC_REQUIRE(value != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(value).bytes().size() == 0);
}

ZC_TEST("LIR return string terminator carries the string constant") {
  auto carrier = ValueType::pointer(0);
  zc::Vector<uint8_t> bytes;
  bytes.add(static_cast<uint8_t>('h'));
  bytes.add(static_cast<uint8_t>('i'));
  auto value = StringConstant::from(carrier, zc::mv(bytes));
  ZC_REQUIRE(value != zc::none);
  auto terminator = Terminator::returnString(ZC_REQUIRE_NONNULL(zc::mv(value)));
  ZC_EXPECT(terminator.kind() == TerminatorKind::ReturnString);
  auto& returned = terminator.returnStringValue();
  ZC_EXPECT(returned.carrier().kind() == ValueTypeKind::Pointer);
  auto returnedBytes = returned.bytes();
  ZC_REQUIRE(returnedBytes.size() == 2);
  ZC_EXPECT(returnedBytes[0] == static_cast<uint8_t>('h'));
  ZC_EXPECT(returnedBytes[1] == static_cast<uint8_t>('i'));
}

ZC_TEST("LIR return string terminator does not leak aggregate slots") {
  auto carrier = ValueType::pointer(0);
  zc::Vector<uint8_t> bytes;
  bytes.add(static_cast<uint8_t>('z'));
  auto value = StringConstant::from(carrier, zc::mv(bytes));
  ZC_REQUIRE(value != zc::none);
  auto terminator = Terminator::returnString(ZC_REQUIRE_NONNULL(zc::mv(value)));
  ZC_EXPECT(terminator.returnAggregateSlots().size() == 0);
}

ZC_TEST("LIR return float terminator carries the float constant") {
  auto carrier = ZC_REQUIRE_NONNULL(ValueType::floating(FloatFormat::Binary64));
  auto value = FloatConstant::from(carrier, 0x3ff8000000000000ULL);
  ZC_REQUIRE(value != zc::none);
  auto terminator = Terminator::returnFloat(ZC_REQUIRE_NONNULL(zc::mv(value)));
  ZC_EXPECT(terminator.kind() == TerminatorKind::ReturnFloat);
  const auto& returned = terminator.returnFloatValue();
  ZC_EXPECT(returned.carrier() == carrier);
  ZC_EXPECT(returned.bits() == 0x3ff8000000000000ULL);
}

ZC_TEST("LIR float constant fails closed on a non-float carrier") {
  auto carrier = ZC_REQUIRE_NONNULL(ValueType::integer(IntegerBitWidth::Bit64));
  ZC_EXPECT(FloatConstant::from(carrier, 0x3ff8000000000000ULL) == zc::none);
}

// RFC 0059: the HIR scalar closure admission must never exceed the LIR call
// ladder width, or the HIR builder would emit a call the LIR terminator cannot
// represent and the function would reach the legacy rejection cascade as an
// invariant failure instead of a clean capability drain. The two HIR bounds are
// kept in lockstep with each other, and each is bounded by the LIR cap.
ZC_TEST("HIR closure arity bounds never exceed the LIR call argument cap") {
  ZC_EXPECT(hir::kMaxScalarClosureParameters == hir::kMaxClosureValueCallArguments);
  ZC_EXPECT(hir::kMaxScalarClosureParameters <= kMaxCallArguments);
  ZC_EXPECT(hir::kMaxClosureValueCallArguments <= kMaxCallArguments);
}

// RFC 0059: the LIR call ladder width is three. The terminator factory itself
// enforces the cap so no caller can construct an unbounded call: a zero- or
// at-cap argument vector is admitted while an over-cap vector fails closed.
ZC_TEST("LIR call terminator factory admits the cap and rejects an over-cap vector") {
  zc::Vector<Operand> noArguments;
  ZC_EXPECT(Terminator::callFunction(1, 2, zc::mv(noArguments), LirBlockId()) != zc::none);

  zc::Vector<Operand> atCap;
  for (uint32_t index = 0; index < kMaxCallArguments; ++index) {
    atCap.add(Operand::localUse(index + 1));
  }
  ZC_EXPECT(Terminator::callFunction(1, 2, zc::mv(atCap), LirBlockId()) != zc::none);

  zc::Vector<Operand> overCap;
  for (uint32_t index = 0; index < kMaxCallArguments + 1; ++index) {
    overCap.add(Operand::localUse(index + 1));
  }
  ZC_EXPECT(Terminator::callFunction(1, 2, zc::mv(overCap), LirBlockId()) == zc::none);
}

}  // namespace
}  // namespace zomlang::compiler::lir
