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
#include "zc/core/string.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::format {

/// \brief One canonical source replacement: an original half-open byte range
/// `[start, end)` and the replacement UTF-8 text.
///
/// See RFC 0044 "Input And Result". A replacement is built only through the
/// validating factory; `start <= end` is enforced. An insertion is the empty
/// range `[p, p)` with non-empty text; a deletion is a non-empty range with
/// empty text.
class SourceReplacement final {
public:
  SourceReplacement(SourceReplacement&&) noexcept = default;
  SourceReplacement& operator=(SourceReplacement&&) noexcept = default;
  ZC_DISALLOW_COPY(SourceReplacement);
  ~SourceReplacement() noexcept = default;

  /// \brief Builds a replacement over `[start, end)` with `text`.
  /// \return none when `end < start`.
  ZC_NODISCARD static zc::Maybe<SourceReplacement> make(uint64_t start, uint64_t end,
                                                        zc::StringPtr text);

  ZC_NODISCARD uint64_t start() const noexcept { return startValue; }
  ZC_NODISCARD uint64_t end() const noexcept { return endValue; }
  ZC_NODISCARD zc::StringPtr text() const noexcept { return textValue; }

  ZC_NODISCARD SourceReplacement clone() const;

private:
  SourceReplacement(uint64_t start, uint64_t end, zc::String&& text) noexcept
      : startValue(start), endValue(end), textValue(zc::mv(text)) {}

  uint64_t startValue;
  uint64_t endValue;
  zc::String textValue;
};

/// \brief The canonical result of one format request.
///
/// RFC 0044 "Input And Result": exactly one of `Unchanged`, `Edits`, or
/// `Rejected`. `Edits` carries sorted, disjoint, adjacent-merged replacements.
enum class FormatOutcome : uint8_t {
  Unchanged = 0x01,
  Edits = 0x02,
  Rejected = 0x03,
};

/// \brief A canonical, immutable format-edit result.
///
/// `unchanged()` and `rejected()` carry no replacements. `edits()` carries a
/// sorted, disjoint, adjacent-merged sequence produced by `normalize`. Rejected
/// input produces no edit prefix (an empty replacement list).
class FormatResult final {
public:
  FormatResult(FormatResult&&) noexcept = default;
  FormatResult& operator=(FormatResult&&) noexcept = default;
  ZC_DISALLOW_COPY(FormatResult);
  ~FormatResult() noexcept = default;

  ZC_NODISCARD static FormatResult unchanged() noexcept;
  ZC_NODISCARD static FormatResult rejected() noexcept;

  /// \brief Normalizes `replacements` into a canonical `Edits` (or `Unchanged`).
  ///
  /// Sorts by start offset, rejects any overlap (two replacements whose ranges
  /// intersect), and merges adjacent replacements (one range's end equals the
  /// next range's start) into a single replacement. An empty input, or a set
  /// whose only replacements neither change bytes nor exist, yields `Unchanged`.
  /// \return The canonical result, or `Rejected` on overlapping ranges.
  ZC_NODISCARD static FormatResult normalize(zc::Array<SourceReplacement>&& replacements);

  ZC_NODISCARD FormatOutcome outcome() const noexcept { return outcomeValue; }
  ZC_NODISCARD zc::ArrayPtr<const SourceReplacement> replacements() const noexcept {
    return replacementValues.asPtr();
  }

  /// \brief Applies the result to `source`, producing the canonical bytes.
  ///
  /// For `Unchanged`/`Rejected` the source is returned verbatim. For `Edits` the
  /// sorted disjoint replacements are spliced in order. Ranges outside the
  /// source length are clamped by the caller's contract; this operation assumes
  /// in-range canonical edits from `normalize`.
  ZC_NODISCARD zc::String apply(zc::StringPtr source) const;

private:
  FormatResult(FormatOutcome outcome, zc::Vector<SourceReplacement>&& replacements) noexcept
      : outcomeValue(outcome), replacementValues(zc::mv(replacements)) {}

  FormatOutcome outcomeValue;
  zc::Vector<SourceReplacement> replacementValues;
};

}  // namespace zomlang::compiler::format
