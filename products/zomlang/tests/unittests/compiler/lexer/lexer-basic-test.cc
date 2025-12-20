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
#include "zomlang/compiler/source/manager.h"
#include "zomlang/tests/unittests/compiler/lexer/utils.h"

namespace zomlang {
namespace compiler {
namespace lexer {

ZC_TEST("LexerBasicTest.WhitespaceAndLineBreaks") {
  // Case 1: Spaces only (no line break)
  {
    auto tokens = tokenize("a b"_zc);
    ZC_EXPECT(tokens.size() == 3);  // a, b, EOF
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
    ZC_EXPECT(!tokens[0].hasPrecedingLineBreak());
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
    ZC_EXPECT(!tokens[1].hasPrecedingLineBreak());
  }

  // Case 2: Simple newline
  {
    auto tokens = tokenize("a\nb"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
    ZC_EXPECT(tokens[1].hasPrecedingLineBreak());
  }

  // Case 3: Newline then space
  {
    auto tokens = tokenize("a\n  b"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[1].hasPrecedingLineBreak());
  }

  // Case 4: Space then newline
  {
    auto tokens = tokenize("a  \nb"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[1].hasPrecedingLineBreak());
  }

  // Case 5: Multiple newlines
  {
    auto tokens = tokenize("a\n\n\nb"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[1].hasPrecedingLineBreak());
  }

  // Case 6: Carriage Return
  {
    auto tokens = tokenize("a\rb"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[1].hasPrecedingLineBreak());
  }

  // Case 7: Tabs (no line break)
  {
    auto tokens = tokenize("a\tb"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(!tokens[1].hasPrecedingLineBreak());
  }

  // Case 8: Vertical Tab (no line break treated as whitespace in this lexer)
  {
    auto tokens = tokenize("a\vb"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(!tokens[1].hasPrecedingLineBreak());
  }

  // Case 9: Form Feed (no line break)
  {
    auto tokens = tokenize("a\fb"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(!tokens[1].hasPrecedingLineBreak());
  }

  // Case 10: Mixed whitespace without newline
  {
    auto tokens = tokenize("a \t \v \f b"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(!tokens[1].hasPrecedingLineBreak());
  }

  // Case 11: Mixed whitespace with newline
  {
    auto tokens = tokenize("a \t \n \v \f b"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[1].hasPrecedingLineBreak());
  }
}

ZC_TEST("LexerBasicTest.UnicodeEscapeIdentifier") {
  auto& sm = getSourceManager();

  // Case 1: Valid unicode escape start
  // \u0061 is 'a'
  {
    auto tokens = tokenize("\\u0061"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
    ZC_EXPECT(tokens[0].getText(sm) == "\u0061");
    ZC_EXPECT(tokens[0].getText(sm) == "a");
  }

  // Case 2: Unicode escape combined with normal chars
  // \u0061b is 'ab'
  {
    auto tokens = tokenize("\\u0061b"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
    ZC_EXPECT(tokens[0].getText(sm) == "\u0061b");
    ZC_EXPECT(tokens[0].getText(sm) == "ab");
  }

  // Case 3: Invalid unicode escape (not identifier start)
  // \u0030 is '0', which cannot start an identifier
  // Should lex as Unknown/Invalid if it falls through or fails isIdentifierStart check
  // Based on code: if (cp >= 0 && isIdentifierStart(cp)) ... else return lexInvalidCharacter()
  {
    auto tokens = tokenize("\\u0030"_zc);
    // When lexInvalidCharacter is called, it consumes one character (the backslash) and forms an
    // Unknown token. Then it continues lexing the rest "u0030" which is an identifier. So we expect
    // 3 tokens: Unknown(\), Identifier(u0030), EndOfFile
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Unknown));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
    ZC_EXPECT(tokens[1].getText(sm) == "u0030");
  }
}

ZC_TEST("LexerBasicTest.HashAndShebang") {
  auto& sm = getSourceManager();

  // Case 1: #! at start of file (Shebang) -> ignored as comment
  {
    // "#!/usr/bin/env zom\nvar a"
    // Should tokenize as: var, a, EOF
    auto tokens = tokenize("#!/usr/bin/env zom\nlet a"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
    ZC_EXPECT(tokens[1].getText(sm) == "a");
  }

  // Case 2: #! not at start of file -> Error + Unknown token
  {
    // " #!" (space before #!)
    // Should tokenize as: Unknown(#), Exclamation(!), EOF
    auto tokens = tokenize(" #!"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Unknown));
    ZC_EXPECT(tokens[0].getText(sm) == "#"_zc);
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Exclamation));
    ZC_EXPECT(tokens[1].getText(sm) == "!"_zc);
  }

  // Case 3: # alone (Hash)
  {
    auto tokens = tokenize("#"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Hash));
    ZC_EXPECT(tokens[0].getText(sm) == "#");
  }
}

ZC_TEST("LexerBasicTest.BinaryFileDetection") {
  // Invalid UTF-8 sequence: 0xFF
  // Should trigger FileAppearsToBeBinary error and return NonTextFileMarker
  char invalidBytes[] = {'\xFF', 0};
  auto tokens = tokenize(zc::StringPtr(invalidBytes, 1));

  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::NonTextFileMarker));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerBasicTest.UnicodeIdentifiersDefaultCase") {
  auto& sm = getSourceManager();
  // "你好" - valid unicode identifier
  {
    auto tokens = tokenize("你好"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
    ZC_EXPECT(tokens[0].getText(sm) == "你好");
  }
}

ZC_TEST("LexerBasicTest.InvalidCharacter") {
  // Null byte \0 should be invalid
  char nullByte[] = {'\0', 0};
  auto tokens = tokenize(zc::StringPtr(nullByte, 1));
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerBasicTest.UnicodeWhitespace") {
  auto& sm = getSourceManager();
  // \u00A0 is NBSP (No-Break Space). Should be treated as whitespace (skipped).
  // "a\u00A0b" -> Identifier(a), Identifier(b)
  auto tokens = tokenize("a\u00A0b"_zc);
  ZC_EXPECT(tokens.size() == 3);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[0].getText(sm) == "a");
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[1].getText(sm) == "b");
}

ZC_TEST("LexerBasicTest.UnicodeLineBreak") {
  // \u2028 is Line Separator. Should be treated as line break.
  // "a\u2028b" -> Identifier(a), Identifier(b) with PrecedingLineBreak
  auto tokens = tokenize("a\u2028b"_zc);
  ZC_EXPECT(tokens.size() == 3);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[1].hasPrecedingLineBreak());
}

ZC_TEST("LexerBasicTest.TokenTextFastPaths") {
  auto& sm = getSourceManager();

  // Value fast path: identifiers cache their value in Token::value.
  {
    auto tokens = tokenize("abc"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
    ZC_IF_SOME(value, tokens[0].getValue()) { ZC_EXPECT(value == "abc"_zc); }
    else { ZC_FAIL_EXPECT("Expected token value for identifier"); }
    ZC_EXPECT(tokens[0].getText(sm) == "abc"_zc);
  }

  // Static text fast path: keywords/operators should return static text.
  {
    auto tokens = tokenize("let +"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::LetKeyword));
    ZC_EXPECT(tokens[0].getText(sm) == "let"_zc);
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Plus));
    ZC_EXPECT(tokens[1].getText(sm) == "+"_zc);
  }

  // Unknown kind: getStaticTextForTokenKind should return none.
  { ZC_EXPECT(Token::getStaticTextForTokenKind(ast::SyntaxKind::Identifier) == zc::none); }

  // getTextWithBufferHint: null hint falls back to range extraction.
  // Use a manually constructed token with a known range and no cached value.
  {
    auto bufferId = sm.addMemBufferCopy("xyz"_zc.asBytes(), "token_text_test.zom");
    auto startLoc = sm.getLocForOffset(bufferId, 0);
    auto endLoc = sm.getLocForOffset(bufferId, 3);
    auto tokenRange = source::SourceRange(startLoc, endLoc);
    Token token(ast::SyntaxKind::Identifier, tokenRange, zc::none);

    ZC_EXPECT(token.getTextWithBufferHint(sm, nullptr) == "xyz"_zc);

    const source::BufferId bufferHint(bufferId);
    ZC_EXPECT(token.getTextWithBufferHint(sm, &bufferHint) == "xyz"_zc);
  }
}

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
