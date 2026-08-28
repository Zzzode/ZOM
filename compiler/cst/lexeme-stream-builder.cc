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
// See the License for the specific language governing permissions and limitations under
// the License.

#include "compiler/cst/lexeme-stream-builder.h"

#include "compiler/ast/kinds.h"
#include "compiler/identity/crypto/sha256.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::cst {
namespace {

// The absolute byte offset of `loc` within a buffer whose first byte is `base`,
// or none when the location is invalid or lies outside `[base, base + size]`.
zc::Maybe<uint64_t> offsetOf(const zc::byte* base, uint64_t size, source::SourceLoc loc) {
  if (loc.isInvalid()) { return zc::none; }
  const auto* pointer = loc.getOpaqueValue();
  if (pointer < base || pointer > base + size) { return zc::none; }
  return static_cast<uint64_t>(pointer - base);
}

// True for the bytes the lexer treats as whitespace (lexer.cc: space, tab,
// vertical tab, form feed, carriage return, line feed).
bool isWhitespaceByte(uint8_t byte) {
  return byte == ' ' || byte == '\t' || byte == '\v' || byte == '\f' || byte == '\r' ||
         byte == '\n';
}

// Emits one trivia lexeme of `kind` covering `[start, end)` of `bytes`. Returns
// false when the span is malformed (the caller then fails closed).
bool addTrivia(zc::Vector<CstLexeme>& lexemes, zc::ArrayPtr<const uint8_t> bytes, TriviaKind kind,
               uint64_t start, uint64_t end) {
  if (end <= start || end > bytes.size()) { return false; }
  auto lexeme = CstLexeme::trivia(kind, ByteRange{start, end}, bytes.slice(start, end));
  if (lexeme == zc::none) { return false; }
  ZC_IF_SOME(value, lexeme) { lexemes.add(zc::mv(value)); }
  return true;
}

// Splits the inter-token byte gap `[start, end)` into precise trivia lexemes:
// maximal whitespace runs (Whitespace), `//` line comments to just before the
// line break (LineComment), and `/* ... */` block comments (BlockComment). This
// mirrors the lexer's own trivia scanning (lexer.cc: whitespace skip, and the
// `/` comment branches). Returns false on a malformed span.
bool addTriviaGap(zc::Vector<CstLexeme>& lexemes, zc::ArrayPtr<const uint8_t> bytes, uint64_t start,
                  uint64_t end) {
  if (end <= start || end > bytes.size()) { return false; }
  uint64_t cursor = start;
  while (cursor < end) {
    const uint8_t byte = bytes[cursor];
    if (isWhitespaceByte(byte)) {
      uint64_t run = cursor + 1;
      while (run < end && isWhitespaceByte(bytes[run])) { ++run; }
      if (!addTrivia(lexemes, bytes, TriviaKind::Whitespace, cursor, run)) { return false; }
      cursor = run;
      continue;
    }
    if (byte == '/' && cursor + 1 < end && bytes[cursor + 1] == '/') {
      // A line comment runs to just before the next line break (or gap end).
      uint64_t run = cursor + 2;
      while (run < end && bytes[run] != '\r' && bytes[run] != '\n') { ++run; }
      if (!addTrivia(lexemes, bytes, TriviaKind::LineComment, cursor, run)) { return false; }
      cursor = run;
      continue;
    }
    if (byte == '/' && cursor + 1 < end && bytes[cursor + 1] == '*') {
      // A block comment runs through the closing `*/` (inclusive).
      uint64_t run = cursor + 2;
      bool closed = false;
      while (run < end) {
        if (bytes[run] == '*' && run + 1 < end && bytes[run + 1] == '/') {
          run += 2;
          closed = true;
          break;
        }
        ++run;
      }
      // An unterminated block comment cannot appear in an accepted token gap.
      if (!closed) { return false; }
      if (!addTrivia(lexemes, bytes, TriviaKind::BlockComment, cursor, run)) { return false; }
      cursor = run;
      continue;
    }
    // A gap byte that is neither whitespace nor a comment opener is not trivia.
    return false;
  }
  return true;
}

}  // namespace

LexemeStreamResult buildLexemeStreamFromTokens(zc::ArrayPtr<const zc::byte> bufferBytes,
                                               zc::ArrayPtr<const lexer::Token> tokens) {
  const auto* base = bufferBytes.begin();
  const uint64_t size = bufferBytes.size();
  // Reinterpret the buffer as unsigned bytes for spelling slices and hashing.
  const zc::ArrayPtr<const uint8_t> bytes(reinterpret_cast<const uint8_t*>(base), size);

  auto contentDigest = identity::sha256(bytes);
  if (contentDigest == zc::none) {
    return LexemeStreamResult(LexemePartitionFailure::EmptyPartition);
  }

  zc::Vector<CstLexeme> lexemes;
  uint64_t cursor = 0;
  for (const auto& token : tokens) {
    // The zero-width end-of-file token is a parser event, not a lexeme.
    if (token.is(ast::SyntaxKind::EndOfFile)) { continue; }
    const auto range = token.getRange();
    auto start = offsetOf(base, size, range.getStart());
    auto end = offsetOf(base, size, range.getEnd());
    if (start == zc::none || end == zc::none) {
      return LexemeStreamResult(LexemePartitionFailure::NonAdjacentPartition);
    }
    uint64_t tokenStart = 0;
    uint64_t tokenEnd = 0;
    ZC_IF_SOME(value, start) { tokenStart = value; }
    ZC_IF_SOME(value, end) { tokenEnd = value; }
    if (tokenEnd < tokenStart || tokenStart < cursor) {
      return LexemeStreamResult(LexemePartitionFailure::NonAdjacentPartition);
    }
    // Any bytes between the previous lexeme and this token are trivia.
    if (tokenStart > cursor && !addTriviaGap(lexemes, bytes, cursor, tokenStart)) {
      return LexemeStreamResult(LexemePartitionFailure::NonAdjacentPartition);
    }
    // A zero-width significant token cannot contribute a lexeme spelling.
    if (tokenEnd == tokenStart) {
      return LexemeStreamResult(LexemePartitionFailure::SpellingWidthMismatch);
    }
    auto lexeme =
        CstLexeme::token(static_cast<uint32_t>(token.getKind()), ByteRange{tokenStart, tokenEnd},
                         bytes.slice(tokenStart, tokenEnd));
    if (lexeme == zc::none) {
      return LexemeStreamResult(LexemePartitionFailure::SpellingWidthMismatch);
    }
    ZC_IF_SOME(value, lexeme) { lexemes.add(zc::mv(value)); }
    cursor = tokenEnd;
  }
  // Any bytes after the last significant token are trailing trivia.
  if (cursor < size && !addTriviaGap(lexemes, bytes, cursor, size)) {
    return LexemeStreamResult(LexemePartitionFailure::NonAdjacentPartition);
  }

  identity::Sha256Digest digest;
  ZC_IF_SOME(value, contentDigest) { digest = value; }
  return LexemePartitionVerifier::verify(
      LexemeStreamRequest{digest, size, lexemes.releaseAsArray()});
}

}  // namespace zomlang::compiler::cst
