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

#pragma once

#include "compiler/cst/lexeme-codec.h"
#include "compiler/lexer/token.h"
#include "zc/core/array.h"
#include "zc/core/common.h"

namespace zomlang::compiler::source {
class BufferId;
class SourceManager;
}  // namespace zomlang::compiler::source

namespace zomlang::compiler::cst {

/// \brief Builds a verified lexeme stream from the live lexer's token output over
/// one source buffer.
///
/// RFC 0023 "Recoverable Parsing": the recoverable CST is built from one closed
/// lexeme stream that covers every source byte. The current lexer emits only
/// significant tokens (and a zero-width end-of-file token), skipping the trivia
/// bytes between them. This bridge reconstructs the exact byte partition RFC 0023
/// requires by walking the significant tokens in source order, emitting one
/// `Token` lexeme per significant token and one `Trivia` lexeme for each
/// inter-token byte gap and any trailing gap, then verifying the result through
/// `LexemePartitionVerifier`.
///
/// The production parser combines this stream with its construction-event
/// stream and verified recovery sequence into `RecoverableSyntaxTree`.
///
/// Trivia sub-kind: each inter-token gap is split into precise trivia lexemes
/// mirroring the lexer's own scanning -- maximal whitespace runs
/// (`TriviaKind::Whitespace`), `//` line comments (`TriviaKind::LineComment`),
/// and `/* ... */` block comments (`TriviaKind::BlockComment`). The partition
/// and byte-reconstruction invariants hold across the split.
///
/// \param bufferBytes The entire source buffer, as returned by
///        `SourceManager::getEntireTextForBuffer`.
/// \param tokens The lexer's buffered token output for that buffer, including the
///        trailing end-of-file token. Token byte offsets are computed against
///        `bufferBytes.begin()`.
/// \return A verified lexeme stream, or the `LexemePartitionFailure` that a
///         malformed token range produced (a token outside the buffer, an
///         out-of-order token, or a byte-count mismatch fails closed).
ZC_NODISCARD LexemeStreamResult buildLexemeStreamFromTokens(
    zc::ArrayPtr<const zc::byte> bufferBytes, zc::ArrayPtr<const lexer::Token> tokens);

/// \brief Builds the verified lexeme stream through the CST-owned source-buffer
/// access boundary.
ZC_NODISCARD LexemeStreamResult
buildLexemeStreamFromSource(const source::SourceManager& sources, const source::BufferId& buffer,
                            zc::ArrayPtr<const lexer::Token> tokens);

}  // namespace zomlang::compiler::cst
