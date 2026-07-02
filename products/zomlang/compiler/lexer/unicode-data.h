// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include <cstddef>
#include <cstdint>

#include "zc/core/common.h"

namespace zomlang {
namespace compiler {
namespace lexer {

/// \brief Unicode code point range for efficient range checking
struct UnicodeRange {
  uint32_t start;
  uint32_t end;
};

/// \brief Unicode Character Database release used for identifier tables.
inline constexpr char kUnicodeIdentifierDataVersion[] = "15.1.0";

/// \brief UCD property file used by scripts/codegen/gen_unicode_data.py.
inline constexpr char kUnicodeIdentifierDataSource[] =
    "https://www.unicode.org/Public/15.1.0/ucd/DerivedCoreProperties.txt";

inline constexpr size_t kUnicodeIdentifierStartRangeCount = 660;
inline constexpr size_t kUnicodeIdentifierPartRangeCount = 769;

/// \brief Corresponds to the ID_Start and Other_ID_Start property
extern const zc::ArrayPtr<const UnicodeRange> ID_START_RANGES;

/// \brief Corresponds to ID_Continue, Other_ID_Continue, plus ID_Start and Other_ID_Start
extern const zc::ArrayPtr<const UnicodeRange> ID_PART_RANGES;

/// \brief Check if a code point is in ID_Start and Other_ID_Start category
/// \param codePoint The Unicode code point to check
/// \return true if the code point can start an identifier
bool isIdStart(uint32_t codePoint);

/// \brief Check if a code point is in ID_Continue, Other_ID_Continue, plus ID_Start and
/// Other_ID_Start category
/// \param codePoint The Unicode code point to check
/// \return true if the code point can continue an identifier
bool isIdPart(uint32_t codePoint);

/// \brief Check if a code point is in a given range of Unicode code points
/// \param codePoint The Unicode code point to check
/// \param ranges The range of Unicode code points to check against
/// \return true if the code point is in the range
bool isInUnicodeRange(uint32_t codePoint, const zc::ArrayPtr<const UnicodeRange>& ranges);

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
