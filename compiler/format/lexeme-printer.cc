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

#include "compiler/format/lexeme-printer.h"

#include "compiler/ast/kinds.h"
#include "compiler/format/doc-renderer.h"
#include "compiler/format/doc.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::format {
namespace {

// The verbatim bytes of one lexeme spelling as a NUL-terminated string, so it can
// enter a Doc::text node without re-lexing.
zc::String spellingString(const cst::CstLexeme& lexeme) {
  const auto bytes = lexeme.spelling();
  // Allocate one extra byte for the NUL terminator zc::String / zc::StringPtr
  // require; the spelling itself never contains an interior NUL.
  auto chars = zc::heapArray<char>(bytes.size() + 1);
  for (size_t index = 0; index < bytes.size(); ++index) {
    chars[index] = static_cast<char>(bytes[index]);
  }
  chars[bytes.size()] = '\0';
  return zc::str(zc::StringPtr(chars.begin(), bytes.size()));
}

}  // namespace

zc::String formatLexemeStream(const cst::VerifiedLexemeStream& stream) {
  const auto lexemes = stream.lexemes();
  zc::Vector<Doc> pieces(lexemes.size());
  for (const auto& lexeme : lexemes) { pieces.add(Doc::text(spellingString(lexeme))); }
  return DocRenderer::render(Doc::concat(pieces.releaseAsArray()));
}

bool tokenSequencePreserved(const cst::VerifiedLexemeStream& original,
                            const cst::VerifiedLexemeStream& reformatted) {
  // RFC 0044 preserves the significant token sequence -- token kinds, order, and
  // spellings -- while permitting whitespace/trivia normalization. So the
  // comparison walks only the Token lexemes of each stream and ignores trivia
  // (whitespace and comment lexemes may legitimately differ after normalization).
  const auto left = original.lexemes();
  const auto right = reformatted.lexemes();
  size_t leftIndex = 0;
  size_t rightIndex = 0;
  while (leftIndex < left.size() && rightIndex < right.size()) {
    if (left[leftIndex].tag() != cst::CstLexemeTag::Token) {
      ++leftIndex;
      continue;
    }
    if (right[rightIndex].tag() != cst::CstLexemeTag::Token) {
      ++rightIndex;
      continue;
    }
    if (left[leftIndex].tokenKind() != right[rightIndex].tokenKind()) { return false; }
    const auto leftBytes = left[leftIndex].spelling();
    const auto rightBytes = right[rightIndex].spelling();
    if (leftBytes.size() != rightBytes.size()) { return false; }
    for (size_t byte = 0; byte < leftBytes.size(); ++byte) {
      if (leftBytes[byte] != rightBytes[byte]) { return false; }
    }
    ++leftIndex;
    ++rightIndex;
  }
  // Any remaining lexemes on either side must all be trivia (no extra tokens).
  for (; leftIndex < left.size(); ++leftIndex) {
    if (left[leftIndex].tag() == cst::CstLexemeTag::Token) { return false; }
  }
  for (; rightIndex < right.size(); ++rightIndex) {
    if (right[rightIndex].tag() == cst::CstLexemeTag::Token) { return false; }
  }
  return true;
}

namespace {

// True for a horizontal whitespace byte (not a line break): space, tab, vertical
// tab, or form feed. Trailing-whitespace stripping removes runs of these that sit
// immediately before a line break.
bool isHorizontalSpace(uint8_t byte) {
  return byte == ' ' || byte == '\t' || byte == '\v' || byte == '\f';
}

// True when a whitespace lexeme spelling contains a line break; a comma followed
// by such whitespace is a multiline-list case governed by structural reflow, not
// the same-line "one space after comma" rule.
bool containsLineBreak(zc::ArrayPtr<const uint8_t> spelling) {
  for (const auto byte : spelling) {
    if (byte == '\r' || byte == '\n') { return true; }
  }
  return false;
}

}  // namespace

FormatResult normalizeTriviaWhitespace(const cst::VerifiedLexemeStream& stream) {
  const auto lexemes = stream.lexemes();
  const uint64_t sourceByteCount = stream.sourceByteCount();
  zc::Vector<SourceReplacement> replacements;

  // (1) Strip horizontal whitespace that immediately precedes a line break,
  // editing only Whitespace trivia lexeme bytes. A run of spaces/tabs ending at a
  // '\r' or '\n' (within the same Whitespace lexeme) is deleted.
  //
  // (2) Canonicalize the file-final whitespace to exactly one '\n': if the last
  // lexeme is Whitespace, its trailing run of blank characters after the final
  // meaningful newline collapses to a single '\n' (or one is added when missing).
  for (size_t index = 0; index < lexemes.size(); ++index) {
    const auto& lexeme = lexemes[index];
    if (lexeme.tag() != cst::CstLexemeTag::Trivia ||
        lexeme.triviaKind() != cst::TriviaKind::Whitespace) {
      continue;
    }
    const auto spelling = lexeme.spelling();
    const uint64_t base = lexeme.range().start;
    const bool isFinalLexeme = index + 1 == lexemes.size();

    // (1) Trailing-space-before-newline runs inside this whitespace lexeme. The
    // final whitespace lexeme is handled wholesale by the final-newline rule
    // below, so skip it here to avoid overlapping edits.
    size_t cursor = 0;
    while (!isFinalLexeme && cursor < spelling.size()) {
      if (isHorizontalSpace(spelling[cursor])) {
        size_t runEnd = cursor + 1;
        while (runEnd < spelling.size() && isHorizontalSpace(spelling[runEnd])) { ++runEnd; }
        const bool beforeBreak =
            runEnd < spelling.size() && (spelling[runEnd] == '\r' || spelling[runEnd] == '\n');
        // A horizontal run that ends the final whitespace lexeme (end of file with
        // no trailing newline) is handled by the final-newline rule below.
        if (beforeBreak) {
          auto edit = SourceReplacement::make(base + cursor, base + runEnd, ""_zc);
          ZC_IF_SOME(value, edit) { replacements.add(zc::mv(value)); }
        }
        cursor = runEnd;
        continue;
      }
      ++cursor;
    }

    // (2) File-final newline: the final whitespace lexeme must reduce to exactly
    // one '\n'. Replace the whole final whitespace run with a single newline.
    if (isFinalLexeme) {
      bool alreadyCanonical = spelling.size() == 1 && spelling[0] == '\n';
      if (!alreadyCanonical) {
        auto edit = SourceReplacement::make(base, base + spelling.size(), "\n"_zc);
        ZC_IF_SOME(value, edit) { replacements.add(zc::mv(value)); }
      }
    }
  }

  // The source must end with exactly one newline; when the last lexeme is not
  // whitespace (no trailing newline at all), insert one at end of file.
  if (lexemes.size() != 0 &&
      !(lexemes[lexemes.size() - 1].tag() == cst::CstLexemeTag::Trivia &&
        lexemes[lexemes.size() - 1].triviaKind() == cst::TriviaKind::Whitespace)) {
    auto edit = SourceReplacement::make(sourceByteCount, sourceByteCount, "\n"_zc);
    ZC_IF_SOME(value, edit) { replacements.add(zc::mv(value)); }
  }

  // RFC 0044 fixed style: exactly one space after a comma on the same line. When
  // a Comma token is followed by a same-line separator, normalize the spacing to
  // one space: a following Whitespace lexeme with no line break collapses to a
  // single space, and a comma directly followed by another token (no gap) gets a
  // space inserted at the boundary. A comma followed by a newline is a
  // multiline-list case governed by structural reflow, and the final whitespace
  // lexeme is owned by the final-newline rule above, so both are skipped here to
  // keep edits disjoint.
  for (size_t index = 0; index + 1 < lexemes.size(); ++index) {
    const auto& token = lexemes[index];
    if (token.tag() != cst::CstLexemeTag::Token ||
        token.tokenKind() != static_cast<uint32_t>(ast::SyntaxKind::Comma)) {
      continue;
    }
    const auto& next = lexemes[index + 1];
    if (next.tag() == cst::CstLexemeTag::Token) {
      // No gap after the comma: insert exactly one space at the boundary.
      auto edit = SourceReplacement::make(token.range().end, token.range().end, " "_zc);
      ZC_IF_SOME(value, edit) { replacements.add(zc::mv(value)); }
      continue;
    }
    if (next.tag() != cst::CstLexemeTag::Trivia ||
        next.triviaKind() != cst::TriviaKind::Whitespace) {
      continue;  // a comment directly follows; comment spacing is out of scope
    }
    if (index + 1 == lexemes.size() - 1) { continue; }  // final lexeme: rule 2 owns it
    const auto spelling = next.spelling();
    if (containsLineBreak(spelling)) { continue; }
    // Already exactly one space: nothing to do.
    if (spelling.size() == 1 && spelling[0] == ' ') { continue; }
    auto edit = SourceReplacement::make(next.range().start, next.range().end, " "_zc);
    ZC_IF_SOME(value, edit) { replacements.add(zc::mv(value)); }
  }

  return FormatResult::normalize(replacements.releaseAsArray());
}

}  // namespace zomlang::compiler::format
