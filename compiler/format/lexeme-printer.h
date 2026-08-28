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
#include "compiler/format/source-edits.h"
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
/// significant `Token` lexemes of the two streams -- kinds, order, and spellings
/// -- and ignores trivia, since whitespace normalization legitimately changes
/// trivia while every token must be preserved. A formatter that inserted,
/// deleted, reordered, or re-spelled a token, or introduced or dropped a token,
/// fails this check.
///
/// \param original The lexeme stream the formatter consumed.
/// \param reformatted The lexeme stream re-derived from the formatted output.
/// \return true when the two streams carry the identical lexeme sequence.
ZC_NODISCARD bool tokenSequencePreserved(const cst::VerifiedLexemeStream& original,
                                         const cst::VerifiedLexemeStream& reformatted);

/// \brief Computes the canonical whitespace normalization of a lexeme stream as a
/// source-edit set.
///
/// RFC 0044 "Fixed Style" / L127-128: the formatter may normalize whitespace only
/// at token and trivia boundaries where the result parses to the same lossless
/// token sequence. This performs the two normalizations that follow purely from
/// the trivia stream, with no parser structure and no risk of re-lexing
/// differently:
///
///   - strip trailing whitespace before each line break, and
///   - canonicalize the file-final whitespace to exactly one trailing newline.
///
/// It edits only `Whitespace` trivia lexeme bytes; token, line-comment, and
/// block-comment bytes are never touched, and no token is inserted, deleted, or
/// reordered. The result is a `FormatResult`: `Edits` (sorted, disjoint,
/// adjacent-merged `SourceReplacement`s) when a normalization applies, or
/// `Unchanged` when the source is already canonical. This is the first real,
/// non-identity reformat; structural indentation and line reflow are driven by
/// the parser and are later slices.
ZC_NODISCARD FormatResult normalizeTriviaWhitespace(const cst::VerifiedLexemeStream& stream);

}  // namespace zomlang::compiler::format
