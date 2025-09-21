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

/// \brief Unicode ID_Start code point ranges
/// Based on Unicode 15.0.0 specification
/// These ranges define characters that can start an identifier
extern const zc::ArrayPtr<const UnicodeRange> ID_START_RANGES;

/// \brief Unicode ID_Continue code point ranges
/// Based on Unicode 15.0.0 specification
/// These ranges define characters that can continue an identifier
extern const zc::ArrayPtr<const UnicodeRange> ID_CONTINUE_RANGES;

/// \brief Check if a code point is in ID_Start category
/// \param codePoint The Unicode code point to check
/// \return true if the code point can start an identifier
bool isIdStart(uint32_t codePoint);

/// \brief Check if a code point is in ID_Continue category
/// \param codePoint The Unicode code point to check
/// \return true if the code point can continue an identifier
bool isIdContinue(uint32_t codePoint);

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang