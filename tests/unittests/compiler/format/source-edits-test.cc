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

// RFC 0044 O6/KR6.2 canonical edit set (Implementation Plan step 3): the pure
// `SourceReplacement` / `FormatResult` value types normalize a replacement set
// into sorted, disjoint, adjacent-merged edits and apply them to source bytes.
// No lexer, parser, CST, printer, or filesystem is involved.

#include "compiler/format/source-edits.h"

#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::format {
namespace {

SourceReplacement replacement(uint64_t start, uint64_t end, zc::StringPtr text) {
  auto value = SourceReplacement::make(start, end, text);
  ZC_REQUIRE(value != zc::none);
  return ZC_REQUIRE_NONNULL(zc::mv(value));
}

}  // namespace

// A replacement over an inverted range fails closed; equal start/end (an
// insertion point) is valid.

ZC_TEST("Source replacement rejects an inverted range") {
  ZC_EXPECT(SourceReplacement::make(5, 3, "x"_zc) == zc::none);
  ZC_EXPECT(SourceReplacement::make(3, 3, "x"_zc) != zc::none);
  ZC_EXPECT(SourceReplacement::make(2, 5, ""_zc) != zc::none);
}

// An empty replacement set is Unchanged; a set whose only edits change nothing
// is Unchanged.

ZC_TEST("Format result normalizes an empty or no-op set to Unchanged") {
  ZC_EXPECT(FormatResult::normalize(zc::Array<SourceReplacement>()).outcome() ==
            FormatOutcome::Unchanged);

  auto builder = zc::heapArrayBuilder<SourceReplacement>(1);
  builder.add(replacement(4, 4, ""_zc));  // empty range, empty text: no change
  ZC_EXPECT(FormatResult::normalize(builder.finish()).outcome() == FormatOutcome::Unchanged);
}

// Overlapping ranges are rejected with no edit prefix.

ZC_TEST("Format result rejects overlapping replacements") {
  auto builder = zc::heapArrayBuilder<SourceReplacement>(2);
  builder.add(replacement(0, 5, "a"_zc));
  builder.add(replacement(3, 8, "b"_zc));  // overlaps [0,5)
  auto result = FormatResult::normalize(builder.finish());
  ZC_EXPECT(result.outcome() == FormatOutcome::Rejected);
  ZC_EXPECT(result.replacements().size() == 0);
}

// Out-of-order disjoint replacements are sorted; adjacent replacements (one's
// end equals the next's start) merge into a single replacement.

ZC_TEST("Format result sorts and merges adjacent replacements") {
  auto builder = zc::heapArrayBuilder<SourceReplacement>(3);
  builder.add(replacement(10, 12, "z"_zc));  // out of order
  builder.add(replacement(0, 2, "a"_zc));
  builder.add(replacement(2, 4, "b"_zc));  // adjacent to [0,2)
  auto result = FormatResult::normalize(builder.finish());
  ZC_REQUIRE(result.outcome() == FormatOutcome::Edits);
  ZC_REQUIRE(result.replacements().size() == 2);
  // [0,2)+[2,4) merged into [0,4) with "ab"; then [10,12) with "z".
  ZC_EXPECT(result.replacements()[0].start() == 0);
  ZC_EXPECT(result.replacements()[0].end() == 4);
  ZC_EXPECT(result.replacements()[0].text() == "ab"_zc);
  ZC_EXPECT(result.replacements()[1].start() == 10);
  ZC_EXPECT(result.replacements()[1].end() == 12);
  ZC_EXPECT(result.replacements()[1].text() == "z"_zc);
}

// Applying an Edits result splices the sorted disjoint replacements into the
// source; Unchanged/Rejected return the source verbatim.

ZC_TEST("Format result applies edits to source bytes") {
  auto builder = zc::heapArrayBuilder<SourceReplacement>(2);
  builder.add(replacement(0, 1, "H"_zc));  // replace 'h' -> 'H'
  builder.add(replacement(5, 5, "!"_zc));  // insert '!' at offset 5
  auto result = FormatResult::normalize(builder.finish());
  ZC_REQUIRE(result.outcome() == FormatOutcome::Edits);
  ZC_EXPECT(result.apply("hello"_zc) == "Hello!"_zc);

  ZC_EXPECT(FormatResult::unchanged().apply("hello"_zc) == "hello"_zc);
  ZC_EXPECT(FormatResult::rejected().apply("hello"_zc) == "hello"_zc);
}

}  // namespace zomlang::compiler::format
