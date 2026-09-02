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
//
// Contains tables derived from Unicode Character Database 15.1.0.
// See thirdparty/unicode/LICENSE.txt and thirdparty/unicode/README.md.

#pragma once

#include <cstddef>
#include <cstdint>

#include "zc/core/array.h"

namespace zomlang::compiler::identity {

struct UnicodeCombiningClassEntry final {
  uint32_t codePoint;
  uint8_t combiningClass;
};

struct UnicodeDecompositionEntry final {
  uint32_t codePoint;
  uint32_t offset;
  uint16_t length;
};

struct UnicodeCompositionEntry final {
  uint32_t starter;
  uint32_t combining;
  uint32_t composite;
};

struct UnicodeCaseFoldEntry final {
  uint32_t codePoint;
  uint32_t offset;
  uint8_t length;
};

inline constexpr char kUnicodeNormalizationDataVersion[] = "15.1.0";
inline constexpr char kUnicodeNormalizationUnicodeDataSource[] =
    "https://www.unicode.org/Public/15.1.0/ucd/UnicodeData.txt";
inline constexpr char kUnicodeNormalizationPropertiesSource[] =
    "https://www.unicode.org/Public/15.1.0/ucd/DerivedNormalizationProps.txt";
inline constexpr char kUnicodeCaseFoldingSource[] =
    "https://www.unicode.org/Public/15.1.0/ucd/CaseFolding.txt";

inline constexpr size_t kUnicodeCombiningClassCount = 922;
inline constexpr size_t kUnicodeDecompositionCount = 2061;
inline constexpr size_t kUnicodeDecompositionScalarCount = 3406;
inline constexpr size_t kUnicodeCompositionCount = 941;
inline constexpr size_t kUnicodeCaseFoldCount = 1530;
inline constexpr size_t kUnicodeCaseFoldScalarCount = 1650;

extern const zc::ArrayPtr<const UnicodeCombiningClassEntry> UNICODE_COMBINING_CLASSES;
extern const zc::ArrayPtr<const UnicodeDecompositionEntry> UNICODE_DECOMPOSITIONS;
extern const zc::ArrayPtr<const uint32_t> UNICODE_DECOMPOSITION_SCALARS;
extern const zc::ArrayPtr<const UnicodeCompositionEntry> UNICODE_COMPOSITIONS;
extern const zc::ArrayPtr<const UnicodeCaseFoldEntry> UNICODE_CASE_FOLDS;
extern const zc::ArrayPtr<const uint32_t> UNICODE_CASE_FOLD_SCALARS;

}  // namespace zomlang::compiler::identity
