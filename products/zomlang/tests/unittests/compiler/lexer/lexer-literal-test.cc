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
#include "zomlang/compiler/diagnostics/diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/lexer/lexer.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/tests/unittests/compiler/lexer/utils.h"

namespace zomlang {
namespace compiler {
namespace lexer {

ZC_TEST("LexerLiteralTest.StringLiterals") {
  const auto& sm = getSourceManager();

  // Case 1: Empty double-quoted string
  {
    auto tokens = tokenize("\"\""_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::StringLiteral));
    ZC_EXPECT(tokens[0].getText(sm) == ""_zc);
  }

  // Case 2: Empty single-quoted string
  {
    auto tokens = tokenize("''"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::StringLiteral));
    ZC_EXPECT(tokens[0].getText(sm) == ""_zc);
  }

  // Case 3: Simple double-quoted string
  {
    auto tokens = tokenize("\"hello\""_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::StringLiteral));
    ZC_EXPECT(tokens[0].getText(sm) == "hello"_zc);
  }

  // Case 4: Simple single-quoted string
  {
    auto tokens = tokenize("'world'"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::StringLiteral));
    ZC_EXPECT(tokens[0].getText(sm) == "world"_zc);
  }

  // Case 5: Escaped quotes
  {
    auto tokens = tokenize("\"\\\"\""_zc);  // "\""
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::StringLiteral));
    ZC_EXPECT(tokens[0].getText(sm) == "\""_zc);

    auto tokens2 = tokenize("'\\''"_zc);  // '\''
    ZC_EXPECT(tokens2.size() == 2);
    ZC_EXPECT(tokens2[0].is(ast::SyntaxKind::StringLiteral));
    ZC_EXPECT(tokens2[0].getText(sm) == "'"_zc);
  }

  // Case 6: Mixed quotes
  {
    auto tokens = tokenize("\"'\""_zc);  // "'"
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::StringLiteral));
    ZC_EXPECT(tokens[0].getText(sm) == "'"_zc);

    auto tokens2 = tokenize("'\"'"_zc);  // '"'
    ZC_EXPECT(tokens2.size() == 2);
    ZC_EXPECT(tokens2[0].is(ast::SyntaxKind::StringLiteral));
    ZC_EXPECT(tokens2[0].getText(sm) == "\""_zc);
  }

  // Case 7: Common escapes
  {
    auto tokens = tokenize("\"\\n\\t\\r\\\\\""_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::StringLiteral));
    ZC_EXPECT(tokens[0].getText(sm) == "\n\t\r\\"_zc);
  }

  // Case 8: Unicode escape
  {
    // \u0041 is 'A'
    auto tokens = tokenize("\"\\u0041\""_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::StringLiteral));
    ZC_EXPECT(tokens[0].getText(sm) == "A"_zc);
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
    ZC_EXPECT(tokens[0].getText(getSourceManager()) == "hello world"_zc);
  }

  // TemplateHead
  {
    auto tokens = tokenize("`hello ${"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::TemplateHead));
    ZC_EXPECT(tokens[0].getText(getSourceManager()) == "hello "_zc);
  }

  // EmptyTemplateLiteral
  {
    auto tokens = tokenize("``"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::NoSubstitutionTemplateLiteral));
    ZC_EXPECT(tokens[0].getText(getSourceManager()) == ""_zc);
  }

  // MultilineTemplateLiteral
  {
    auto tokens = tokenize("`line1\nline2`"_zc);
    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::NoSubstitutionTemplateLiteral));
    ZC_EXPECT(tokens[0].getText(getSourceManager()) == "line1\nline2"_zc);
  }

  // UnterminatedTemplateLiteral
  {
    auto& sm = getSourceManager();
    auto diags = zc::heap<diagnostics::DiagnosticEngine>(sm);
    auto tokens = tokenize("`hello"_zc, diags.get());

    ZC_EXPECT(tokens.size() == 2);
    ZC_EXPECT(tokens[0].is(ast::SyntaxKind::NoSubstitutionTemplateLiteral));
    ZC_EXPECT(tokens[0].hasFlag(TokenFlags::Unterminated));
    ZC_EXPECT(tokens[0].getText(sm) == "hello"_zc);
    ZC_EXPECT(diags->hasErrors());
  }
}

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
