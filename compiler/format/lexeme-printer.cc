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

}  // namespace zomlang::compiler::format
