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
#include "zomlang/tests/unittests/compiler/lexer/utils.h"

namespace zomlang {
namespace compiler {
namespace lexer {

ZC_TEST("LexerOperatorTest.ExclamationOperators") {
  // Case 1: Simple exclamation '!'
  {
    auto tokens = tokenize("!"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Exclamation));
  }

  // Case 2: Exclamation equals '!='
  {
    auto tokens = tokenize("!="_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::ExclamationEquals));
  }

  // Case 3: Exclamation equals equals '!=='
  {
    auto tokens = tokenize("!=="_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::ExclamationEqualsEquals));
  }

  // Case 4: Error unwrap '!!'
  {
    auto tokens = tokenize("!!"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::ErrorUnwrap));
  }

  // Case 5: Complex sequence '!!!' -> '!!' then '!'
  {
    auto tokens = tokenize("!!!"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::ErrorUnwrap));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Exclamation));
  }

  // Case 6: Complex sequence '!= =' -> '!=' then '='
  {
    auto tokens = tokenize("!= ="_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::ExclamationEquals));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Equals));
  }

  // Case 7: Complex sequence '!===' -> '!==' then '='
  {
    auto tokens = tokenize("!==="_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::ExclamationEqualsEquals));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Equals));
  }
}

ZC_TEST("LexerOperatorTest.PercentOperators") {
  // Case 1: Simple percent '%'
  {
    auto tokens = tokenize("%"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Percent));
  }

  // Case 2: Percent equals '%='
  {
    auto tokens = tokenize("%="_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::PercentEquals));
  }

  // Case 3: Complex sequence '% =' -> '%' then '='
  {
    auto tokens = tokenize("% ="_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Percent));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Equals));
  }
}

ZC_TEST("LexerOperatorTest.AmpersandOperators") {
  // Case 1: Simple ampersand '&'
  {
    auto tokens = tokenize("&"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Ampersand));
  }

  // Case 2: Ampersand equals '&='
  {
    auto tokens = tokenize("&="_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::AmpersandEquals));
  }

  // Case 3: Ampersand ampersand '&&'
  {
    auto tokens = tokenize("&&"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::AmpersandAmpersand));
  }

  // Case 4: Ampersand ampersand equals '&&='
  {
    auto tokens = tokenize("&&="_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::AmpersandAmpersandEquals));
  }

  // Case 5: Complex sequence '&&&' -> '&&' then '&'
  {
    auto tokens = tokenize("&&&"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::AmpersandAmpersand));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Ampersand));
  }
}

ZC_TEST("LexerOperatorTest.Parentheses") {
  // Case 1: Left Paren '('
  {
    auto tokens = tokenize("("_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::LeftParen));
  }

  // Case 2: Right Paren ')'
  {
    auto tokens = tokenize(")"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::RightParen));
  }

  // Case 3: Parentheses sequence '()'
  {
    auto tokens = tokenize("()"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::LeftParen));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::RightParen));
  }

  // Case 4: Nested parentheses '(())'
  {
    auto tokens = tokenize("(())"_zc);
    ZC_EXPECT(tokens.size() == 5);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::LeftParen));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::LeftParen));
    ZC_EXPECT(tokens[2].is(ast::SyntaxKind::RightParen));
    ZC_EXPECT(tokens[3].is(ast::SyntaxKind::RightParen));
  }
}

ZC_TEST("LexerOperatorTest.AsteriskOperators") {
  // Case 1: Simple asterisk '*'
  {
    auto tokens = tokenize("*"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Asterisk));
  }

  // Case 2: Asterisk equals '*='
  {
    auto tokens = tokenize("*="_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::AsteriskEquals));
  }

  // Case 3: Asterisk asterisk '**'
  {
    auto tokens = tokenize("**"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::AsteriskAsterisk));
  }

  // Case 4: Asterisk asterisk equals '**='
  {
    auto tokens = tokenize("**="_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::AsteriskAsteriskEquals));
  }

  // Case 5: Complex sequence '***' -> '**' then '*'
  {
    auto tokens = tokenize("***"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::AsteriskAsterisk));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Asterisk));
  }
}

ZC_TEST("LexerOperatorTest.PlusOperators") {
  // Case 1: Simple plus '+'
  {
    auto tokens = tokenize("+"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Plus));
  }

  // Case 2: Plus plus '++'
  {
    auto tokens = tokenize("++"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::PlusPlus));
  }

  // Case 3: Plus equals '+='
  {
    auto tokens = tokenize("+="_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::PlusEquals));
  }

  // Case 4: Complex sequence '+++' -> '++' then '+'
  {
    auto tokens = tokenize("+++"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::PlusPlus));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Plus));
  }
}

ZC_TEST("LexerOperatorTest.CommaOperator") {
  // Case 1: Simple comma ','
  {
    auto tokens = tokenize(","_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Comma));
  }

  // Case 2: Multiple commas ',,'
  {
    auto tokens = tokenize(",,"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Comma));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Comma));
  }
}

ZC_TEST("LexerOperatorTest.MinusOperators") {
  // Case 1: Simple minus '-'
  {
    auto tokens = tokenize("-"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Minus));
  }

  // Case 2: Minus minus '--'
  {
    auto tokens = tokenize("--"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::MinusMinus));
  }

  // Case 3: Minus equals '-='
  {
    auto tokens = tokenize("-="_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::MinusEquals));
  }

  // Case 4: Arrow '->'
  {
    auto tokens = tokenize("->"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Arrow));
  }

  // Case 5: Complex sequence '-->' -> '--' then '>'
  // Note: Lexer prioritizes longest match starting at current position.
  // at pos 0: '-' -> next is '-'. matches '--' (MinusMinus).
  // remaining: '>'
  {
    auto tokens = tokenize("-->"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::MinusMinus));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::GreaterThan));
  }

  // Case 6: Complex sequence '->-' -> '->' then '-'
  {
    auto tokens = tokenize("->-"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Arrow));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Minus));
  }
}

ZC_TEST("LexerOperatorTest.DotOperators") {
  // Case 1: Simple dot '.'
  {
    auto tokens = tokenize("."_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Period));
  }

  // Case 2: Ellipsis '...'
  {
    auto tokens = tokenize("..."_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::DotDotDot));
  }

  // Case 3: Dot followed by digit (should be parsed as number)
  {
    auto tokens = tokenize(".123"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::FloatLiteral));
  }

  // Case 4: Complex sequence '....' -> '...' then '.'
  {
    auto tokens = tokenize("...."_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::DotDotDot));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Period));
  }

  // Case 5: Complex sequence '.. .' -> '.' then '.' then '.'
  {
    auto tokens = tokenize(".. ."_zc);
    ZC_EXPECT(tokens.size() == 4);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Period));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Period));
    ZC_EXPECT(tokens[2].is(ast::SyntaxKind::Period));
  }
}

ZC_TEST("LexerOperatorTest.SlashOperators") {
  // Case 1: Slash '/'
  {
    auto tokens = tokenize("/"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Slash));
  }

  // Case 2: Slash equals '/='
  {
    auto tokens = tokenize("/="_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::SlashEquals));
  }

  // Case 3: Single-line comment (should be skipped)
  {
    auto tokens = tokenize("// comment"_zc);
    ZC_EXPECT(tokens.size() == 1);  // Only EOF
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::EndOfFile));
  }

  // Case 4: Single-line comment followed by token
  {
    auto tokens = tokenize("// comment\na"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
    ZC_EXPECT(tokens[0].hasPrecedingLineBreak());
  }

  // Case 5: Multi-line comment (should be skipped)
  {
    auto tokens = tokenize("/* comment */"_zc);
    ZC_EXPECT(tokens.size() == 1);  // Only EOF
  }

  // Case 6: Multi-line comment followed by token
  {
    auto tokens = tokenize("/* comment */a"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
    ZC_EXPECT(!tokens[0].hasPrecedingLineBreak());
  }

  // Case 7: Multi-line comment with newline followed by token
  {
    auto tokens = tokenize("/* comment \n */a"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
    ZC_EXPECT(tokens[0].hasPrecedingLineBreak());
  }

  // Case 8: Nested slash in comment (should be ignored)
  {
    auto tokens = tokenize("/* / */"_zc);
    ZC_EXPECT(tokens.size() == 1);
  }

  // Case 9: Slash start but not comment
  {
    auto tokens = tokenize("/ a"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Slash));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
  }
}

ZC_TEST("LexerOperatorTest.CommentDirectives") {
  // Case 1: Single-line comment directive 'zom-expect-error'
  {
    auto directives = tokenizeAndGetDirectives("// @zom-expect-error"_zc);
    ZC_EXPECT(directives.size() == 1);
    ZC_EXPECT(directives[0].kind == CommentDirectiveKind::ExpectError);
  }

  // Case 2: Single-line comment directive 'zom-ignore'
  {
    auto directives = tokenizeAndGetDirectives("// @zom-ignore"_zc);
    ZC_EXPECT(directives.size() == 1);
    ZC_EXPECT(directives[0].kind == CommentDirectiveKind::Ignore);
  }

  // Case 3: Single-line comment directive with surrounding text (should be ignored if not at
  // start?) Logic check: processCommentDirective skips whitespace then checks for @. "Skip
  // whitespace", then "Directive must start with '@'". So "//  @zom-expect-error" should work.
  // "// foo @zom-expect-error" should NOT work.
  {
    auto directives = tokenizeAndGetDirectives("//   @zom-expect-error"_zc);
    ZC_EXPECT(directives.size() == 1);
    ZC_EXPECT(directives[0].kind == CommentDirectiveKind::ExpectError);
  }

  {
    auto directives = tokenizeAndGetDirectives("// foo @zom-expect-error"_zc);
    ZC_EXPECT(directives.size() == 0);
  }

  // Case 4: Multi-line comment directive
  {
    auto directives = tokenizeAndGetDirectives("/* @zom-expect-error */"_zc);
    ZC_EXPECT(directives.size() == 1);
    ZC_EXPECT(directives[0].kind == CommentDirectiveKind::ExpectError);
  }

  // Case 5: Multi-line comment directive with newlines
  {
    auto directives = tokenizeAndGetDirectives("/* \n @zom-ignore \n */"_zc);
    ZC_EXPECT(directives.size() == 0);
  }

  // Case 6: Directive with extra text
  {
    auto directives = tokenizeAndGetDirectives("// @zom-expect-error: some reason"_zc);
    ZC_EXPECT(directives.size() == 1);
    ZC_EXPECT(directives[0].kind == CommentDirectiveKind::ExpectError);
  }
}

ZC_TEST("LexerOperatorTest.ColonOperator") {
  // Case 1: Simple colon ':'
  {
    auto tokens = tokenize(":"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Colon));
  }

  // Case 2: Colon followed by identifier ':foo'
  {
    auto tokens = tokenize(":foo"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Colon));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
  }
}

ZC_TEST("LexerOperatorTest.SemicolonOperator") {
  // Case 1: Simple semicolon ';'
  {
    auto tokens = tokenize(";"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Semicolon));
  }

  // Case 2: Multiple semicolons ';;'
  {
    auto tokens = tokenize(";;"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Semicolon));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Semicolon));
  }
}

ZC_TEST("LexerOperatorTest.LessThanOperators") {
  // Case 1: Simple less than '<'
  {
    auto tokens = tokenize("<"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::LessThan));
  }

  // Case 2: Less than equals '<='
  {
    auto tokens = tokenize("<="_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::LessThanEquals));
  }

  // Case 3: Less than less than '<<'
  {
    auto tokens = tokenize("<<"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::LessThanLessThan));
  }

  // Case 4: Less than less than equals '<<='
  {
    auto tokens = tokenize("<<="_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::LessThanLessThanEquals));
  }

  // Case 5: Complex sequence '<<<' -> '<<' then '<'
  {
    auto tokens = tokenize("<<<"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::LessThanLessThan));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::LessThan));
  }

  // Case 6: Complex sequence '< <=' -> '<' then '<='
  {
    auto tokens = tokenize("< <="_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::LessThan));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::LessThanEquals));
  }
}

ZC_TEST("LexerOperatorTest.EqualsOperators") {
  // Case 1: Simple equals '='
  {
    auto tokens = tokenize("="_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Equals));
  }

  // Case 2: Equals equals '=='
  {
    auto tokens = tokenize("=="_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::EqualsEquals));
  }

  // Case 3: Equals equals equals '==='
  {
    auto tokens = tokenize("==="_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::EqualsEqualsEquals));
  }

  // Case 4: Fat arrow '=>'
  {
    auto tokens = tokenize("=>"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::EqualsGreaterThan));
  }

  // Case 5: Complex sequence '====' -> '===' then '='
  {
    auto tokens = tokenize("===="_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::EqualsEqualsEquals));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Equals));
  }

  // Case 6: Complex sequence '==>' -> '==' then '>'
  {
    auto tokens = tokenize("==>"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::EqualsEquals));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::GreaterThan));
  }
}

ZC_TEST("LexerOperatorTest.GreaterThanOperators") {
  // Case 1: Simple greater than '>'
  {
    auto tokens = tokenize(">"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::GreaterThan));
  }

  // Case 2: Greater than equals '>='
  {
    auto tokens = tokenize(">="_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::GreaterThanEquals));
  }

  // Case 3: Greater than greater than '>>'
  {
    auto tokens = tokenize(">>"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::GreaterThanGreaterThan));
  }

  // Case 4: Greater than greater than equals '>>='
  {
    auto tokens = tokenize(">>="_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::GreaterThanGreaterThanEquals));
  }

  // Case 5: Unsigned right shift '>>>'
  {
    auto tokens = tokenize(">>>"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::GreaterThanGreaterThanGreaterThan));
  }

  // Case 6: Complex sequence '> >=' -> '>' then '>='
  {
    auto tokens = tokenize("> >="_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::GreaterThan));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::GreaterThanEquals));
  }
}

ZC_TEST("LexerOperatorTest.QuestionOperators") {
  // Case 1: Simple question '?'
  {
    auto tokens = tokenize("?"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Question));
  }

  // Case 2: Question question '??'
  {
    auto tokens = tokenize("\?\?"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::QuestionQuestion));
  }

  // Case 3: Question question equals '??='
  {
    auto tokens = tokenize("\?\?="_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::QuestionQuestionEquals));
  }

  // Case 4: Question dot with digit '?.1'
  // Based on code: does NOT match QuestionDot if charAt(2) is digit.
  {
    auto tokens = tokenize("?.1"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Question));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::FloatLiteral));
  }

  // Case 5: Question dot without digit '?.a'
  // Should match QuestionDot
  {
    auto tokens = tokenize("?.a"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::QuestionDot));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
  }
}

ZC_TEST("LexerOperatorTest.BracketOperators") {
  // Case 1: Left Bracket '['
  {
    auto tokens = tokenize("["_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::LeftBracket));
  }

  // Case 2: Bracket sequence '[['
  {
    auto tokens = tokenize("[["_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::LeftBracket));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::LeftBracket));
  }

  // Case 3: Right Bracket ']'
  {
    auto tokens = tokenize("]"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::RightBracket));
  }

  // Case 4: Bracket pair '[]'
  {
    auto tokens = tokenize("[]"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::LeftBracket));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::RightBracket));
  }
}

ZC_TEST("LexerOperatorTest.CaretOperators") {
  // Case 1: Simple caret '^'
  {
    auto tokens = tokenize("^"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Caret));
  }

  // Case 2: Caret equals '^='
  {
    auto tokens = tokenize("^="_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::CaretEquals));
  }

  // Case 3: Complex sequence '^ =' -> '^' then '='
  {
    auto tokens = tokenize("^ ="_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Caret));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Equals));
  }
}

ZC_TEST("LexerOperatorTest.BraceOperators") {
  // Case 1: Left Brace '{'
  {
    auto tokens = tokenize("{"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::LeftBrace));
  }

  // Case 2: Brace sequence '{{'
  {
    auto tokens = tokenize("{{"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::LeftBrace));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::LeftBrace));
  }

  // Case 3: Right Brace '}'
  {
    auto tokens = tokenize("}"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::RightBrace));
  }

  // Case 4: Brace pair '{}'
  {
    auto tokens = tokenize("{}"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::LeftBrace));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::RightBrace));
  }
}

ZC_TEST("LexerOperatorTest.BarOperators") {
  // Case 1: Simple bar '|'
  {
    auto tokens = tokenize("|"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Bar));
  }

  // Case 2: Bar equals '|='
  {
    auto tokens = tokenize("|="_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::BarEquals));
  }

  // Case 3: Bar bar '||'
  {
    auto tokens = tokenize("||"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::BarBar));
  }

  // Case 4: Bar bar equals '||='
  {
    auto tokens = tokenize("||="_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::BarBarEquals));
  }

  // Case 5: Complex sequence '|||' -> '||' then '|'
  {
    auto tokens = tokenize("|||"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::BarBar));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Bar));
  }

  // Case 6: Complex sequence '| ||' -> '|' then '||'
  {
    auto tokens = tokenize("| ||"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Bar));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::BarBar));
  }
}

ZC_TEST("LexerOperatorTest.TildeOperator") {
  // Case 1: Simple tilde '~'
  {
    auto tokens = tokenize("~"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Tilde));
  }

  // Case 2: Double tilde '~~'
  {
    auto tokens = tokenize("~~"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Tilde));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Tilde));
  }
}

ZC_TEST("LexerOperatorTest.AtOperator") {
  // Case 1: Simple at '@'
  {
    auto tokens = tokenize("@"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::At));
  }

  // Case 2: Double at '@@'
  {
    auto tokens = tokenize("@@"_zc);
    ZC_EXPECT(tokens.size() == 3);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::At));
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::At));
  }
}

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
