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

/// \brief The closed category of one top-level declaration in a document outline.
///
/// RFC 0023 "IDE Semantic Snapshots" Authority Rails: the facade exposes symbols
/// as closed categories and ranges, never a compiler-internal `ast::SyntaxKind`
/// and -- in this first, deliberately narrow slice -- never the declaration's
/// name text. A consumer draws an outline entry per category over its byte range
/// and reads any name from its own copy of the source. The category set is
/// closed: a top-level declaration whose kind is not one of the named symbol
/// kinds is not emitted at all (it is not mapped to a catch-all), so every entry
/// is a recognized symbol-bearing declaration.
enum class SnapshotOutlineCategory : uint8_t {
  Function = 0x01,
  Class = 0x02,
  Struct = 0x03,
  Interface = 0x04,
  Enum = 0x05,
  Module = 0x06,
  Import = 0x07,
  Export = 0x08,
  TypeAlias = 0x09,
  Implementation = 0x0a,
  Error = 0x0b,
  Variable = 0x0c,
};

/// \brief One IDE-safe document-outline entry for a top-level declaration.
///
/// RFC 0023 "IDE Semantic Snapshots": the entry carries the declaration's closed
/// category and its half-open source byte range, and nothing else. It carries no
/// name, no `ast::SyntaxKind`, no node id, and no nested children -- this slice
/// models only the flat sequence of top-level declarations. The range is
/// half-open (`[byteStart, byteEnd)`) and, on the published arm, always within
/// the parsed source length.
struct SnapshotOutlineEntry final {
  SnapshotOutlineCategory category = SnapshotOutlineCategory::Function;
  uint64_t byteStart = 0;
  uint64_t byteEnd = 0;

  bool operator==(const SnapshotOutlineEntry& other) const noexcept = default;
};

/// \brief Maps a top-level declaration `SyntaxKind` to its outline category, or
/// none when the kind is not a recognized top-level symbol declaration.
///
/// The mapping is the single closed authority for what enters an outline: only a
/// kind that maps to a category is projected. Non-symbol declaration-range kinds
/// (generic parameter lists, where clauses, member lists, and the like) and every
/// non-declaration kind return none.
ZC_NODISCARD zc::Maybe<SnapshotOutlineCategory> projectOutlineCategory(
    ast::SyntaxKind kind) noexcept;

}  // namespace zomlang::compiler::ide
