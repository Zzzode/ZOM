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

#include "zomlang/compiler/lexer/token.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/tests/unittests/compiler/lexer/utils.h"

namespace zomlang {
namespace compiler {
namespace lexer {

ZC_TEST("TokenTest.ConstructorsAndAssignment") {
  source::SourceLoc loc1 = source::SourceLoc::getFromOpaqueValue((const zc::byte*)10);
  source::SourceLoc loc2 = source::SourceLoc::getFromOpaqueValue((const zc::byte*)20);
  source::SourceRange range(loc1, loc2);

  // Default constructor
  Token t1;
  ZC_EXPECT(t1.is(ast::SyntaxKind::Unknown));
  ZC_EXPECT(t1.getFlags() == TokenFlags::None);

  // Constructor with arguments
  Token t2(ast::SyntaxKind::Identifier, range, "test"_zc, TokenFlags::StringLiteralFlags);
  ZC_EXPECT(t2.is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(t2.getRange().getStart() == range.getStart());
  ZC_EXPECT(t2.getRange().getEnd() == range.getEnd());
  ZC_EXPECT(t2.getValue() == "test"_zc);

  ZC_EXPECT(t2.hasFlag(TokenFlags::StringLiteralFlags));

  // Copy constructor
  Token t3(t2);
  ZC_EXPECT(t3.is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(t3.getRange().getStart() == range.getStart());
  ZC_EXPECT(t3.getRange().getEnd() == range.getEnd());
  ZC_EXPECT(t3.getValue() == "test"_zc);

  ZC_EXPECT(t3.hasFlag(TokenFlags::StringLiteralFlags));

  // Move constructor
  Token t4(zc::mv(t2));
  ZC_EXPECT(t4.is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(t4.getRange().getStart() == range.getStart());
  ZC_EXPECT(t4.getRange().getEnd() == range.getEnd());
  ZC_EXPECT(t4.getValue() == "test"_zc);

  ZC_EXPECT(t4.hasFlag(TokenFlags::StringLiteralFlags));
  // t2 is now in a moved-from state, we shouldn't use it generally, but for coverage we might check
  // it doesn't crash? Standard move semantics usually leave valid but unspecified state.

  // Copy assignment
  Token t5;
  t5 = t3;
  ZC_EXPECT(t5.is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(t5.getRange().getStart() == range.getStart());
  ZC_EXPECT(t5.getRange().getEnd() == range.getEnd());
  ZC_EXPECT(t5.getValue() == "test"_zc);

  // Move assignment
  Token t6;
  t6 = zc::mv(t3);
  ZC_EXPECT(t6.is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(t6.getRange().getStart() == range.getStart());
  ZC_EXPECT(t6.getRange().getEnd() == range.getEnd());
  ZC_EXPECT(t6.getValue() == "test"_zc);

  // Self assignment check (mostly to ensure no crash/correctness)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-assign-overloaded"
  t6 = t6;
#pragma clang diagnostic pop
  ZC_EXPECT(t6.is(ast::SyntaxKind::Identifier));

  Token t7;
  t7 = zc::mv(t7);  // Self move assignment
  ZC_EXPECT(t7.is(ast::SyntaxKind::Unknown));
}

ZC_TEST("TokenTest.SettersAndGetters") {
  Token t;
  source::SourceLoc loc1 = source::SourceLoc::getFromOpaqueValue((const zc::byte*)100);
  source::SourceLoc loc2 = source::SourceLoc::getFromOpaqueValue((const zc::byte*)105);
  source::SourceRange range(loc1, loc2);

  t.setKind(ast::SyntaxKind::IntegerLiteral);
  ZC_EXPECT(t.getKind() == ast::SyntaxKind::IntegerLiteral);
  ZC_EXPECT(t.is(ast::SyntaxKind::IntegerLiteral));

  t.setRange(range);
  ZC_EXPECT(t.getRange().getStart() == range.getStart());
  ZC_EXPECT(t.getRange().getEnd() == range.getEnd());
  ZC_EXPECT(t.getLocation() == range.getStart());

  t.setValue("123"_zc);
  ZC_EXPECT(t.getValue() == "123"_zc);

  t.setFlags(TokenFlags::Octal);
  ZC_EXPECT(t.getFlags() == TokenFlags::Octal);
  ZC_EXPECT(t.hasFlag(TokenFlags::Octal));

  t.addFlag(TokenFlags::ContainsLeadingZero);
  ZC_EXPECT(t.hasFlag(TokenFlags::Octal));
  ZC_EXPECT(t.hasFlag(TokenFlags::ContainsLeadingZero));

  // Test PrecedingLineBreak helper
  t.setFlags(TokenFlags::PrecedingLineBreak);
  ZC_EXPECT(t.hasPrecedingLineBreak());

  t.setFlags(TokenFlags::None);
  ZC_EXPECT(!t.hasPrecedingLineBreak());
}

ZC_TEST("TokenTest.StaticText") {
  // Sample check for keywords
  ZC_IF_SOME(text, Token::getStaticTextForTokenKind(ast::SyntaxKind::LetKeyword)) {
    ZC_EXPECT(text == "let"_zc);
  }
  else { ZC_FAIL_ASSERT("Expected static text for LetKeyword"); }

  ZC_IF_SOME(text, Token::getStaticTextForTokenKind(ast::SyntaxKind::ReturnKeyword)) {
    ZC_EXPECT(text == "return"_zc);
  }
  else { ZC_FAIL_ASSERT("Expected static text for ReturnKeyword"); }

  // Sample check for operators
  ZC_IF_SOME(text, Token::getStaticTextForTokenKind(ast::SyntaxKind::Plus)) {
    ZC_EXPECT(text == "+"_zc);
  }
  else { ZC_FAIL_ASSERT("Expected static text for Plus"); }

  ZC_IF_SOME(text, Token::getStaticTextForTokenKind(ast::SyntaxKind::Arrow)) {
    ZC_EXPECT(text == "->"_zc);
  }
  else { ZC_FAIL_ASSERT("Expected static text for Arrow"); }

  // Check for punctuation
  ZC_IF_SOME(text, Token::getStaticTextForTokenKind(ast::SyntaxKind::LeftBrace)) {
    ZC_EXPECT(text == "{"_zc);
  }
  else { ZC_FAIL_ASSERT("Expected static text for LeftBrace"); }

  // Check for non-static text kinds
  ZC_EXPECT(Token::getStaticTextForTokenKind(ast::SyntaxKind::Identifier) == zc::none);
  ZC_EXPECT(Token::getStaticTextForTokenKind(ast::SyntaxKind::IntegerLiteral) == zc::none);

  // Check default case (empty string in impl)
  ZC_EXPECT(Token::getStaticTextForTokenKind(ast::SyntaxKind::Unknown) == zc::none);
}

ZC_TEST("TokenTest.GetValue") {
  // Case 1: Token with explicit value
  {
    Token t;
    t.setValue("cached"_zc);
    ZC_EXPECT(t.getValue() == "cached"_zc);
  }

  // Case 2: Token with static text value
  {
    Token t;
    t.setKind(ast::SyntaxKind::LetKeyword);
    ZC_EXPECT(t.getValue() == "let"_zc);
  }

  // Case 3: Identifier token must carry its value explicitly
  {
    Token t;
    t.setKind(ast::SyntaxKind::Identifier);
    t.setValue("x"_zc);
    ZC_EXPECT(t.getValue() == "x"_zc);
  }
}

ZC_TEST("TokenTest.FlagOperations") {
  Token t;

  // Test basic flag operations
  t.setFlags(TokenFlags::None);
  ZC_EXPECT(t.getFlags() == TokenFlags::None);

  t.addFlag(TokenFlags::Unterminated);
  ZC_EXPECT(t.hasFlag(TokenFlags::Unterminated));
  ZC_EXPECT(t.getFlags() == TokenFlags::Unterminated);

  t.addFlag(TokenFlags::Scientific);
  ZC_EXPECT(t.hasFlag(TokenFlags::Unterminated));
  ZC_EXPECT(t.hasFlag(TokenFlags::Scientific));

  // Test bitwise operators for TokenFlags
  TokenFlags f1 = TokenFlags::Octal;
  TokenFlags f2 = TokenFlags::HexSpecifier;

  TokenFlags f3 = f1 | f2;
  ZC_EXPECT(hasFlag(f3, TokenFlags::Octal));
  ZC_EXPECT(hasFlag(f3, TokenFlags::HexSpecifier));

  TokenFlags f4 = f3 & TokenFlags::Octal;
  ZC_EXPECT(f4 == TokenFlags::Octal);

  TokenFlags f5 = f1 ^ f2;
  ZC_EXPECT(hasFlag(f5, TokenFlags::Octal));
  ZC_EXPECT(hasFlag(f5, TokenFlags::HexSpecifier));

  TokenFlags f6 = ~TokenFlags::None;
  ZC_EXPECT(f6 != TokenFlags::None);

  TokenFlags f7 = TokenFlags::None;
  f7 |= TokenFlags::Octal;
  ZC_EXPECT(f7 == TokenFlags::Octal);

  f7 &= TokenFlags::None;
  ZC_EXPECT(f7 == TokenFlags::None);

  f7 ^= TokenFlags::HexSpecifier;
  ZC_EXPECT(f7 == TokenFlags::HexSpecifier);
}

ZC_TEST("TokenTest.OperatorStaticText") {
  // Test a few more operators to ensure coverage of the switch statement
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::Minus)) == "-"_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::Asterisk)) ==
            "*"_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::Slash)) == "/"_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::Percent)) ==
            "%"_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::Equals)) == "="_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::EqualsEquals)) ==
            "=="_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(
                Token::getStaticTextForTokenKind(ast::SyntaxKind::ExclamationEquals)) == "!="_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::LessThan)) ==
            "<"_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::GreaterThan)) ==
            ">"_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(
                Token::getStaticTextForTokenKind(ast::SyntaxKind::AmpersandAmpersand)) == "&&"_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::BarBar)) ==
            "||"_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::Exclamation)) ==
            "!"_zc);

  // Test some keywords
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::IfKeyword)) ==
            "if"_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::ElseKeyword)) ==
            "else"_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::WhileKeyword)) ==
            "while"_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::ForKeyword)) ==
            "for"_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::TrueKeyword)) ==
            "true"_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::FalseKeyword)) ==
            "false"_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::NullKeyword)) ==
            "null"_zc);

  // Test types
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::I32Keyword)) ==
            "i32"_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::F64Keyword)) ==
            "f64"_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::StrKeyword)) ==
            "str"_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::BoolKeyword)) ==
            "bool"_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::UnitKeyword)) ==
            "unit"_zc);

  // Test punctuation
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::Semicolon)) ==
            ";"_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::Comma)) == ","_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::Period)) == "."_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::LeftParen)) ==
            "("_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::RightParen)) ==
            ")"_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::LeftBracket)) ==
            "["_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(Token::getStaticTextForTokenKind(ast::SyntaxKind::RightBracket)) ==
            "]"_zc);
}

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
