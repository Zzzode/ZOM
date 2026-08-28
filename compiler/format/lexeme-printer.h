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
#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/string.h"

namespace zomlang::compiler::format {

/// \brief Formats a verified lexeme stream by emitting each lexeme's bytes
/// verbatim through the Doc-IR layout engine.
///
/// RFC 0044 "Reference-Level Design": the formatter receives the recoverable
/// lossless token and trivia stream, must not insert, delete, or reorder syntax
/// tokens, and emits `Doc::text` bytes that are never re-lexed. This is the
/// token-preserving identity printer: it walks the RFC 0023 verified lexeme
/// stream, emits one `text` node per lexeme spelling, concatenates them, and
/// renders the document. Because every lexeme spelling is emitted verbatim and
/// no reflow decision is introduced, the output is byte-identical to the source
/// the stream was built from.
///
/// This is the printer's safe baseline (RFC 0044 Implementation Plan, the
/// syntax-directed printer over the lossless stream). Group/line/indent reflow
/// that normalizes whitespace at trivia boundaries -- the only normalization RFC
/// 0044 permits, and only where the result re-lexes identically -- is a later
/// refinement layered on this token-preserving base.
ZC_NODISCARD zc::String formatLexemeStream(const cst::VerifiedLexemeStream& stream);

/// \brief Proves a formatting result preserves the complete lossless token
/// sequence.
///
/// RFC 0044 "Verification": independent token/trivia verification proves
/// formatting preserves the complete lossless token sequence. This compares the
/// original stream's lexemes with the lexemes re-derived from the formatted
/// output, requiring identical tags, kinds, and spellings in order. A formatter
/// that inserted, deleted, reordered, or re-lexed a token fails this check.
///
/// \param original The lexeme stream the formatter consumed.
/// \param reformatted The lexeme stream re-derived from the formatted output.
/// \return true when the two streams carry the identical lexeme sequence.
ZC_NODISCARD bool tokenSequencePreserved(const cst::VerifiedLexemeStream& original,
                                         const cst::VerifiedLexemeStream& reformatted);

}  // namespace zomlang::compiler::format
