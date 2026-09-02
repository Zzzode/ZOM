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

#include <cstdint>

#include "compiler/ast/kinds.h"
#include "zc/core/common.h"

namespace zomlang::compiler::ide {

/// \brief The closed IDE token category a projected token exposes.
///
/// RFC 0023 "IDE Semantic Snapshots" Authority Rails: the facade exposes only
/// files, ranges, symbols, display types, edits, and closed states -- never a
/// compiler-internal identity. A token's `ast::SyntaxKind` is such an internal
/// identity (it enumerates every keyword, operator, and AST node kind), so the
/// projection collapses it into this small closed category set an editor's
/// semantic-token or outline feature can consume directly. The mapping is
/// derived from the `SyntaxKind` token ranges (`FirstKeyword..LastKeyword`,
/// `FirstBinaryOperator..LastBinaryOperator`, `FirstPunctuation..LastPunctuation`)
/// plus the explicit literal and comment kinds; anything outside those ranges is
/// `Other`, so the category is total and never leaks a raw kind.
enum class SnapshotTokenCategory : uint8_t {
  Identifier = 0x01,
  Keyword = 0x02,
  StringLiteral = 0x03,
  NumberLiteral = 0x04,
  CharacterLiteral = 0x05,
  TemplateLiteral = 0x06,
  Operator = 0x07,
  Punctuation = 0x08,
  Comment = 0x09,
  Other = 0x0a,
};

/// \brief One IDE-safe lexical token projected out of a verified parse.
///
/// RFC 0023 "IDE Semantic Snapshots": the editor semantic facade exposes a
/// token's closed category and its half-open source byte range, and nothing
/// else. It deliberately carries neither the compiler-internal `ast::SyntaxKind`
/// nor the token spelling: a semantic-token or outline consumer colors ranges by
/// category and reads spellings from its own copy of the source, so re-exposing
/// the text would only duplicate source bytes across the boundary. The byte
/// range is half-open (`[byteStart, byteEnd)`) and, on the published arm, always
/// within the parsed source length; a zero-width range is never produced because
/// the end-of-file sentinel token is dropped during projection.
struct SnapshotToken final {
  SnapshotTokenCategory category = SnapshotTokenCategory::Other;
  uint64_t byteStart = 0;
  uint64_t byteEnd = 0;

  bool operator==(const SnapshotToken& other) const noexcept = default;
};

/// \brief Maps one parser `SyntaxKind` to its closed IDE token category.
///
/// Total over every `SyntaxKind`: any kind outside the recognized literal,
/// keyword, operator, punctuation, and comment ranges maps to `Other`.
ZC_NODISCARD SnapshotTokenCategory projectTokenCategory(ast::SyntaxKind kind) noexcept;

}  // namespace zomlang::compiler::ide
