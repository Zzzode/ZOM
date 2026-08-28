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

#include "compiler/format/doc-renderer.h"

#include "zc/core/debug.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::format {
namespace {

// The scalar-value width of a text token. This slice treats each byte as one
// column; multi-byte UTF-8 scalar accounting is a later refinement and does not
// change the layout decisions for the ASCII structural tokens exercised here.
uint32_t textWidth(zc::StringPtr bytes) noexcept { return static_cast<uint32_t>(bytes.size()); }

// The flat width of `doc`, or none when it can never render flat because it
// contains a hardline. In flat mode `line` is one space, `softline` is nothing,
// `ifBreak` takes its flat alternative, and `fill` is fully packed (its
// separators flat). This is the "flat width plus contains-no-hardline" test the
// RFC's group decision uses, computed as one total function.
zc::Maybe<uint32_t> flatWidth(const Doc& doc) {
  switch (doc.kind()) {
    case DocKind::Text:
      return textWidth(doc.textValue());
    case DocKind::Line:
      return static_cast<uint32_t>(1);
    case DocKind::Softline:
      return static_cast<uint32_t>(0);
    case DocKind::Hardline:
      return zc::none;
    case DocKind::Group:
    case DocKind::Indent:
      return flatWidth(doc.child());
    case DocKind::IfBreak:
      return flatWidth(doc.ifBreakFlat());
    case DocKind::Concat:
    case DocKind::Fill: {
      uint32_t total = 0;
      for (const auto& child : doc.children()) {
        auto width = flatWidth(child);
        if (width == zc::none) { return zc::none; }
        total += ZC_REQUIRE_NONNULL(width);
      }
      return total;
    }
  }
  ZC_UNREACHABLE
}

// True when `doc`, rendered flat starting at `column`, stays within the width.
bool fitsFlat(const Doc& doc, uint32_t column) {
  auto width = flatWidth(doc);
  if (width == zc::none) { return false; }
  return column + ZC_REQUIRE_NONNULL(width) <= kTargetWidth;
}

class Layout final {
public:
  ZC_NODISCARD zc::String finish() { return zc::str(output.asPtr()); }

  // Renders `doc`. `broken` is the enclosing group's decision; `indentCols` is
  // the current indentation applied after each newline. `column` tracks the
  // current output column and is updated as bytes are appended.
  void render(const Doc& doc, bool broken, uint32_t indentCols, uint32_t& column) {
    switch (doc.kind()) {
      case DocKind::Text:
        appendText(doc.textValue(), column);
        return;
      case DocKind::Concat:
        for (const auto& child : doc.children()) { render(child, broken, indentCols, column); }
        return;
      case DocKind::Line:
        if (broken) {
          appendBreak(indentCols, column);
        } else {
          appendText(" "_zc, column);
        }
        return;
      case DocKind::Softline:
        if (broken) { appendBreak(indentCols, column); }
        return;
      case DocKind::Hardline:
        appendBreak(indentCols, column);
        return;
      case DocKind::Group: {
        // The unit of break decision: render flat when the flat form fits at the
        // current column and carries no hardline, otherwise render broken.
        const bool flat = fitsFlat(doc, column);
        render(doc.child(), !flat, indentCols, column);
        return;
      }
      case DocKind::Indent:
        render(doc.child(), broken, indentCols + kIndentStep, column);
        return;
      case DocKind::IfBreak:
        render(broken ? doc.ifBreakBroken() : doc.ifBreakFlat(), broken, indentCols, column);
        return;
      case DocKind::Fill:
        renderFill(doc, indentCols, column);
        return;
    }
    ZC_UNREACHABLE
  }

private:
  void appendText(zc::StringPtr bytes, uint32_t& column) {
    for (const auto byte : bytes) { output.add(byte); }
    column += textWidth(bytes);
  }

  void appendBreak(uint32_t indentCols, uint32_t& column) {
    output.add('\n');
    for (uint32_t i = 0; i < indentCols; ++i) { output.add(' '); }
    column = indentCols;
  }

  // Fill packs as many items per line as the width allows. Items alternate with
  // implicit soft separators: an item renders flat and stays on the line when it
  // fits at the current column, otherwise a break precedes it.
  void renderFill(const Doc& doc, uint32_t indentCols, uint32_t& column) {
    bool first = true;
    for (const auto& item : doc.children()) {
      if (!first && !fitsFlat(item, column)) {
        appendBreak(indentCols, column);
      } else if (!first) {
        appendText(" "_zc, column);
      }
      const bool flat = fitsFlat(item, column);
      render(item, !flat, indentCols, column);
      first = false;
    }
  }

  zc::Vector<char> output;
};

}  // namespace

zc::String DocRenderer::render(const Doc& doc) {
  Layout layout;
  uint32_t column = 0;
  layout.render(doc, !fitsFlat(doc, column), 0, column);
  return layout.finish();
}

}  // namespace zomlang::compiler::format
