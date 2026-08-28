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

#include "compiler/format/doc.h"
#include "zc/core/string.h"

namespace zomlang::compiler::format {

/// \brief The pinned target line width in Unicode scalar values (RFC 0044).
///
/// The width is a fixed constant of the one ZOM style, not a configurable
/// option; changing it is a future accepted-RFC decision, never a user setting.
inline constexpr uint32_t kTargetWidth = 100;

/// \brief The fixed block-indentation step in columns (RFC 0044 "Fixed Style").
inline constexpr uint32_t kIndentStep = 4;

/// \brief The generic width-driven layout pass for the Wadler/Lindig `Doc`.
///
/// See RFC 0044 "Layout Engine". `render` walks a finite `Doc` once and produces
/// canonical layout bytes: a `group` renders flat when its flat width plus the
/// current column does not exceed `kTargetWidth` and it contains no `hardline`,
/// otherwise every direct `line`/`softline` in that group breaks and its
/// `indent` applies. It is a total function with no search or backtracking, so
/// cost is linear in document size. The renderer consumes only the `Doc`; it has
/// no knowledge of tokens, the CST, or the filesystem.
class DocRenderer final {
public:
  /// \brief Renders `doc` to canonical layout bytes at the pinned width.
  ZC_NODISCARD static zc::String render(const Doc& doc);
};

}  // namespace zomlang::compiler::format
