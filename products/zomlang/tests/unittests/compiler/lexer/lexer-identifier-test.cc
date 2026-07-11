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
#include "zomlang/compiler/lexer/lexer.h"
#include "zomlang/compiler/lexer/unicode-data.h"
#include "zomlang/tests/unittests/compiler/lexer/utils.h"

namespace zomlang {
namespace compiler {
namespace lexer {

// ================================================================================
// ASCII identifiers
// ================================================================================

ZC_TEST("LexerIdentifierTest.AsciiLetterIdentifier") {
  auto tokens = tokenize("abc"_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[0].getValue() == "abc"_zc);
}

ZC_TEST("LexerIdentifierTest.UnderscoreStartIdentifier") {
  auto tokens = tokenize("_foo"_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[0].getValue() == "_foo"_zc);
}

ZC_TEST("LexerIdentifierTest.DollarStartIdentifier") {
  // '$' is a valid identifier start character (like JavaScript global symbols)
  auto tokens = tokenize("$bar"_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[0].getValue() == "$bar"_zc);
}

ZC_TEST("LexerIdentifierTest.DollarAloneIdentifier") {
  auto tokens = tokenize("$"_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[0].getValue() == "$"_zc);
}

ZC_TEST("LexerIdentifierTest.DollarWithDigitsIdentifier") {
  auto tokens = tokenize("$123"_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[0].getValue() == "$123"_zc);
}

ZC_TEST("LexerIdentifierTest.UnderscoreAloneIsUnderscoreToken") {
  // A bare '_' is the Underscore token, not an Identifier
  auto tokens = tokenize("_ "_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Underscore));
  ZC_EXPECT(tokens[0].getValue() == "_"_zc);
}

ZC_TEST("LexerIdentifierTest.NumericLikeUnderscoreReportsError") {
  // '_' followed by digits is treated as identifier but flagged as error
  auto& sm = getSourceManager();
  auto diags = zc::heap<diagnostics::DiagnosticEngine>(sm);
  auto tokens = tokenize("_123"_zc, *diags);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[0].getValue() == "_123"_zc);
  ZC_EXPECT(diags->hasErrors());
}

ZC_TEST("LexerIdentifierTest.HyphenatedIsTwoIdentifiers") {
  // 'a-b' should lex as Identifier(a), Minus, Identifier(b)
  auto tokens = tokenize("a-b"_zc);
  ZC_EXPECT(tokens.size() == 4);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[0].getValue() == "a"_zc);
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Minus));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[2].getValue() == "b"_zc);
}

ZC_TEST("LexerIdentifierTest.MixedAsciiIdentifier") {
  auto tokens = tokenize("foo_bar123$baz"_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[0].getValue() == "foo_bar123$baz"_zc);
}

// ================================================================================
// Unicode XID_Start identifiers
// ================================================================================

ZC_TEST("LexerIdentifierTest.UnicodeLatinAccentStart") {
  // U+00E9 is in XID_Start.
  auto tokens = tokenize("\303\251"_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[0].getValue() == "\303\251"_zc);
}

ZC_TEST("LexerIdentifierTest.UnicodeLatinAccentEclair") {
  auto tokens = tokenize("\303\251clair"_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[0].getValue() == "\303\251clair"_zc);
}

ZC_TEST("LexerIdentifierTest.UnicodeSpanishEnie") {
  // U+00F1 is in XID_Start.
  auto tokens = tokenize("\303\261o\303\261o"_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[0].getValue() == "\303\261o\303\261o"_zc);
}

ZC_TEST("LexerIdentifierTest.UnicodeGreekAlpha") {
  // Greek small letters U+03B1, U+03B2, U+03B3 are in XID_Start/Continue.
  auto tokens = tokenize("\316\261\316\262\316\263"_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[0].getValue() == "\316\261\316\262\316\263"_zc);
}

ZC_TEST("LexerIdentifierTest.UnicodeCyrillicDe") {
  // Cyrillic small letters U+0434, U+043E, U+043C are in XID_Start/Continue.
  auto tokens = tokenize("\320\264\320\276\320\274"_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[0].getValue() == "\320\264\320\276\320\274"_zc);
}

ZC_TEST("LexerIdentifierTest.UnicodeCJKChinese") {
  // U+4E2D and U+6587 are in XID_Start/Continue.
  auto tokens = tokenize("\344\270\255\346\226\207"_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[0].getValue() == "\344\270\255\346\226\207"_zc);
}

ZC_TEST("LexerIdentifierTest.UnicodeHelloWithAccents") {
  // U+00E9 in non-start position (XID_Continue).
  auto tokens = tokenize("h\303\251llo"_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[0].getValue() == "h\303\251llo"_zc);
}

ZC_TEST("LexerIdentifierTest.UnicodeWorldWithUmlaut") {
  // U+00F6 in non-start position (XID_Continue).
  auto tokens = tokenize("w\303\266rld"_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[0].getValue() == "w\303\266rld"_zc);
}

// ================================================================================
// Unicode XID_Continue in non-start position
// ================================================================================

ZC_TEST("LexerIdentifierTest.UnicodeContinueInNonStart") {
  // 'a' (ASCII start) + combining characters (XID_Continue only)
  // ASCII 'e' plus U+0300 combining grave accent as separate code points.
  // Note: U+0300 is XID_Continue (combining grave accent) but NOT XID_Start
  // so the byte sequence should be a single identifier.
  auto tokens = tokenize("e\314\200"_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  // The value should contain both the 'e' and the combining character
  ZC_EXPECT(tokens[0].getValue().size() == 3);  // 'e' (1 byte) + U+0300 (2 bytes)
}

// ================================================================================
// Invalid identifier starts
// ================================================================================

ZC_TEST("LexerIdentifierTest.EmojiIsNotIdentifierStart") {
  // U+1F600 is not in XID_Start.
  // It should produce an Unknown token
  auto tokens = tokenize("\xF0\x9F\x98\x80"_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Unknown));
}

ZC_TEST("LexerIdentifierTest.SymbolIsNotIdentifierStart") {
  // U+00A9 copyright sign is not in XID_Start.
  auto tokens = tokenize("\xC2\xA9"_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Unknown));
}

ZC_TEST("LexerIdentifierTest.DigitCannotStartIdentifier") {
  // '1abc' should lex as IntegerLiteral(1) + Identifier(abc)
  auto tokens = tokenize("1abc"_zc);
  ZC_EXPECT(tokens.size() == 3);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[1].getValue() == "abc"_zc);
}

// ================================================================================
// Unicode data table sanity
// ================================================================================

ZC_TEST("LexerIdentifierTest.UnicodeIdStartKnownCodepoints") {
  // Greek small letter pi (U+03C0)
  ZC_EXPECT(isIdStart(0x03C0));
  // Latin small letter e with acute (U+00E9)
  ZC_EXPECT(isIdStart(0x00E9));
  // CJK Unified Ideograph (U+4E2D)
  ZC_EXPECT(isIdStart(0x4E2D));
  // Cyrillic small letter de (U+0434)
  ZC_EXPECT(isIdStart(0x0434));
}

ZC_TEST("LexerIdentifierTest.UnicodeIdStartRejectsNonLetters") {
  // Combining grave accent (U+0300) - XID_Continue but not XID_Start
  ZC_EXPECT(!isIdStart(0x0300));
  // ASCII digit '0' (U+0030)
  ZC_EXPECT(!isIdStart(0x0030));
  // Emoji range (U+1F600)
  ZC_EXPECT(!isIdStart(0x1F600));
}

ZC_TEST("LexerIdentifierTest.UnicodeIdPartIncludesContinue") {
  // Combining grave accent (U+0300) is XID_Continue
  ZC_EXPECT(isIdPart(0x0300));
  // ASCII digit '0' (U+0030) is XID_Continue
  ZC_EXPECT(isIdPart(0x0030));
  // Latin small letter a (U+0061) is both XID_Start and XID_Continue
  ZC_EXPECT(isIdPart(0x0061));
}

// ================================================================================
// Identifier followed by keyword boundary
// ================================================================================

ZC_TEST("LexerIdentifierTest.IdentifierKeywordBoundary") {
  // "letx" should be a single identifier, not LetKeyword + Identifier(x)
  auto tokens = tokenize("letx"_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[0].getValue() == "letx"_zc);
}

ZC_TEST("LexerIdentifierTest.KeywordThenIdentifier") {
  // "let x" should be LetKeyword + Identifier(x)
  auto tokens = tokenize("let x"_zc);
  ZC_EXPECT(tokens.size() == 3);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::LetKeyword));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[1].getValue() == "x"_zc);
}

ZC_TEST("LexerIdentifierTest.NonKeywordHeritageSpellingsAreIdentifiers") {
  auto tokens = tokenize("extends implements"_zc);
  ZC_EXPECT(tokens.size() == 3);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[0].getValue() == "extends"_zc);
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[1].getValue() == "implements"_zc);
}

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
