// Copyright (c) 2026 Zode.Z. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/ir/instance-identity.h"

#include <cstdint>

#include "zc/core/array.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::ir {
namespace {

zc::Array<uint8_t> bytes(zc::ArrayPtr<const uint8_t> content) {
  auto result = zc::heapArray<uint8_t>(content.size());
  for (size_t index = 0; index < content.size(); ++index) result[index] = content[index];
  return result;
}

// One frozen-store record is always a non-empty length-framed byte sequence,
// even for an empty substitution or witness list.
zc::Array<uint8_t> singleByte(uint8_t value) {
  auto result = zc::heapArray<uint8_t>(1);
  result[0] = value;
  return result;
}

zc::Array<uint8_t> record(uint8_t tag) {
  auto result = zc::heapArray<uint8_t>(2);
  result[0] = 0x00;
  result[1] = tag;
  return result;
}

ZC_TEST("MonoInstanceSymbol.followsDomainShapeAndIsReproducible") {
  auto frame = encodeInstanceIdentity(bytes({{0xde, 0xad}}).asPtr(), record(0x01), record(0x02));
  ZC_REQUIRE(frame != zc::none);
  auto symbol = deriveMonoInstanceSymbol(ZC_ASSERT_NONNULL(frame).asPtr());
  ZC_REQUIRE(symbol != zc::none);
  const zc::String& value = ZC_ASSERT_NONNULL(symbol);
  // zom.mono.<2-byte key as 4 hex chars>.<64-hex-char digest>
  constexpr size_t kPrefixLength = sizeof("zom.mono.dead.") - 1;
  ZC_EXPECT(value.size() == kPrefixLength + 64);
  ZC_EXPECT(value.startsWith("zom.mono.dead."));

  auto rebuilt = deriveMonoInstanceSymbol(ZC_ASSERT_NONNULL(frame).asPtr());
  ZC_REQUIRE(rebuilt != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(rebuilt) == value);
}

ZC_TEST("MonoInstanceSymbol.digestChangesWithSubstitutionRecord") {
  auto first = encodeInstanceIdentity(singleByte(0x01).asPtr(), record(0x01), record(0x02));
  auto second = encodeInstanceIdentity(singleByte(0x01).asPtr(), record(0x03), record(0x02));
  ZC_REQUIRE(first != zc::none && second != zc::none);
  auto firstSymbol = deriveMonoInstanceSymbol(ZC_ASSERT_NONNULL(first).asPtr());
  auto secondSymbol = deriveMonoInstanceSymbol(ZC_ASSERT_NONNULL(second).asPtr());
  ZC_REQUIRE(firstSymbol != zc::none && secondSymbol != zc::none);
  ZC_EXPECT(!(ZC_ASSERT_NONNULL(firstSymbol) == ZC_ASSERT_NONNULL(secondSymbol)));
}

ZC_TEST("MonoInstanceSymbol.changesWithWitnessRecord") {
  auto first = encodeInstanceIdentity(singleByte(0x01).asPtr(), record(0x01), record(0x02));
  auto second = encodeInstanceIdentity(singleByte(0x01).asPtr(), record(0x01), record(0x04));
  ZC_REQUIRE(first != zc::none && second != zc::none);
  auto firstSymbol = deriveMonoInstanceSymbol(ZC_ASSERT_NONNULL(first).asPtr());
  auto secondSymbol = deriveMonoInstanceSymbol(ZC_ASSERT_NONNULL(second).asPtr());
  ZC_REQUIRE(firstSymbol != zc::none && secondSymbol != zc::none);
  ZC_EXPECT(!(ZC_ASSERT_NONNULL(firstSymbol) == ZC_ASSERT_NONNULL(secondSymbol)));
}

ZC_TEST("MonoInstanceSymbol.rejectsMalformedFrame") {
  auto garbage = zc::heapArray<uint8_t>(3);
  garbage[0] = 0x00;
  garbage[1] = 0x00;
  garbage[2] = 0x00;
  ZC_EXPECT(deriveMonoInstanceSymbol(garbage.asPtr()) == zc::none);
}

}  // namespace
}  // namespace zomlang::compiler::ir
