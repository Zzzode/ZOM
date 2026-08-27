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

#include "zc/ztest/test.h"
#include "zomlang/compiler/lir/lir-identity.h"
#include "zomlang/compiler/lir/lir-stores.h"

namespace zomlang::compiler::lir {
namespace {

/// \brief Returns a valid integer carrier of the given width.
LirValueType requireInteger(IntegerBitWidth width) {
  auto type = LirValueType::integer(width);
  ZC_IF_SOME(value, type) { return value; }
  ZC_UNREACHABLE
}

// Store-local identities are one-based and zero is invalid, per RFC 0021.

ZC_TEST("LIR store-local identities reject zero and validate one-based ordinals") {
  ZC_EXPECT(LirValueTypeId::fromOrdinal(0) == zc::none);
  ZC_EXPECT(LayoutId::fromOrdinal(0) == zc::none);
  ZC_EXPECT(FnAbiId::fromOrdinal(0) == zc::none);
  ZC_EXPECT(RuntimeSymbolId::fromOrdinal(0) == zc::none);
  ZC_EXPECT(LirSourceLocationId::fromOrdinal(0) == zc::none);

  ZC_EXPECT(LirValueTypeId().isValid() == false);
  ZC_IF_SOME(id, LirValueTypeId::fromOrdinal(3)) {
    ZC_EXPECT(id.isValid());
    ZC_EXPECT(id.ordinal() == 3);
  }
}

// The value-type store interns structurally, issues dense one-based handles, and
// looks the record back up unchanged.

ZC_TEST("LIR value-type store interns, dedups, and round-trips") {
  LirValueTypeStore store;
  const auto i32 = store.intern(requireInteger(IntegerBitWidth::Bit32));
  const auto i64 = store.intern(requireInteger(IntegerBitWidth::Bit64));
  const auto i32Again = store.intern(requireInteger(IntegerBitWidth::Bit32));

  ZC_EXPECT(i32.isValid());
  ZC_EXPECT(i32 == i32Again);
  ZC_EXPECT(i32 != i64);
  ZC_EXPECT(i32.ordinal() == 1);
  ZC_EXPECT(i64.ordinal() == 2);
  ZC_EXPECT(store.size() == 2);

  ZC_IF_SOME(record, store.lookup(i32)) {
    ZC_EXPECT(record.kind() == LirValueTypeKind::Integer);
    ZC_EXPECT(record.integerWidth() == IntegerBitWidth::Bit32);
  }
  ZC_IF_SOME(record, store.lookup(i64)) {
    ZC_EXPECT(record.integerWidth() == IntegerBitWidth::Bit64);
  }
  ZC_EXPECT(store.lookup(LirValueTypeId()) == zc::none);
}

// Pointer carriers are opaque and distinguished only by address space; the
// integer carrier construction rejects an out-of-domain width, and floats reject
// invalid formats.

ZC_TEST("LIR value type construction fails closed on invalid domains") {
  ZC_EXPECT(LirValueType::integer(static_cast<IntegerBitWidth>(7)) == zc::none);
  ZC_EXPECT(LirValueType::floating(static_cast<FloatFormat>(9)) == zc::none);

  auto p0 = LirValueType::pointer(0);
  auto p1 = LirValueType::pointer(1);
  ZC_EXPECT(p0.kind() == LirValueTypeKind::Pointer);
  ZC_EXPECT(p0 != p1);
}

// The layout store keys on the scalar layout record and rejects a non-power-of-two
// alignment or an invalid carrier.

ZC_TEST("LIR layout store interns scalar layouts and validates alignment") {
  LirValueTypeStore types;
  const auto carrier = types.intern(requireInteger(IntegerBitWidth::Bit32));

  ZC_EXPECT(StorageLayout::scalar(4, 3, carrier) == zc::none);
  ZC_EXPECT(StorageLayout::scalar(4, 4, LirValueTypeId()) == zc::none);

  LayoutStore layouts;
  ZC_IF_SOME(layout, StorageLayout::scalar(4, 4, carrier)) {
    const auto id = layouts.intern(layout);
    ZC_IF_SOME(again, StorageLayout::scalar(4, 4, carrier)) {
      ZC_EXPECT(layouts.intern(again) == id);
    }
    ZC_EXPECT(layouts.size() == 1);
    ZC_IF_SOME(record, layouts.lookup(id)) {
      ZC_EXPECT(record.sizeBytes() == 4);
      ZC_EXPECT(record.abiAlignment() == 4);
      ZC_EXPECT(record.carrier() == carrier);
    }
  }
}

// The function-ABI store retains ordered physical return and parameter carriers
// and dedups structurally equal records.

ZC_TEST("LIR function-ABI store interns ordered carrier decompositions") {
  LirValueTypeStore types;
  const auto i32 = types.intern(requireInteger(IntegerBitWidth::Bit32));
  const auto i64 = types.intern(requireInteger(IntegerBitWidth::Bit64));

  FnAbi first;
  first.addReturnCarrier(i32);
  first.addParameterCarrier(i32);
  first.addParameterCarrier(i64);

  FnAbi second;
  second.addReturnCarrier(i32);
  second.addParameterCarrier(i32);
  second.addParameterCarrier(i64);

  FnAbi reordered;
  reordered.addReturnCarrier(i32);
  reordered.addParameterCarrier(i64);
  reordered.addParameterCarrier(i32);

  FnAbiStore store;
  const auto a = store.intern(first);
  const auto b = store.intern(second);
  const auto c = store.intern(reordered);
  ZC_EXPECT(a == b);
  ZC_EXPECT(a != c);
  ZC_EXPECT(store.size() == 2);

  ZC_IF_SOME(record, store.lookup(a)) {
    ZC_EXPECT(record.returnCarriers().size() == 1);
    ZC_EXPECT(record.parameterCarriers().size() == 2);
    ZC_EXPECT(record.parameterCarriers()[0] == i32);
    ZC_EXPECT(record.parameterCarriers()[1] == i64);
  }
}

// The runtime-symbol store rejects empty and non-ASCII names and keys on the
// name plus function ABI.

ZC_TEST("LIR runtime-symbol store interns named imports and fails closed") {
  FnAbiStore abis;
  const auto abi = abis.intern(FnAbi{});

  ZC_EXPECT(RuntimeSymbol::from(""_zc, abi) == zc::none);
  ZC_EXPECT(RuntimeSymbol::from("__zom\xff"
                                "bad"_zc,
                                abi) == zc::none);
  ZC_EXPECT(RuntimeSymbol::from("__zom_catch_unwind"_zc, FnAbiId()) == zc::none);

  RuntimeSymbolStore store;
  ZC_IF_SOME(symbol, RuntimeSymbol::from("__zom_catch_unwind"_zc, abi)) {
    const auto id = store.intern(symbol);
    ZC_IF_SOME(again, RuntimeSymbol::from("__zom_catch_unwind"_zc, abi)) {
      ZC_EXPECT(store.intern(again) == id);
    }
    ZC_EXPECT(store.size() == 1);
    ZC_IF_SOME(record, store.lookup(id)) {
      ZC_EXPECT(record.name() == "__zom_catch_unwind"_zc);
      ZC_EXPECT(record.fnAbi() == abi);
    }
  }
}

// The source-location store rejects an end that precedes the start and dedups
// equal spans.

ZC_TEST("LIR source-location store interns byte spans") {
  ZC_EXPECT(LirSourceLocation::from(10, 4) == zc::none);

  LirSourceLocationStore store;
  ZC_IF_SOME(location, LirSourceLocation::from(4, 10)) {
    const auto id = store.intern(location);
    ZC_IF_SOME(again, LirSourceLocation::from(4, 10)) { ZC_EXPECT(store.intern(again) == id); }
    ZC_IF_SOME(other, LirSourceLocation::from(4, 11)) { ZC_EXPECT(store.intern(other) != id); }
    ZC_EXPECT(store.size() == 2);
    ZC_IF_SOME(record, store.lookup(id)) {
      ZC_EXPECT(record.byteStart() == 4);
      ZC_EXPECT(record.byteEnd() == 10);
    }
  }
}

}  // namespace
}  // namespace zomlang::compiler::lir
