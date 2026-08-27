// Copyright (c) 2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/lexer/bigint.h"

#include "zc/ztest/test.h"

namespace zomlang {
namespace compiler {
namespace lexer {

ZC_TEST("BigIntTest.ParseDecimal") {
  // Test basic decimal
  ZC_EXPECT(parsePseudoBigInt("123n"_zc) == "123"_zc);
  ZC_EXPECT(parsePseudoBigInt("0n"_zc) == "0"_zc);

  // Test without 'n' suffix (should work as fallback)
  ZC_EXPECT(parsePseudoBigInt("123"_zc) == "123"_zc);

  // Test leading zeros
  ZC_EXPECT(parsePseudoBigInt("00123n"_zc) == "123"_zc);
  ZC_EXPECT(parsePseudoBigInt("000n"_zc) == "0"_zc);
}

ZC_TEST("BigIntTest.ParseBinary") {
  // 0b101 -> 5
  ZC_EXPECT(parsePseudoBigInt("0b101n"_zc) == "5"_zc);
  // 0B101 -> 5
  ZC_EXPECT(parsePseudoBigInt("0B101n"_zc) == "5"_zc);

  // 0b0 -> 0
  ZC_EXPECT(parsePseudoBigInt("0b0n"_zc) == "0"_zc);

  // Larger value: 0b11110000 -> 240
  ZC_EXPECT(parsePseudoBigInt("0b11110000n"_zc) == "240"_zc);
}

ZC_TEST("BigIntTest.ParseOctal") {
  // 0o755 -> 493
  ZC_EXPECT(parsePseudoBigInt("0o755n"_zc) == "493"_zc);
  // 0O755 -> 493
  ZC_EXPECT(parsePseudoBigInt("0O755n"_zc) == "493"_zc);

  // 0o0 -> 0
  ZC_EXPECT(parsePseudoBigInt("0o0n"_zc) == "0"_zc);

  // 0o10 -> 8
  ZC_EXPECT(parsePseudoBigInt("0o10n"_zc) == "8"_zc);
}

ZC_TEST("BigIntTest.ParseHex") {
  // 0xFF -> 255
  ZC_EXPECT(parsePseudoBigInt("0xFFn"_zc) == "255"_zc);
  // 0xff -> 255
  ZC_EXPECT(parsePseudoBigInt("0xffn"_zc) == "255"_zc);
  // 0XFF -> 255
  ZC_EXPECT(parsePseudoBigInt("0XFFn"_zc) == "255"_zc);

  // 0x0 -> 0
  ZC_EXPECT(parsePseudoBigInt("0x0n"_zc) == "0"_zc);

  // 0x10 -> 16
  ZC_EXPECT(parsePseudoBigInt("0x10n"_zc) == "16"_zc);

  // Mixed case
  ZC_EXPECT(parsePseudoBigInt("0xAbCdn"_zc) == "43981"_zc);
}

ZC_TEST("BigIntTest.LargeNumbers") {
  // Test a number that spans multiple 16-bit segments
  // 0x123456789ABC -> 20015998343868
  ZC_EXPECT(parsePseudoBigInt("0x123456789ABCn"_zc) == "20015998343868"_zc);
}

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
