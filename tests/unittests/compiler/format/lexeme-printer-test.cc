// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// RFC 0044 formatter printer slice: prove the token-preserving identity printer
// over the RFC 0023 lexeme stream is byte-exact, idempotent, and token
// preserving. The printer emits each lexeme spelling verbatim through the Doc-IR
// engine, so a formatted buffer reproduces the source and re-derives the same
// lexeme sequence. This composes the live lexer, the lexeme-stream bridge, and
// the Doc-IR core.

#include "compiler/format/lexeme-printer.h"

#include "compiler/basic/string-pool.h"
#include "compiler/basic/zomlang-opts.h"
#include "compiler/cst/lexeme-stream-builder.h"
#include "compiler/diagnostics/core/diagnostic-engine.h"
#include "compiler/lexer/lexer.h"
#include "compiler/source/manager.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::format {
namespace {

// Lexes `source` and builds the verified lexeme stream for it, holding the owned
// lexer state alive for the duration of the returned stream's buffer references.
struct LexedStream final {
  zc::Own<basic::StringPool> stringPool;
  zc::Own<diagnostics::DiagnosticEngine> diagnostics;
  zc::Vector<lexer::Token> tokens;
  zc::Own<cst::VerifiedLexemeStream> stream;
};

LexedStream lexedStream(source::SourceManager& sourceManager, zc::StringPtr source) {
  LexedStream result;
  result.stringPool = zc::heap<basic::StringPool>();
  result.diagnostics = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();
  auto bufferId = sourceManager.addMemBufferCopy(source.asBytes(), "printer-test.zom");
  lexer::Lexer lexer(sourceManager, *result.diagnostics, langOpts, *result.stringPool, bufferId);
  lexer::Token token;
  do {
    lexer.lex(token);
    result.tokens.add(token);
  } while (token.getKind() != ast::SyntaxKind::EndOfFile);
  auto bufferBytes = sourceManager.getEntireTextForBuffer(bufferId);
  auto built = cst::buildLexemeStreamFromTokens(bufferBytes, result.tokens.asPtr());
  ZC_REQUIRE(built.is<cst::VerifiedLexemeStream>());
  result.stream =
      zc::heap<cst::VerifiedLexemeStream>(zc::mv(built.get<cst::VerifiedLexemeStream>()));
  return result;
}

// The identity printer reproduces the source byte-for-byte.
void expectIdentity(zc::StringPtr source) {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto lexed = lexedStream(*sourceManager, source);
  auto formatted = formatLexemeStream(*lexed.stream);
  ZC_EXPECT(formatted == source);
}

ZC_TEST("Lexeme printer reproduces a whitespace source") { expectIdentity("let x = 42"_zc); }

ZC_TEST("Lexeme printer reproduces a source with a comment") {
  expectIdentity("let /* c */ y = 1"_zc);
}

ZC_TEST("Lexeme printer reproduces leading and trailing whitespace") {
  expectIdentity("  let z = 0  "_zc);
}

ZC_TEST("Lexeme printer reproduces a no-trivia source") { expectIdentity("identifier"_zc); }

// Formatting is idempotent: formatting the formatted bytes changes nothing, and
// the re-derived lexeme sequence is preserved.
ZC_TEST("Lexeme printer is idempotent and token preserving") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto lexed = lexedStream(*sourceManager, "let x = 42 // tail"_zc);
  auto formatted = formatLexemeStream(*lexed.stream);

  // Re-lex the formatted output and prove the lexeme sequence is preserved.
  auto reformatted = lexedStream(*sourceManager, formatted);
  ZC_EXPECT(tokenSequencePreserved(*lexed.stream, *reformatted.stream));

  // Formatting again reproduces the same bytes.
  auto second = formatLexemeStream(*reformatted.stream);
  ZC_EXPECT(second == formatted);
}

// Trailing whitespace before a line break is stripped, editing only whitespace.
ZC_TEST("Whitespace normalizer strips trailing whitespace") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto lexed = lexedStream(*sourceManager, "let x = 1   \nlet y = 2\n"_zc);
  auto result = normalizeTriviaWhitespace(*lexed.stream);
  ZC_REQUIRE(result.outcome() == FormatOutcome::Edits);
  ZC_EXPECT(result.apply("let x = 1   \nlet y = 2\n"_zc) == "let x = 1\nlet y = 2\n"_zc);
}

// A source with no final newline gains exactly one.
ZC_TEST("Whitespace normalizer adds a missing final newline") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto lexed = lexedStream(*sourceManager, "let x = 1"_zc);
  auto result = normalizeTriviaWhitespace(*lexed.stream);
  ZC_REQUIRE(result.outcome() == FormatOutcome::Edits);
  ZC_EXPECT(result.apply("let x = 1"_zc) == "let x = 1\n"_zc);
}

// Multiple trailing blank lines collapse to a single final newline.
ZC_TEST("Whitespace normalizer collapses trailing blank lines") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto lexed = lexedStream(*sourceManager, "let x = 1\n\n\n"_zc);
  auto result = normalizeTriviaWhitespace(*lexed.stream);
  ZC_REQUIRE(result.outcome() == FormatOutcome::Edits);
  ZC_EXPECT(result.apply("let x = 1\n\n\n"_zc) == "let x = 1\n"_zc);
}

// A source already in canonical whitespace form is Unchanged.
ZC_TEST("Whitespace normalizer leaves canonical source unchanged") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto lexed = lexedStream(*sourceManager, "let x = 1\n"_zc);
  auto result = normalizeTriviaWhitespace(*lexed.stream);
  ZC_EXPECT(result.outcome() == FormatOutcome::Unchanged);
}

// Normalization never touches token or comment bytes: the normalized output
// re-lexes to the identical token sequence.
ZC_TEST("Whitespace normalizer preserves the token sequence") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto lexed = lexedStream(*sourceManager, "let x = 1  // c  \n"_zc);
  auto result = normalizeTriviaWhitespace(*lexed.stream);
  auto normalized = result.apply("let x = 1  // c  \n"_zc);
  auto relexed = lexedStream(*sourceManager, normalized);
  ZC_EXPECT(tokenSequencePreserved(*lexed.stream, *relexed.stream));
}

// A comma with no following space gains exactly one space (same line).
ZC_TEST("Whitespace normalizer inserts one space after a comma") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto lexed = lexedStream(*sourceManager, "f(a,b)\n"_zc);
  auto result = normalizeTriviaWhitespace(*lexed.stream);
  ZC_REQUIRE(result.outcome() == FormatOutcome::Edits);
  ZC_EXPECT(result.apply("f(a,b)\n"_zc) == "f(a, b)\n"_zc);
}

// Multiple spaces after a comma collapse to exactly one.
ZC_TEST("Whitespace normalizer collapses spaces after a comma") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto lexed = lexedStream(*sourceManager, "f(a,   b)\n"_zc);
  auto result = normalizeTriviaWhitespace(*lexed.stream);
  ZC_REQUIRE(result.outcome() == FormatOutcome::Edits);
  ZC_EXPECT(result.apply("f(a,   b)\n"_zc) == "f(a, b)\n"_zc);
}

// A comma at end of line (followed by a newline) is a multiline-list case and is
// left to structural reflow; the comma rule does not join the lines.
ZC_TEST("Whitespace normalizer leaves a comma before a newline alone") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto lexed = lexedStream(*sourceManager, "f(a,\n  b)\n"_zc);
  auto result = normalizeTriviaWhitespace(*lexed.stream);
  // No trailing whitespace and a final newline already present, and the comma is
  // followed by a line break, so nothing applies.
  ZC_EXPECT(result.outcome() == FormatOutcome::Unchanged);
}

// The comma rule preserves the token sequence.
ZC_TEST("Comma normalization preserves the token sequence") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto lexed = lexedStream(*sourceManager, "f(a,b,c)\n"_zc);
  auto result = normalizeTriviaWhitespace(*lexed.stream);
  auto normalized = result.apply("f(a,b,c)\n"_zc);
  ZC_EXPECT(normalized == "f(a, b, c)\n"_zc);
  auto relexed = lexedStream(*sourceManager, normalized);
  ZC_EXPECT(tokenSequencePreserved(*lexed.stream, *relexed.stream));
}

}  // namespace
}  // namespace zomlang::compiler::format
