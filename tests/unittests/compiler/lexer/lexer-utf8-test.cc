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

#include "zc/ztest/test.h"
#include "compiler/lexer/lexer.h"
#include "tests/unittests/compiler/lexer/utils.h"

namespace zomlang {
namespace compiler {
namespace lexer {

// ================================================================================
// Invalid byte (0xFF)
// ================================================================================

ZC_TEST("LexerUtf8Test.InvalidByteRecoversLocally") {
  // 0xFF is never valid in UTF-8
  char invalidBytes[] = {'\xFF', ' ', 'a', 0};
  auto tokens = tokenize(zc::StringPtr(invalidBytes, 3));
  ZC_EXPECT(tokens.size() == 3);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[1].getValue() == "a"_zc);
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::EndOfFile));
}

// ================================================================================
// Overlong encodings
// ================================================================================

ZC_TEST("LexerUtf8Test.OverlongTwoByteEncoding") {
  // Overlong encoding of '/' (U+002F) as 0xC0 0xAF
  // Valid '/' is 0x2F (single byte). The overlong form 0xC0 0xAF is invalid.
  // The lexer rejects the first byte (size=1) since the decoded code point is < 0x80.
  // The second byte (0xAF) is a lone continuation byte, also rejected as Unknown.
  char overlong[] = {'\xC0', '\xAF', ' ', 'a', 0};
  auto tokens = tokenize(zc::StringPtr(overlong, 4));
  // Expected: Unknown(0xC0), Unknown(0xAF), Identifier(a), EOF
  ZC_EXPECT(tokens.size() == 4);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[2].getValue() == "a"_zc);
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerUtf8Test.OverlongThreeByteEncoding") {
  // Overlong encoding of '!' (U+0021) as 0xE0 0x80 0xA1
  // The decoder detects codePoint < 0x800 and returns {kInvalidCodePoint, 1}.
  // Each remaining continuation byte is also treated as a separate invalid byte.
  char overlong[] = {'\xE0', '\x80', '\xA1', 0};
  auto tokens = tokenize(zc::StringPtr(overlong, 3));
  // Expected: Unknown(0xE0), Unknown(0x80), Unknown(0xA1), EOF
  ZC_EXPECT(tokens.size() == 4);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerUtf8Test.OverlongFourByteEncoding") {
  // Overlong encoding of 'a' (U+0061) as 0xF0 0x80 0x80 0xA1
  // The decoder detects codePoint < 0x10000 and returns {kInvalidCodePoint, 1}.
  // Each remaining continuation byte is also treated as a separate invalid byte.
  char overlong[] = {'\xF0', '\x80', '\x80', '\xA1', 0};
  auto tokens = tokenize(zc::StringPtr(overlong, 4));
  // Expected: Unknown(0xF0), Unknown(0x80), Unknown(0x80), Unknown(0xA1), EOF
  ZC_EXPECT(tokens.size() == 5);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[4].is(ast::SyntaxKind::EndOfFile));
}

// ================================================================================
// Surrogate halves (U+D800 - U+DFFF)
// ================================================================================

ZC_TEST("LexerUtf8Test.SurrogateHalfLow") {
  // U+D800 encoded as UTF-8: 0xED 0xA0 0x80
  // Surrogate halves are not valid Unicode scalar values.
  // The decoder detects the surrogate and returns {kInvalidCodePoint, 1} for the first byte.
  // The remaining continuation bytes are each treated as separate invalid bytes.
  char surrogate[] = {'\xED', '\xA0', '\x80', ' ', 'a', 0};
  auto tokens = tokenize(zc::StringPtr(surrogate, 5));
  // Expected: Unknown(0xED), Unknown(0xA0), Unknown(0x80), Identifier(a), EOF
  ZC_EXPECT(tokens.size() == 5);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[3].getValue() == "a"_zc);
  ZC_EXPECT(tokens[4].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerUtf8Test.SurrogateHalfHigh") {
  // U+DFFF encoded as UTF-8: 0xED 0xBF 0xBF
  char surrogate[] = {'\xED', '\xBF', '\xBF', ' ', 'a', 0};
  auto tokens = tokenize(zc::StringPtr(surrogate, 5));
  // Expected: Unknown(0xED), Unknown(0xBF), Unknown(0xBF), Identifier(a), EOF
  ZC_EXPECT(tokens.size() == 5);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[3].getValue() == "a"_zc);
  ZC_EXPECT(tokens[4].is(ast::SyntaxKind::EndOfFile));
}

// ================================================================================
// Codepoints > U+10FFFF
// ================================================================================

ZC_TEST("LexerUtf8Test.CodepointAboveMax") {
  // U+110000 (above Unicode max) encoded as 4-byte UTF-8: 0xF4 0x90 0x80 0x80
  // The decoder detects codePoint > 0x10FFFF and returns {kInvalidCodePoint, 1}
  // Each remaining continuation byte is also treated as a separate invalid byte.
  char aboveMax[] = {'\xF4', '\x90', '\x80', '\x80', ' ', 'a', 0};
  auto tokens = tokenize(zc::StringPtr(aboveMax, 6));
  // Expected: Unknown(0xF4), Unknown(0x90), Unknown(0x80), Unknown(0x80),
  //           Identifier(a), EOF
  ZC_EXPECT(tokens.size() == 6);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[4].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[4].getValue() == "a"_zc);
  ZC_EXPECT(tokens[5].is(ast::SyntaxKind::EndOfFile));
}

// ================================================================================
// Truncated multi-byte sequences
// ================================================================================

ZC_TEST("LexerUtf8Test.TruncatedTwoByteSequence") {
  // 0xC3 starts a 2-byte sequence but we hit EOF before the continuation byte
  char truncated[] = {'\xC3', 0};
  auto tokens = tokenize(zc::StringPtr(truncated, 1));
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerUtf8Test.TruncatedThreeByteSequence") {
  // 0xE0 starts a 3-byte sequence but only one continuation byte follows.
  // The decoder returns {kInvalidCodePoint, 1} for the truncated start.
  // The remaining continuation byte is also treated as a separate invalid byte.
  char truncated[] = {'\xE0', '\xA0', 0};
  auto tokens = tokenize(zc::StringPtr(truncated, 2));
  // Expected: Unknown(0xE0), Unknown(0xA0), EOF
  ZC_EXPECT(tokens.size() == 3);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerUtf8Test.TruncatedFourByteSequence") {
  // 0xF0 starts a 4-byte sequence but only two continuation bytes follow (need 3).
  // The decoder returns {kInvalidCodePoint, 1} for the truncated start.
  // Each remaining continuation byte is also treated as a separate invalid byte.
  char truncated[] = {'\xF0', '\x90', '\x80', 0};
  auto tokens = tokenize(zc::StringPtr(truncated, 3));
  // Expected: Unknown(0xF0), Unknown(0x90), Unknown(0x80), EOF
  ZC_EXPECT(tokens.size() == 4);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::EndOfFile));
}

// ================================================================================
// Invalid continuation bytes
// ================================================================================

ZC_TEST("LexerUtf8Test.InvalidContinuationByte") {
  // 0xC3 0x28 - 0x28 is '(' which is not a valid continuation byte (0x80-0xBF)
  char badCont[] = {'\xC3', '\x28', 'a', 0};
  auto tokens = tokenize(zc::StringPtr(badCont, 3));
  // First byte 0xC3 is invalid (bad continuation), should be Unknown
  // Then 0x28 is '(' = LeftParen, then 'a' = Identifier
  ZC_EXPECT(tokens.size() >= 3);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Unknown));
  // 0x28 is '(' which should be recognized as LeftParen after recovery
  ZC_EXPECT(tokens[tokens.size() - 1].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerUtf8Test.InvalidContinuationInThreeByte") {
  // 0xE4 0x41 0x80 - 0x41 is 'A', not a valid continuation byte
  char badCont[] = {'\xE4', '\x41', '\x80', 0};
  auto tokens = tokenize(zc::StringPtr(badCont, 3));
  ZC_EXPECT(tokens.size() >= 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[tokens.size() - 1].is(ast::SyntaxKind::EndOfFile));
}

// ================================================================================
// Recovery: multiple invalid sequences with valid tokens between
// ================================================================================

ZC_TEST("LexerUtf8Test.MultipleInvalidSequencesRecovery") {
  // Invalid byte, then valid identifier, then another invalid byte, then valid keyword
  char mixed[] = {'\xFF', ' ', 'x', ' ', '\xFE', ' ', 'l', 'e', 't', 0};
  auto tokens = tokenize(zc::StringPtr(mixed, 9));
  // Expected: Unknown, Identifier(x), Unknown, LetKeyword, EOF
  ZC_EXPECT(tokens.size() == 5);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[1].getValue() == "x"_zc);
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::LetKeyword));
  ZC_EXPECT(tokens[4].is(ast::SyntaxKind::EndOfFile));
}

// ================================================================================
// Null byte handling
// ================================================================================

ZC_TEST("LexerUtf8Test.NullByteIsUnknown") {
  char nullByte[] = {'\0', 0};
  auto tokens = tokenize(zc::StringPtr(nullByte, 1));
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::EndOfFile));
}

// ================================================================================
// Valid UTF-8 sanity checks (ensure we don't break valid sequences)
// ================================================================================

ZC_TEST("LexerUtf8Test.ValidTwoByteSequence") {
  // U+00E9 = 0xC3 0xA9.
  auto tokens = tokenize("\xC3\xA9"_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[0].getValue() == "\xC3\xA9"_zc);
}

ZC_TEST("LexerUtf8Test.ValidThreeByteSequence") {
  // U+4E2D = 0xE4 0xB8 0xAD.
  auto tokens = tokenize("\xE4\xB8\xAD"_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[0].getValue() == "\xE4\xB8\xAD"_zc);
}

ZC_TEST("LexerUtf8Test.ValidFourByteSequence") {
  // U+1F600 is not an identifier start, but it is valid UTF-8.
  // It should produce Unknown token (not crash)
  auto tokens = tokenize("\xF0\x9F\x98\x80"_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Unknown));
}

// ================================================================================
// Diagnostic emission for invalid UTF-8
// ================================================================================

ZC_TEST("LexerUtf8Test.InvalidUtf8EmitsDiagnostics") {
  auto& sm = getSourceManager();
  auto diags = zc::heap<diagnostics::DiagnosticEngine>(sm);

  char invalidBytes[] = {'\xFF', ' ', 'a', 0};
  auto tokens = tokenize(zc::StringPtr(invalidBytes, 3), *diags);

  ZC_EXPECT(tokens.size() == 3);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[1].getValue() == "a"_zc);
  // The lexer should report errors for invalid characters
  ZC_EXPECT(diags->hasErrors());
}

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
