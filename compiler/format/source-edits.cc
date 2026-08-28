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

#include "compiler/format/source-edits.h"

#include "zc/core/debug.h"

namespace zomlang::compiler::format {

zc::Maybe<SourceReplacement> SourceReplacement::make(uint64_t start, uint64_t end,
                                                     zc::StringPtr text) {
  if (end < start) { return zc::none; }
  return SourceReplacement(start, end, zc::str(text));
}

SourceReplacement SourceReplacement::clone() const {
  return SourceReplacement(startValue, endValue, zc::str(textValue));
}

FormatResult FormatResult::unchanged() noexcept {
  return FormatResult(FormatOutcome::Unchanged, zc::Vector<SourceReplacement>());
}

FormatResult FormatResult::rejected() noexcept {
  return FormatResult(FormatOutcome::Rejected, zc::Vector<SourceReplacement>());
}

FormatResult FormatResult::normalize(zc::Array<SourceReplacement>&& replacements) {
  if (replacements.size() == 0) { return unchanged(); }

  // Sort by start offset (then end) with a stable insertion sort; the sets are
  // small and this keeps the ordering deterministic.
  zc::Vector<SourceReplacement> sorted(replacements.size());
  for (auto& replacement : replacements) { sorted.add(zc::mv(replacement)); }
  for (size_t i = 1; i < sorted.size(); ++i) {
    for (size_t j = i; j > 0; --j) {
      const bool inOrder =
          sorted[j - 1].start() < sorted[j].start() ||
          (sorted[j - 1].start() == sorted[j].start() && sorted[j - 1].end() <= sorted[j].end());
      if (inOrder) { break; }
      auto tmp = zc::mv(sorted[j]);
      sorted[j] = zc::mv(sorted[j - 1]);
      sorted[j - 1] = zc::mv(tmp);
    }
  }

  // Reject any overlap: a replacement whose range starts before the previous
  // replacement's end. Merge adjacent replacements (previous end == next start)
  // into a single replacement spanning both ranges with concatenated text.
  zc::Vector<SourceReplacement> merged;
  for (auto& replacement : sorted) {
    if (merged.size() != 0) {
      auto& previous = merged.back();
      if (replacement.start() < previous.end()) { return rejected(); }
      if (replacement.start() == previous.end()) {
        auto combined = SourceReplacement::make(previous.start(), replacement.end(),
                                                zc::str(previous.text(), replacement.text()));
        merged.back() = ZC_ASSERT_NONNULL(zc::mv(combined));
        continue;
      }
    }
    merged.add(zc::mv(replacement));
  }

  // A replacement whose range is empty and whose text is empty changes nothing.
  bool anyChange = false;
  for (const auto& replacement : merged) {
    if (replacement.start() != replacement.end() || replacement.text().size() != 0) {
      anyChange = true;
      break;
    }
  }
  if (!anyChange) { return unchanged(); }

  return FormatResult(FormatOutcome::Edits, zc::mv(merged));
}

zc::String FormatResult::apply(zc::StringPtr source) const {
  if (outcomeValue != FormatOutcome::Edits) { return zc::str(source); }
  const auto bytes = source.asArray();
  zc::Vector<char> output;
  uint64_t cursor = 0;
  for (const auto& replacement : replacementValues) {
    // Copy the unedited span before this replacement.
    for (uint64_t index = cursor; index < replacement.start() && index < bytes.size(); ++index) {
      output.add(bytes[index]);
    }
    for (const auto byte : replacement.text()) { output.add(byte); }
    cursor = replacement.end();
  }
  for (uint64_t index = cursor; index < bytes.size(); ++index) { output.add(bytes[index]); }
  return zc::str(output.asPtr());
}

}  // namespace zomlang::compiler::format
