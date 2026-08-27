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
#include "compiler/diagnostics/consumer/diagnostic-consumer.h"
#include "compiler/diagnostics/core/diagnostic-engine.h"
#include "compiler/lexer/lexer.h"
#include "compiler/source/manager.h"
#include "tests/unittests/compiler/lexer/utils.h"

namespace zomlang {
namespace compiler {
namespace lexer {

ZC_TEST("LexerLiteralTest.StringLiterals") {
  // Case 1: Empty double-quoted string
  {
    auto tokens = tokenize("\"\""_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::StringLiteral));
    ZC_EXPECT(tokens[0].getValue() == ""_zc);
  }

  // Case 2: Empty single-quoted literal is recoverable but invalid
  {
    auto& sourceManager = getSourceManager();
    auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);

    auto tokens = tokenize("''"_zc, *diagnosticEngine);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::StringLiteral));
    ZC_EXPECT(tokens[0].getValue() == ""_zc);
    ZC_EXPECT(diagnosticEngine->hasErrors());
  }

  // Case 3: Simple double-quoted string
  {
    auto tokens = tokenize("\"hello\""_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::StringLiteral));
    ZC_EXPECT(tokens[0].getValue() == "hello"_zc);
  }

  // Case 4: Single-quoted character literal uses the CharacterLiteral token
  {
    auto tokens = tokenize("'w'"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::CharacterLiteral));
    ZC_EXPECT(tokens[0].getValue() == "w"_zc);
  }

  // Case 5: Escaped quotes
  {
    auto tokens = tokenize("\"\\\"\""_zc);  // "\""
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::StringLiteral));
    ZC_EXPECT(tokens[0].getValue() == "\""_zc);

    auto tokens2 = tokenize("'\\''"_zc);  // '\''
    ZC_EXPECT(tokens2.size() == 2);
    ZC_EXPECT(tokens2[0].is(ast::SyntaxKind::CharacterLiteral));
    ZC_EXPECT(tokens2[0].getValue() == "'"_zc);
  }

  // Case 6: Mixed quotes
  {
    auto tokens = tokenize("\"'\""_zc);  // "'"
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::StringLiteral));
    ZC_EXPECT(tokens[0].getValue() == "'"_zc);

    auto tokens2 = tokenize("'\"'"_zc);  // '"'
    ZC_EXPECT(tokens2.size() == 2);
    ZC_EXPECT(tokens2[0].is(ast::SyntaxKind::CharacterLiteral));
    ZC_EXPECT(tokens2[0].getValue() == "\""_zc);
  }

  // Case 7: Common escapes
  {
    auto tokens = tokenize("\"\\n\\t\\r\\\\\""_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::StringLiteral));
    ZC_EXPECT(tokens[0].getValue() == "\n\t\r\\"_zc);
  }

  // Case 8: Unicode escape
  {
    // \u0041 is 'A'
    auto tokens = tokenize("\"\\u0041\""_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::StringLiteral));
    ZC_EXPECT(tokens[0].getValue() == "A"_zc);
  }

  // Case 9: Unicode content in string literal
  {
    auto tokens = tokenize("\"hello \xC3\xA9 world\""_zc);  // "hello e with acute"
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::StringLiteral));
    ZC_EXPECT(tokens[0].getValue() == "hello \xC3\xA9 world"_zc);
  }

  // Case 10: Unicode character literal (single multi-byte scalar)
  {
    auto tokens = tokenize("'\xC3\xA9'"_zc);  // e with acute
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::CharacterLiteral));
    ZC_EXPECT(tokens[0].getValue() == "\xC3\xA9"_zc);
  }

  // Case 11: CJK character literal
  {
    auto tokens = tokenize("'\xE4\xB8\xAD'"_zc);  // CJK ideograph
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::CharacterLiteral));
    ZC_EXPECT(tokens[0].getValue() == "\xE4\xB8\xAD"_zc);
  }

  // Case 12: Empty single-quoted literal (invalid - empty)
  {
    auto& sm = getSourceManager();
    auto diags = zc::heap<diagnostics::DiagnosticEngine>(sm);
    auto tokens = tokenize("''"_zc, *diags);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::StringLiteral));
    ZC_EXPECT(tokens[0].getValue() == ""_zc);
    ZC_EXPECT(diags->hasErrors());
  }

  // Case 13: Multi-scalar single-quoted literal (invalid - multiple code points)
  {
    auto& sm = getSourceManager();
    auto diags = zc::heap<diagnostics::DiagnosticEngine>(sm);
    auto tokens = tokenize("'ab'"_zc, *diags);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::StringLiteral));
    ZC_EXPECT(diags->hasErrors());
  }
}

ZC_TEST("LexerLiteralTest.MultiCharacterSingleQuotedLiteralReportsError") {
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);

  auto tokens = tokenize("'world'"_zc, *diagnosticEngine);

  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::StringLiteral));
  ZC_EXPECT(diagnosticEngine->hasErrors());
}

ZC_TEST("LexerLiteralTest.RejectsUnknownStringEscape") {
  class CaptureConsumer final : public diagnostics::DiagnosticConsumer {
  public:
    void handleDiagnostic(const source::SourceManager&,
                          const diagnostics::Diagnostic& diagnostic) override {
      if (diagnostic.getId() == diagnostics::DiagID::EscapeSequenceNotAllowed) { observed = true; }
    }

    bool observed = false;
  };

  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto consumer = zc::heap<CaptureConsumer>();
  const auto& retainedConsumer = *consumer;
  diagnosticEngine->addConsumer(zc::mv(consumer));

  auto tokens = tokenize("\"\\q\""_zc, *diagnosticEngine);
  ZC_REQUIRE(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::StringLiteral));
  ZC_EXPECT(tokens[0].hasFlag(TokenFlags::ContainsInvalidEscape));
  ZC_EXPECT(retainedConsumer.observed);
}

ZC_TEST("LexerLiteralTest.AcceptsEveryLineContinuation") {
  const zc::StringPtr sources[] = {
      "\"left\\\nright\""_zc,           "\"left\\\r\nright\""_zc,         "\"left\\\rright\""_zc,
      "\"left\\\xE2\x80\xA8right\""_zc, "\"left\\\xE2\x80\xA9right\""_zc,
  };
  for (const auto source : sources) {
    auto tokens = tokenize(source);
    ZC_REQUIRE(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::StringLiteral));
    ZC_EXPECT(tokens[0].getValue() == "leftright"_zc);
    ZC_EXPECT(!tokens[0].hasFlag(TokenFlags::ContainsInvalidEscape));
  }
}

ZC_TEST("LexerLiteralTest.OctalEscapeSequence") {
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);

  // Test case from user query: \47
  // In TypeScript:
  // parseInt("47", 8) -> 39 (0x27)
  // \x27 is '
  // Error message should use \x27

  // First run to check for errors
  tokenize("\"\\47\"", *diagnosticEngine);
  ZC_EXPECT(diagnosticEngine->hasErrors());

  // Create a custom consumer to capture the error message
  class CaptureConsumer : public diagnostics::DiagnosticConsumer {
  public:
    void handleDiagnostic(const source::SourceManager& sm,
                          const diagnostics::Diagnostic& diagnostic) override {
      if (diagnostic.getId() == diagnostics::DiagID::OctalEscapeSequencesNotAllowed) {
        // Capture the argument
        if (diagnostic.getArgs().size() > 0) {
          auto& arg = diagnostic.getArgs()[0];
          if (arg.is<zc::String>()) {
            capturedHex = zc::heapString(arg.get<zc::String>());
          } else if (arg.is<zc::StringPtr>()) {
            capturedHex = zc::heapString(arg.get<zc::StringPtr>());
          }
        }
      }
    }

    zc::String capturedHex;
  };

  auto consumer = zc::heap<CaptureConsumer>();
  const auto& consumerPtr = *consumer;
  diagnosticEngine->addConsumer(zc::mv(consumer));

  // Re-lex to trigger diagnostic again
  tokenize("\"\\47\"", *diagnosticEngine);

  // \47 -> 39 decimal -> 0x27 hex
  // Expected hex string in error: "27" (or "\\x27" depending on how it's formatted in the
  // diagnostic) The diagnostic format is: "Octal escape sequences are not allowed. Use the syntax
  // '\\x{0}'" So the argument should be "27".

  // If the current implementation is incorrect, it might be parsing "47" as decimal or doing
  // something else. TypeScript does `parseInt("47", 8)` which is 39. 39 in hex is 27.

  ZC_EXPECT(consumerPtr.capturedHex == "27");
}

ZC_TEST("LexerLiteralTest.TemplateLiterals") {
  // NoSubstitutionTemplateLiteral
  {
    auto tokens = tokenize("`hello world`"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::NoSubstitutionTemplateLiteral));
    ZC_EXPECT(tokens[0].getValue() == "hello world"_zc);
  }

  // TemplateHead
  {
    auto tokens = tokenize("`hello ${"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::TemplateHead));
    ZC_EXPECT(tokens[0].getValue() == "hello "_zc);
  }

  // TemplateExpression
  {
    auto tokens = tokenize("`hello ${name}`"_zc);
    ZC_EXPECT(tokens.size() == 4);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::TemplateHead));
    ZC_EXPECT(tokens[0].getValue() == "hello "_zc);
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
    ZC_EXPECT(tokens[1].getValue() == "name"_zc);
    ZC_EXPECT(tokens[2].is(ast::SyntaxKind::TemplateTail));
    ZC_EXPECT(tokens[2].getValue() == ""_zc);
  }

  // NestedTemplateExpression
  {
    auto tokens = tokenize("`outer ${`inner ${value}`}`"_zc);
    ZC_EXPECT(tokens.size() == 6);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::TemplateHead));
    ZC_EXPECT(tokens[0].getValue() == "outer "_zc);
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::TemplateHead));
    ZC_EXPECT(tokens[1].getValue() == "inner "_zc);
    ZC_EXPECT(tokens[2].is(ast::SyntaxKind::Identifier));
    ZC_EXPECT(tokens[2].getValue() == "value"_zc);
    ZC_EXPECT(tokens[3].is(ast::SyntaxKind::TemplateTail));
    ZC_EXPECT(tokens[3].getValue() == ""_zc);
    ZC_EXPECT(tokens[4].is(ast::SyntaxKind::TemplateTail));
    ZC_EXPECT(tokens[4].getValue() == ""_zc);
  }

  // TemplateSubstitutionBraceDepth
  {
    auto tokens = tokenize("`value ${call({ nested: value })} tail`"_zc);
    ZC_EXPECT(tokens.size() == 11);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::TemplateHead));
    ZC_EXPECT(tokens[0].getValue() == "value "_zc);
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
    ZC_EXPECT(tokens[1].getValue() == "call"_zc);
    ZC_EXPECT(tokens[2].is(ast::SyntaxKind::LeftParen));
    ZC_EXPECT(tokens[3].is(ast::SyntaxKind::LeftBrace));
    ZC_EXPECT(tokens[4].is(ast::SyntaxKind::Identifier));
    ZC_EXPECT(tokens[4].getValue() == "nested"_zc);
    ZC_EXPECT(tokens[5].is(ast::SyntaxKind::Colon));
    ZC_EXPECT(tokens[6].is(ast::SyntaxKind::Identifier));
    ZC_EXPECT(tokens[6].getValue() == "value"_zc);
    ZC_EXPECT(tokens[7].is(ast::SyntaxKind::RightBrace));
    ZC_EXPECT(tokens[8].is(ast::SyntaxKind::RightParen));
    ZC_EXPECT(tokens[9].is(ast::SyntaxKind::TemplateTail));
    ZC_EXPECT(tokens[9].getValue() == " tail"_zc);
  }

  // EmptyTemplateLiteral
  {
    auto tokens = tokenize("``"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::NoSubstitutionTemplateLiteral));
    ZC_EXPECT(tokens[0].getValue() == ""_zc);
  }

  // MultilineTemplateLiteral
  {
    auto tokens = tokenize("`line1\nline2`"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::NoSubstitutionTemplateLiteral));
    ZC_EXPECT(tokens[0].getValue() == "line1\nline2"_zc);
  }

  // UnterminatedTemplateLiteral
  {
    auto& sm = getSourceManager();
    auto diags = zc::heap<diagnostics::DiagnosticEngine>(sm);
    auto tokens = tokenize("`hello"_zc, diags.get());

    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::NoSubstitutionTemplateLiteral));
    ZC_EXPECT(tokens[0].hasFlag(TokenFlags::Unterminated));
    ZC_EXPECT(tokens[0].getValue() == "hello"_zc);
    ZC_EXPECT(diags->hasErrors());
  }

  // MultipleSubstitutions: `${a} + ${b} = ${c}`
  {
    auto tokens = tokenize("`${a} + ${b} = ${c}`"_zc);
    // Expected: TemplateHead(""), Identifier(a), TemplateMiddle(" + "),
    //           Identifier(b), TemplateMiddle(" = "), Identifier(c), TemplateTail(""), EOF
    ZC_EXPECT(tokens.size() == 8);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::TemplateHead));
    ZC_EXPECT(tokens[0].getValue() == ""_zc);
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
    ZC_EXPECT(tokens[1].getValue() == "a"_zc);
    ZC_EXPECT(tokens[2].is(ast::SyntaxKind::TemplateMiddle));
    ZC_EXPECT(tokens[2].getValue() == " + "_zc);
    ZC_EXPECT(tokens[3].is(ast::SyntaxKind::Identifier));
    ZC_EXPECT(tokens[3].getValue() == "b"_zc);
    ZC_EXPECT(tokens[4].is(ast::SyntaxKind::TemplateMiddle));
    ZC_EXPECT(tokens[4].getValue() == " = "_zc);
    ZC_EXPECT(tokens[5].is(ast::SyntaxKind::Identifier));
    ZC_EXPECT(tokens[5].getValue() == "c"_zc);
    ZC_EXPECT(tokens[6].is(ast::SyntaxKind::TemplateTail));
    ZC_EXPECT(tokens[6].getValue() == ""_zc);
    ZC_EXPECT(tokens[7].is(ast::SyntaxKind::EndOfFile));
  }

  // ObjectLiteralInSubstitution: `${{x: 1}}`
  {
    auto tokens = tokenize("`${{x: 1}}`"_zc);
    // The outer braces are template delimiters, inner braces are object literal.
    // Expected: TemplateHead(""), LeftBrace, Identifier(x), Colon,
    //           IntegerLiteral(1), RightBrace, TemplateTail(""), EOF
    ZC_EXPECT(tokens.size() == 8);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::TemplateHead));
    ZC_EXPECT(tokens[0].getValue() == ""_zc);
    ZC_EXPECT(tokens[1].is(ast::SyntaxKind::LeftBrace));
    ZC_EXPECT(tokens[2].is(ast::SyntaxKind::Identifier));
    ZC_EXPECT(tokens[2].getValue() == "x"_zc);
    ZC_EXPECT(tokens[3].is(ast::SyntaxKind::Colon));
    ZC_EXPECT(tokens[4].is(ast::SyntaxKind::IntegerLiteral));
    ZC_EXPECT(tokens[4].getValue() == "1"_zc);
    ZC_EXPECT(tokens[5].is(ast::SyntaxKind::RightBrace));
    ZC_EXPECT(tokens[6].is(ast::SyntaxKind::TemplateTail));
    ZC_EXPECT(tokens[6].getValue() == ""_zc);
    ZC_EXPECT(tokens[7].is(ast::SyntaxKind::EndOfFile));
  }
}

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
