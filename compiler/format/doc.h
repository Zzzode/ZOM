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

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"

namespace zomlang::compiler::format {

/// \brief The closed Wadler/Lindig document-algebra constructor set.
///
/// See RFC 0044 "Layout Engine". A syntax-directed printer emits a `Doc`; one
/// generic width-driven layout pass renders it. No node computes its own breaks.
enum class DocKind : uint8_t {
  Text = 0x01,
  Concat = 0x02,
  Line = 0x03,
  Softline = 0x04,
  Hardline = 0x05,
  Group = 0x06,
  Indent = 0x07,
  IfBreak = 0x08,
  Fill = 0x09,
};

/// \brief An immutable Wadler/Lindig layout document.
///
/// `Doc` is the intermediate representation between a syntax-directed printer and
/// the generic layout renderer (see `DocRenderer`). It is a closed, move-only
/// value tree built only through the named static constructors; the renderer
/// walks it through the read-only accessors. This is the pure formatter core of
/// RFC 0044 Implementation Plan step 2: it depends on no lexer, parser, CST, or
/// filesystem, and re-lexes nothing (`text` bytes are emitted verbatim).
class Doc final {
public:
  Doc(Doc&&) noexcept;
  Doc& operator=(Doc&&) noexcept;
  ZC_DISALLOW_COPY(Doc);
  ~Doc() noexcept;

  /// \brief Literal token or trivia bytes, emitted verbatim and never re-lexed.
  ZC_NODISCARD static Doc text(zc::StringPtr bytes);
  /// \brief Ordered composition of child documents.
  ZC_NODISCARD static Doc concat(zc::Array<Doc>&& docs);
  /// \brief A break rendering as one space when flat, a newline when broken.
  ZC_NODISCARD static Doc line();
  /// \brief A break rendering as nothing when flat, a newline when broken.
  ZC_NODISCARD static Doc softline();
  /// \brief A mandatory newline; a group containing it can never render flat.
  ZC_NODISCARD static Doc hardline();
  /// \brief The unit of break decision: flat if it fits the remaining width.
  ZC_NODISCARD static Doc group(Doc&& doc);
  /// \brief Increase indentation by one four-column step for contained breaks.
  ZC_NODISCARD static Doc indent(Doc&& doc);
  /// \brief Select `broken` or `flat` bytes by the enclosing group's decision.
  ZC_NODISCARD static Doc ifBreak(Doc&& broken, Doc&& flat);
  /// \brief Fit as many items per line as the width allows, breaking as needed.
  ZC_NODISCARD static Doc fill(zc::Array<Doc>&& docs);

  ZC_NODISCARD DocKind kind() const noexcept;

  /// \brief The verbatim bytes of a `Text` node.
  ZC_NODISCARD zc::StringPtr textValue() const;
  /// \brief The ordered children of a `Concat` or `Fill` node.
  ZC_NODISCARD zc::ArrayPtr<const Doc> children() const;
  /// \brief The single child of a `Group` or `Indent` node.
  ZC_NODISCARD const Doc& child() const;
  /// \brief The broken-mode alternative of an `IfBreak` node.
  ZC_NODISCARD const Doc& ifBreakBroken() const;
  /// \brief The flat-mode alternative of an `IfBreak` node.
  ZC_NODISCARD const Doc& ifBreakFlat() const;

private:
  struct Impl;
  explicit Doc(zc::Own<Impl> impl) noexcept;

  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::format
