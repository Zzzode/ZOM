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
  const auto left = original.lexemes();
  const auto right = reformatted.lexemes();
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].tag() != right[index].tag()) { return false; }
    // A Token compares by kind, a Trivia by trivia kind; both compare spelling.
    if (left[index].tag() == cst::CstLexemeTag::Token &&
        left[index].tokenKind() != right[index].tokenKind()) {
      return false;
    }
    if (left[index].tag() == cst::CstLexemeTag::Trivia &&
        left[index].triviaKind() != right[index].triviaKind()) {
      return false;
    }
    const auto leftBytes = left[index].spelling();
    const auto rightBytes = right[index].spelling();
    if (leftBytes.size() != rightBytes.size()) { return false; }
    for (size_t byte = 0; byte < leftBytes.size(); ++byte) {
      if (leftBytes[byte] != rightBytes[byte]) { return false; }
    }
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

  return FormatResult::normalize(replacements.releaseAsArray());
}

}  // namespace zomlang::compiler::format
