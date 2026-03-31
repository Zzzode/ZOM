// Copyright (c) 2024-2025 Zode.Z. All rights reserved
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

#include "zc/core/debug.h"
#include "zc/core/string.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/lexer/lexer.h"
#include "zomlang/compiler/lexer/token.h"
#include "zomlang/tests/unittests/compiler/lexer/utils.h"

namespace zomlang {
namespace compiler {
namespace lexer {

ZC_TEST("LexerNumericTest.ValidHexLiterals") {
  auto tokens = tokenize("0x1234 0xabcdef 0xABCDEF 0x1a2B"_zc);
  ZC_EXPECT(tokens.size() == 5);

  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[0].hasFlag(TokenFlags::HexSpecifier));

  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[1].hasFlag(TokenFlags::HexSpecifier));

  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::IntegerLiteral));

  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::IntegerLiteral));
}

ZC_TEST("LexerNumericTest.HexLiteralsWithSeparators") {
  auto tokens = tokenize("0x1_2_3 0xDEAD_BEEF"_zc);
  ZC_EXPECT(tokens.size() == 3);

  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[0].hasFlag(TokenFlags::ContainsSeparator));

  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[1].hasFlag(TokenFlags::ContainsSeparator));
}

ZC_TEST("LexerNumericTest.ValidBinaryLiterals") {
  auto tokens = tokenize("0b101 0B101 0b0 0b1"_zc);
  ZC_EXPECT(tokens.size() == 5);

  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[0].hasFlag(TokenFlags::BinarySpecifier));
  ZC_EXPECT(tokens[0].getValue() == "5"_zc);

  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[1].hasFlag(TokenFlags::BinarySpecifier));
  ZC_EXPECT(tokens[1].getValue() == "5"_zc);  // 0B normalized to 0b

  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[2].hasFlag(TokenFlags::BinarySpecifier));
  ZC_EXPECT(tokens[2].getValue() == "0"_zc);

  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[3].hasFlag(TokenFlags::BinarySpecifier));
  ZC_EXPECT(tokens[3].getValue() == "1"_zc);
}

ZC_TEST("LexerNumericTest.ValidOctalLiterals") {
  auto tokens = tokenize("0o123 0O123 0o0 0o7"_zc);
  ZC_EXPECT(tokens.size() == 5);

  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[0].hasFlag(TokenFlags::OctalSpecifier));
  ZC_EXPECT(tokens[0].getValue() == "83"_zc);

  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[1].hasFlag(TokenFlags::OctalSpecifier));
  ZC_EXPECT(tokens[1].getValue() == "83"_zc);  // 0O normalized to 0o

  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[2].hasFlag(TokenFlags::OctalSpecifier));
  ZC_EXPECT(tokens[2].getValue() == "0"_zc);

  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[3].hasFlag(TokenFlags::OctalSpecifier));
  ZC_EXPECT(tokens[3].getValue() == "7"_zc);
}

ZC_TEST("LexerNumericTest.BinaryLiteralsWithSeparators") {
  auto tokens = tokenize("0b1_0_1 0b1100_1010"_zc);
  ZC_EXPECT(tokens.size() == 3);

  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[0].hasFlag(TokenFlags::ContainsSeparator));
  ZC_EXPECT(tokens[0].getValue() == "5"_zc);

  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[1].hasFlag(TokenFlags::ContainsSeparator));
  ZC_EXPECT(tokens[1].getValue() == "202"_zc);
}

ZC_TEST("LexerNumericTest.OctalLiteralsWithSeparators") {
  auto tokens = tokenize("0o1_2_3 0o77_66"_zc);
  ZC_EXPECT(tokens.size() == 3);

  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[0].hasFlag(TokenFlags::ContainsSeparator));
  ZC_EXPECT(tokens[0].getValue() == "83"_zc);

  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[1].hasFlag(TokenFlags::ContainsSeparator));
  ZC_EXPECT(tokens[1].getValue() == "4086"_zc);
}

ZC_TEST("LexerNumericTest.InvalidSeparators") {
  // 0x_1 (Leading separator)
  // 0x1__2 (Consecutive separators)
  // 0x1_ (Trailing separator)

  // Note: Lexer continues after error, so we should get tokens.
  auto tokens = tokenize("0x_1 0x1__2 0x1_"_zc);
  ZC_EXPECT(tokens.size() == 4);

  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::IntegerLiteral));
  // Depending on implementation, flags might be set even if error.

  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::IntegerLiteral));
}

ZC_TEST("LexerNumericTest.ValidDecimalIntegers") {
  auto tokens = tokenize("0 1 123 9876543210"_zc);
  ZC_EXPECT(tokens.size() == 5);

  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[0].getValue() == "0"_zc);

  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[1].getValue() == "1"_zc);

  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[2].getValue() == "123"_zc);

  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[3].getValue() == "9876543210"_zc);
}

ZC_TEST("LexerNumericTest.DecimalWithSeparators") {
  auto tokens = tokenize("1_000 1_2_3"_zc);
  ZC_EXPECT(tokens.size() == 3);

  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[0].hasFlag(TokenFlags::ContainsSeparator));
  ZC_EXPECT(tokens[0].getValue() == "1000"_zc);

  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[1].hasFlag(TokenFlags::ContainsSeparator));
  ZC_EXPECT(tokens[1].getValue() == "123"_zc);
}

ZC_TEST("LexerNumericTest.FloatingPointLiterals") {
  auto tokens = tokenize("3.14 0.123 10.5"_zc);
  ZC_EXPECT(tokens.size() == 4);  // 3 numbers + EOF

  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::FloatLiteral));
  ZC_EXPECT(tokens[0].getValue() == "3.14"_zc);

  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::FloatLiteral));
  ZC_EXPECT(tokens[1].getValue() == "0.123"_zc);

  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::FloatLiteral));
  ZC_EXPECT(tokens[2].getValue() == "10.5"_zc);
}

ZC_TEST("LexerNumericTest.ScientificLiterals") {
  auto tokens = tokenize("1e2 1.5e2 1e-1"_zc);
  ZC_EXPECT(tokens.size() == 4);

  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::FloatLiteral));
  ZC_EXPECT(tokens[0].hasFlag(TokenFlags::Scientific));
  ZC_EXPECT(tokens[0].getValue() == "100"_zc);

  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::FloatLiteral));
  ZC_EXPECT(tokens[1].hasFlag(TokenFlags::Scientific));
  ZC_EXPECT(tokens[1].getValue() == "150"_zc);

  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::FloatLiteral));
  ZC_EXPECT(tokens[2].hasFlag(TokenFlags::Scientific));
  ZC_EXPECT(tokens[2].getValue() == "0.1"_zc);
}

ZC_TEST("LexerNumericTest.BigIntLiterals") {
  auto tokens = tokenize("123n 1_000n"_zc);
  ZC_EXPECT(tokens.size() == 3);

  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::BigIntLiteral));
  ZC_EXPECT(tokens[0].getValue() == "123n"_zc);

  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::BigIntLiteral));
  ZC_EXPECT(tokens[1].getValue() == "1000n"_zc);
}

ZC_TEST("LexerNumericTest.DecimalInvalidSeparators") {
  // 1__2 (Consecutive separators)
  // 1_ (Trailing separator)
  auto tokens = tokenize("1__2 1_"_zc);
  ZC_EXPECT(tokens.size() == 3);  // 1__2, 1_, EOF

  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[0].getValue() == "12"_zc);
  ZC_EXPECT(tokens[0].hasFlag(TokenFlags::ContainsSeparator));
  ZC_EXPECT(tokens[0].hasFlag(TokenFlags::ContainsInvalidSeparator));

  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[1].getValue() == "1"_zc);
  ZC_EXPECT(tokens[1].hasFlag(TokenFlags::ContainsSeparator));
  ZC_EXPECT(tokens[1].hasFlag(TokenFlags::ContainsInvalidSeparator));
}

ZC_TEST("LexerNumericTest.InvalidBigInts") {
  // 1.5n (Float BigInt)
  // 1e2n (Scientific BigInt)
  auto tokens = tokenize("1.5n 1e2n"_zc);
  ZC_EXPECT(tokens.size() == 3);

  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::FloatLiteral));  // Should fallback to float
  ZC_EXPECT(tokens[0].getValue() == "1.5"_zc);

  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::FloatLiteral));  // 1e2 is float
  ZC_EXPECT(tokens[1].getValue() == "100"_zc);
}

ZC_TEST("LexerNumericTest.NumberFollowedByIdentifier") {
  // 123a
  auto tokens = tokenize("123a"_zc);
  ZC_EXPECT(tokens.size() == 3);  // 123, a, EOF

  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[0].getValue() == "123"_zc);

  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[1].getValue() == "a"_zc);
}

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
