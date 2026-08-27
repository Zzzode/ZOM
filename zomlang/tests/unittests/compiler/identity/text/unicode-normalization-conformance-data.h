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
// Contains test data derived from Unicode Character Database 15.1.0.
// See third_party/unicode/LICENSE.txt and third_party/unicode/README.md.

#pragma once

#include <cstddef>
#include <cstdint>

#include "zc/core/array.h"

namespace zomlang::compiler::identity::test {

struct UnicodeNormalizationInput final {
  uint32_t offset;
  uint16_t length;
};

inline constexpr char kUnicodeNormalizationTestVersion[] = "15.1.0";
inline constexpr char kUnicodeNormalizationTestSource[] =
    "https://www.unicode.org/Public/15.1.0/ucd/NormalizationTest.txt";
inline constexpr char kUnicodeNormalizationExpectedDigest[] =
    "e2ab0b55ce326a724957b79efe63290de3c971a0aa5166cedec05eb77e448d5b";
inline constexpr size_t kUnicodeNormalizationInputCount = 36482;
inline constexpr size_t kUnicodeNormalizationInputByteCount = 216903;

extern const zc::ArrayPtr<const UnicodeNormalizationInput> UNICODE_NORMALIZATION_INPUTS;
extern const zc::ArrayPtr<const uint8_t> UNICODE_NORMALIZATION_INPUT_BYTES;

}  // namespace zomlang::compiler::identity::test
