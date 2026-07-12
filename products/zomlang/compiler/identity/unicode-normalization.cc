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

#include "zomlang/compiler/identity/unicode-normalization.h"

#include "zc/core/encoding.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/identity/unicode-normalization-data.h"

namespace zomlang::compiler::identity {
namespace {

constexpr uint32_t kHangulSyllableBase = 0xAC00;
constexpr uint32_t kHangulLeadingJamoBase = 0x1100;
constexpr uint32_t kHangulVowelJamoBase = 0x1161;
constexpr uint32_t kHangulTrailingJamoBase = 0x11A7;
constexpr uint32_t kHangulLeadingCount = 19;
constexpr uint32_t kHangulVowelCount = 21;
constexpr uint32_t kHangulTrailingCount = 28;
constexpr uint32_t kHangulSyllableBlock = kHangulVowelCount * kHangulTrailingCount;
constexpr uint32_t kHangulSyllableCount = kHangulLeadingCount * kHangulSyllableBlock;

uint8_t combiningClass(uint32_t codePoint) {
  size_t left = 0;
  size_t right = UNICODE_COMBINING_CLASSES.size();
  while (left < right) {
    const size_t middle = left + (right - left) / 2;
    const auto& entry = UNICODE_COMBINING_CLASSES[middle];
    if (codePoint < entry.codePoint) {
      right = middle;
    } else if (codePoint > entry.codePoint) {
      left = middle + 1;
    } else {
      return entry.combiningClass;
    }
  }
  return 0;
}

zc::Maybe<const UnicodeDecompositionEntry&> findDecomposition(uint32_t codePoint) {
  size_t left = 0;
  size_t right = UNICODE_DECOMPOSITIONS.size();
  while (left < right) {
    const size_t middle = left + (right - left) / 2;
    const auto& entry = UNICODE_DECOMPOSITIONS[middle];
    if (codePoint < entry.codePoint) {
      right = middle;
    } else if (codePoint > entry.codePoint) {
      left = middle + 1;
    } else {
      return entry;
    }
  }
  return zc::none;
}

zc::Maybe<uint32_t> findTableComposition(uint32_t starter, uint32_t combining) {
  size_t left = 0;
  size_t right = UNICODE_COMPOSITIONS.size();
  while (left < right) {
    const size_t middle = left + (right - left) / 2;
    const auto& entry = UNICODE_COMPOSITIONS[middle];
    if (starter < entry.starter || (starter == entry.starter && combining < entry.combining)) {
      right = middle;
    } else if (starter > entry.starter ||
               (starter == entry.starter && combining > entry.combining)) {
      left = middle + 1;
    } else {
      return entry.composite;
    }
  }
  return zc::none;
}

zc::Maybe<const UnicodeCaseFoldEntry&> findCaseFold(uint32_t codePoint) {
  size_t left = 0;
  size_t right = UNICODE_CASE_FOLDS.size();
  while (left < right) {
    const size_t middle = left + (right - left) / 2;
    const auto& entry = UNICODE_CASE_FOLDS[middle];
    if (codePoint < entry.codePoint) {
      right = middle;
    } else if (codePoint > entry.codePoint) {
      left = middle + 1;
    } else {
      return entry;
    }
  }
  return zc::none;
}

zc::Maybe<uint32_t> findHangulComposition(uint32_t starter, uint32_t combining) {
  const uint32_t leadingIndex = starter - kHangulLeadingJamoBase;
  if (leadingIndex < kHangulLeadingCount) {
    const uint32_t vowelIndex = combining - kHangulVowelJamoBase;
    if (vowelIndex < kHangulVowelCount) {
      return kHangulSyllableBase +
             (leadingIndex * kHangulVowelCount + vowelIndex) * kHangulTrailingCount;
    }
  }

  const uint32_t syllableIndex = starter - kHangulSyllableBase;
  if (syllableIndex < kHangulSyllableCount && syllableIndex % kHangulTrailingCount == 0) {
    const uint32_t trailingIndex = combining - kHangulTrailingJamoBase;
    if (trailingIndex > 0 && trailingIndex < kHangulTrailingCount) {
      return starter + trailingIndex;
    }
  }
  return zc::none;
}

zc::Maybe<uint32_t> findComposition(uint32_t starter, uint32_t combining) {
  ZC_IF_SOME(hangul, findHangulComposition(starter, combining)) { return hangul; }
  return findTableComposition(starter, combining);
}

void appendCanonicalDecomposition(uint32_t codePoint, zc::Vector<char32_t>& output) {
  const uint32_t syllableIndex = codePoint - kHangulSyllableBase;
  if (syllableIndex < kHangulSyllableCount) {
    output.add(
        static_cast<char32_t>(kHangulLeadingJamoBase + syllableIndex / kHangulSyllableBlock));
    output.add(static_cast<char32_t>(kHangulVowelJamoBase + (syllableIndex % kHangulSyllableBlock) /
                                                                kHangulTrailingCount));
    const uint32_t trailingIndex = syllableIndex % kHangulTrailingCount;
    if (trailingIndex != 0) {
      output.add(static_cast<char32_t>(kHangulTrailingJamoBase + trailingIndex));
    }
    return;
  }

  ZC_IF_SOME(entry, findDecomposition(codePoint)) {
    const auto values =
        UNICODE_DECOMPOSITION_SCALARS.slice(entry.offset, entry.offset + entry.length);
    for (uint32_t value : values) { output.add(static_cast<char32_t>(value)); }
    return;
  }
  output.add(static_cast<char32_t>(codePoint));
}

void reorderCanonical(zc::Vector<char32_t>& values) {
  for (size_t index = 1; index < values.size(); ++index) {
    const char32_t current = values[index];
    const uint8_t currentClass = combiningClass(static_cast<uint32_t>(current));
    if (currentClass == 0) { continue; }

    size_t insertion = index;
    while (insertion > 0) {
      const uint8_t previousClass = combiningClass(static_cast<uint32_t>(values[insertion - 1]));
      if (previousClass == 0 || previousClass <= currentClass) { break; }
      values[insertion] = values[insertion - 1];
      --insertion;
    }
    values[insertion] = current;
  }
}

zc::Vector<char32_t> composeCanonical(zc::Maybe<zc::MemoryResource&> resource,
                                      zc::ArrayPtr<const char32_t> values) {
  zc::Vector<char32_t> output = [&] {
    ZC_IF_SOME(value, resource) { return zc::Vector<char32_t>(value, values.size()); }
    return zc::Vector<char32_t>(values.size());
  }();
  if (values.size() == 0) { return output; }

  output.add(values[0]);
  size_t starterIndex = 0;
  uint32_t starter = static_cast<uint32_t>(values[0]);
  uint8_t previousClass = combiningClass(starter);
  for (size_t index = 1; index < values.size(); ++index) {
    const uint32_t current = static_cast<uint32_t>(values[index]);
    const uint8_t currentClass = combiningClass(current);
    auto composite = findComposition(starter, current);
    if (composite != zc::none && (previousClass == 0 || previousClass < currentClass)) {
      ZC_IF_SOME(value, composite) {
        output[starterIndex] = static_cast<char32_t>(value);
        starter = value;
      }
      continue;
    }

    if (currentClass == 0) {
      starterIndex = output.size();
      starter = current;
    }
    previousClass = currentClass;
    output.add(static_cast<char32_t>(current));
  }
  return output;
}

}  // namespace

zc::Maybe<zc::String> normalizeNfcImpl(zc::Maybe<zc::MemoryResource&> resource,
                                       zc::StringPtr input) {
  auto decoded = [&] {
    ZC_IF_SOME(value, resource) { return zc::encodeUtf32(value, input); }
    return zc::encodeUtf32(input);
  }();
  if (decoded == zc::none) { return zc::none; }

  zc::Vector<char32_t> decomposed = [&] {
    ZC_IF_SOME(value, resource) { return zc::Vector<char32_t>(value, decoded.size()); }
    return zc::Vector<char32_t>(decoded.size());
  }();
  for (char32_t codePoint : decoded) {
    appendCanonicalDecomposition(static_cast<uint32_t>(codePoint), decomposed);
  }
  reorderCanonical(decomposed);
  auto composed = composeCanonical(resource, decomposed.asPtr());
  auto encoded = [&] {
    ZC_IF_SOME(value, resource) { return zc::decodeUtf32(value, composed.asPtr()); }
    return zc::decodeUtf32(composed.asPtr());
  }();
  if (encoded == zc::none) { return zc::none; }
  return zc::mv(encoded);
}

zc::Maybe<zc::String> normalizeNfc(zc::StringPtr input) {
  return normalizeNfcImpl(zc::none, input);
}

zc::Maybe<zc::String> normalizeNfc(zc::MemoryResource& resource, zc::StringPtr input) {
  return normalizeNfcImpl(resource, input);
}

zc::Maybe<bool> isNfc(zc::StringPtr input) {
  ZC_IF_SOME(normalized, normalizeNfc(input)) { return normalized == input; }
  return zc::none;
}

zc::Maybe<bool> isNfc(zc::MemoryResource& resource, zc::StringPtr input) {
  ZC_IF_SOME(normalized, normalizeNfc(resource, input)) { return normalized == input; }
  return zc::none;
}

zc::Maybe<zc::String> fullCaseFold(zc::StringPtr input) {
  auto decoded = zc::encodeUtf32(input);
  if (decoded == zc::none) { return zc::none; }

  zc::Vector<char32_t> folded(decoded.size());
  for (char32_t codePoint : decoded) {
    ZC_IF_SOME(entry, findCaseFold(static_cast<uint32_t>(codePoint))) {
      const auto values =
          UNICODE_CASE_FOLD_SCALARS.slice(entry.offset, entry.offset + entry.length);
      for (uint32_t value : values) { folded.add(static_cast<char32_t>(value)); }
    }
    else { folded.add(codePoint); }
  }
  auto encoded = zc::decodeUtf32(folded.asPtr());
  if (encoded == zc::none) { return zc::none; }
  return zc::mv(encoded);
}

}  // namespace zomlang::compiler::identity
