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
// See the License for the specific language governing permissions and
// limitations under the License.

// RFC 0044 O6/KR6.2 first slice: prove the pure Wadler/Lindig Doc-IR core and
// its width-driven layout renderer are total and deterministic. Documents are
// built by hand and rendered to strings at the pinned 100-column width; there is
// no lexer, parser, CST, or filesystem dependency. This is the pure formatter
// core (Implementation Plan step 2); the syntax-directed printer and CLI are
// later slices.

#include "compiler/format/doc-renderer.h"
#include "compiler/format/doc.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::format {
namespace {

// Builds a Concat from a variadic pack of documents.
template <typename... Docs>
Doc concatOf(Docs&&... docs) {
  auto builder = zc::heapArrayBuilder<Doc>(sizeof...(docs));
  (builder.add(zc::fwd<Docs>(docs)), ...);
  return Doc::concat(builder.finish());
}

template <typename... Docs>
Doc fillOf(Docs&&... docs) {
  auto builder = zc::heapArrayBuilder<Doc>(sizeof...(docs));
  (builder.add(zc::fwd<Docs>(docs)), ...);
  return Doc::fill(builder.finish());
}

zc::String render(const Doc& doc) { return DocRenderer::render(doc); }

// Builds a String of `count` copies of `ch`.
zc::String repeat(size_t count, char ch) {
  auto array = zc::heapArray<char>(count);
  for (auto& slot : array) { slot = ch; }
  return zc::str(array.asPtr());
}

}  // namespace

// A bare text document renders verbatim.

ZC_TEST("Doc renderer emits text verbatim") {
  ZC_EXPECT(render(Doc::text("hello"_zc)) == "hello"_zc);
}

// A group whose flat form fits the pinned width renders flat: `line` is a single
// space and `softline` is nothing.

ZC_TEST("Doc renderer keeps a fitting group flat") {
  auto doc = Doc::group(concatOf(Doc::text("a"_zc), Doc::line(), Doc::text("b"_zc)));
  ZC_EXPECT(render(doc) == "a b"_zc);

  auto tight = Doc::group(concatOf(Doc::text("a"_zc), Doc::softline(), Doc::text("b"_zc)));
  ZC_EXPECT(render(tight) == "ab"_zc);
}

// A group whose flat form exceeds the pinned width breaks: every direct
// `line`/`softline` becomes a newline and the enclosing `indent` applies.

ZC_TEST("Doc renderer breaks an over-wide group and applies indentation") {
  // Two 60-column tokens cannot share a 100-column line, so the group breaks.
  auto wideA = Doc::text(zc::str(repeat(60, 'a')));
  auto wideB = Doc::text(zc::str(repeat(60, 'b')));
  auto doc = Doc::group(Doc::indent(concatOf(zc::mv(wideA), Doc::line(), zc::mv(wideB))));

  auto out = render(doc);
  // The break renders a newline followed by four indent spaces before "bbb...".
  zc::String expected = zc::str(repeat(60, 'a'), "\n    ", repeat(60, 'b'));
  ZC_EXPECT(out == expected);
}

// A hardline forces a break even inside a group that would otherwise fit flat.

ZC_TEST("Doc renderer always breaks on a hardline") {
  auto doc = Doc::group(concatOf(Doc::text("a"_zc), Doc::hardline(), Doc::text("b"_zc)));
  ZC_EXPECT(render(doc) == "a\nb"_zc);
}

// ifBreak selects its flat alternative in a fitting group and its broken
// alternative in a breaking group. This is the trailing-comma idiom.

ZC_TEST("Doc renderer selects ifBreak by the group decision") {
  auto flat =
      Doc::group(concatOf(Doc::text("x"_zc), Doc::ifBreak(Doc::text(","_zc), Doc::text(""_zc))));
  ZC_EXPECT(render(flat) == "x"_zc);

  auto wide = Doc::text(zc::str(repeat(101, 'x')));
  auto broken = Doc::group(
      concatOf(zc::mv(wide), Doc::hardline(), Doc::ifBreak(Doc::text(","_zc), Doc::text(""_zc))));
  auto out = render(broken);
  ZC_EXPECT(out.endsWith(","_zc));
}

// A nested indent accumulates one four-column step per level on each break.

ZC_TEST("Doc renderer accumulates nested indentation") {
  auto inner = Doc::indent(concatOf(Doc::hardline(), Doc::text("inner"_zc)));
  auto doc = Doc::indent(concatOf(Doc::text("outer"_zc), zc::mv(inner)));
  // The hardline sits inside both indent levels, so it accumulates two 4-column
  // steps: eight spaces of indentation.
  ZC_EXPECT(render(doc) == "outer\n        inner"_zc);
}

// fill packs as many items per line as the width allows, breaking only where an
// item would overflow the pinned width.

ZC_TEST("Doc renderer packs a fill and breaks only on overflow") {
  auto small = fillOf(Doc::text("a"_zc), Doc::text("b"_zc), Doc::text("c"_zc));
  ZC_EXPECT(render(small) == "a b c"_zc);

  // A wide middle item forces a break before it but the tail still packs.
  auto wide = Doc::text(zc::str(repeat(100, 'w')));
  auto doc = fillOf(Doc::text("a"_zc), zc::mv(wide), Doc::text("c"_zc));
  auto out = render(doc);
  ZC_EXPECT(out.startsWith("a\n"_zc));
  ZC_EXPECT(out.endsWith("c"_zc));
}

// Rendering is idempotent in the sense the RFC requires: rendering the same Doc
// twice yields identical bytes (the layout pass is a total, deterministic
// function of the document).

ZC_TEST("Doc renderer is deterministic") {
  auto build = []() {
    return Doc::group(Doc::indent(concatOf(Doc::text("f("_zc), Doc::softline(), Doc::text("arg"_zc),
                                           Doc::ifBreak(Doc::text(","_zc), Doc::text(""_zc)),
                                           Doc::softline(), Doc::text(")"_zc))));
  };
  ZC_EXPECT(render(build()) == render(build()));
}

}  // namespace zomlang::compiler::format
