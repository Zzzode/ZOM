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

// RFC 0023 recoverable-parsing bridge: prove the live lexer's token output over a
// real source buffer reconstructs the exact RFC 0023 lexeme partition. The bridge
// walks the significant tokens, fills every inter-token byte gap with a trivia
// lexeme, and verifies the partition covers `[0, sourceByteCount)` and
// reconstructs the source byte-for-byte. This exercises the real lexer, not a
// hand-built stream.

#include "compiler/cst/lexeme-stream-builder.h"

#include "compiler/basic/string-pool.h"
#include "compiler/basic/zomlang-opts.h"
#include "compiler/diagnostics/core/diagnostic-engine.h"
#include "compiler/lexer/lexer.h"
#include "compiler/source/manager.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::cst {
namespace {

// One lexed buffer: the tokens plus the entire buffer bytes, so a test can build
// and verify the lexeme stream against the real source.
struct LexedBuffer final {
  zc::Own<basic::StringPool> stringPool;
  zc::Own<diagnostics::DiagnosticEngine> diagnostics;
  zc::Vector<lexer::Token> tokens;
  zc::ArrayPtr<const zc::byte> bufferBytes;
};

LexedBuffer lexSource(source::SourceManager& sourceManager, zc::StringPtr source) {
  LexedBuffer result;
  result.stringPool = zc::heap<basic::StringPool>();
  result.diagnostics = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();
  auto bufferId = sourceManager.addMemBufferCopy(source.asBytes(), "bridge-test.zom");
  lexer::Lexer lexer(sourceManager, *result.diagnostics, langOpts, *result.stringPool, bufferId);
  lexer::Token token;
  do {
    lexer.lex(token);
    result.tokens.add(token);
  } while (token.getKind() != ast::SyntaxKind::EndOfFile);
  result.bufferBytes = sourceManager.getEntireTextForBuffer(bufferId);
  return result;
}

// Concatenating the verified lexeme spellings reconstructs the source bytes.
bool reconstructsSource(const VerifiedLexemeStream& stream, zc::ArrayPtr<const zc::byte> source) {
  zc::Vector<uint8_t> reconstructed(source.size());
  for (const auto& lexeme : stream.lexemes()) {
    for (const auto byte : lexeme.spelling()) { reconstructed.add(byte); }
  }
  if (reconstructed.size() != source.size()) { return false; }
  for (size_t index = 0; index < source.size(); ++index) {
    if (reconstructed[index] != static_cast<uint8_t>(source[index])) { return false; }
  }
  return true;
}

// A source with inter-token whitespace verifies and reconstructs exactly, with a
// trivia lexeme filling each gap between the significant tokens.
ZC_TEST("Lexeme bridge partitions a source with whitespace") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto lexed = lexSource(*sourceManager, "let x = 42"_zc);
  auto result = buildLexemeStreamFromTokens(lexed.bufferBytes, lexed.tokens.asPtr());
  ZC_REQUIRE(result.is<VerifiedLexemeStream>());
  const auto& stream = result.get<VerifiedLexemeStream>();
  ZC_EXPECT(reconstructsSource(stream, lexed.bufferBytes));
  // Four significant tokens (`let`, `x`, `=`, `42`) and three interleaving
  // whitespace gaps.
  size_t tokenCount = 0;
  size_t triviaCount = 0;
  for (const auto& lexeme : stream.lexemes()) {
    if (lexeme.tag() == CstLexemeTag::Token) { ++tokenCount; }
    if (lexeme.tag() == CstLexemeTag::Trivia) { ++triviaCount; }
  }
  ZC_EXPECT(tokenCount == 4);
  ZC_EXPECT(triviaCount == 3);
}

// A source with a comment between tokens reconstructs exactly; the comment bytes
// are retained in a trivia lexeme.
ZC_TEST("Lexeme bridge retains a comment as trivia") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto lexed = lexSource(*sourceManager, "let /* c */ y = 1"_zc);
  auto result = buildLexemeStreamFromTokens(lexed.bufferBytes, lexed.tokens.asPtr());
  ZC_REQUIRE(result.is<VerifiedLexemeStream>());
  ZC_EXPECT(reconstructsSource(result.get<VerifiedLexemeStream>(), lexed.bufferBytes));
}

// Leading and trailing whitespace is covered by trivia lexemes.
ZC_TEST("Lexeme bridge covers leading and trailing whitespace") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto lexed = lexSource(*sourceManager, "  let z = 0  "_zc);
  auto result = buildLexemeStreamFromTokens(lexed.bufferBytes, lexed.tokens.asPtr());
  ZC_REQUIRE(result.is<VerifiedLexemeStream>());
  const auto& stream = result.get<VerifiedLexemeStream>();
  ZC_EXPECT(reconstructsSource(stream, lexed.bufferBytes));
  // The first and last lexemes are the leading and trailing whitespace trivia.
  const auto lexemes = stream.lexemes();
  ZC_REQUIRE(lexemes.size() >= 2);
  ZC_EXPECT(lexemes[0].tag() == CstLexemeTag::Trivia);
  ZC_EXPECT(lexemes[lexemes.size() - 1].tag() == CstLexemeTag::Trivia);
}

// A single-token source with no trivia is one lexeme covering the whole buffer.
ZC_TEST("Lexeme bridge handles a no-trivia source") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto lexed = lexSource(*sourceManager, "identifier"_zc);
  auto result = buildLexemeStreamFromTokens(lexed.bufferBytes, lexed.tokens.asPtr());
  ZC_REQUIRE(result.is<VerifiedLexemeStream>());
  const auto& stream = result.get<VerifiedLexemeStream>();
  ZC_EXPECT(reconstructsSource(stream, lexed.bufferBytes));
  ZC_EXPECT(stream.lexemes().size() == 1);
  ZC_EXPECT(stream.lexemes()[0].tag() == CstLexemeTag::Token);
}

}  // namespace
}  // namespace zomlang::compiler::cst
