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

#include "zomlang/compiler/identity/text/unicode-normalization.h"

#include "unicode-normalization-conformance-data.h"
#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::identity {
namespace {

void expectNormalization(zc::StringPtr input, zc::StringPtr expected) {
  auto normalized = normalizeNfc(input);
  bool matched = false;
  ZC_IF_SOME(value, normalized) {
    ZC_EXPECT(value == expected);
    auto repeated = normalizeNfc(value);
    ZC_IF_SOME(repeatedValue, repeated) {
      ZC_EXPECT(repeatedValue == value);
      matched = true;
    }
  }
  ZC_EXPECT(matched);
}

void expectCaseFold(zc::StringPtr input, zc::StringPtr expected) {
  bool matched = false;
  ZC_IF_SOME(value, fullCaseFold(input)) {
    ZC_EXPECT(value == expected);
    matched = true;
  }
  ZC_EXPECT(matched);
}

}  // namespace

ZC_TEST("NFC composes canonical Latin and Hangul sequences") {
  expectNormalization("\x65\xCC\x81"_zc, "\xC3\xA9"_zc);
  expectNormalization("\x41\xCC\x8A"_zc, "\xC3\x85"_zc);
  expectNormalization("\xE1\x84\x80\xE1\x85\xA1"_zc, "\xEA\xB0\x80"_zc);
}

ZC_TEST("NFC applies canonical ordering before composition") {
  expectNormalization("\x71\xCC\x87\xCC\xA3"_zc, "\x71\xCC\xA3\xCC\x87"_zc);
}

ZC_TEST("NFC honors singleton mappings and composition exclusions") {
  expectNormalization("\xE2\x84\xA6"_zc, "\xCE\xA9"_zc);
  expectNormalization("\xE0\xA5\x98"_zc, "\xE0\xA4\x95\xE0\xA4\xBC"_zc);
}

ZC_TEST("NFC preserves compatibility-only characters") {
  expectNormalization("\xE2\x84\x8C"_zc, "\xE2\x84\x8C"_zc);
}

ZC_TEST("NFC rejects malformed UTF-8 and reports canonical state") {
  ZC_EXPECT(normalizeNfc("\xC0\x80"_zc) == zc::none);
  ZC_EXPECT(isNfc("\xC3\xA9"_zc) == true);
  ZC_EXPECT(isNfc("\x65\xCC\x81"_zc) == false);
  ZC_EXPECT(isNfc("\xC0\x80"_zc) == zc::none);
}

ZC_TEST("Full case folding uses Unicode 15.1 default mappings") {
  expectCaseFold("ABC"_zc, "abc"_zc);
  expectCaseFold("\xC3\x9F"_zc, "ss"_zc);
  expectCaseFold("\xC4\xB0"_zc, "i\xCC\x87"_zc);
  expectCaseFold("\xEF\xAC\x83"_zc, "ffi"_zc);
  ZC_EXPECT(fullCaseFold("\xC0\x80"_zc) == zc::none);
}

ZC_TEST("NFC passes every Unicode 15.1.0 NormalizationTest input") {
  CanonicalEncoder encoder;
  uint32_t ordinal = 0;
  for (const auto& entry : test::UNICODE_NORMALIZATION_INPUTS) {
    const auto inputBytes =
        test::UNICODE_NORMALIZATION_INPUT_BYTES.slice(entry.offset, entry.offset + entry.length);
    auto input = zc::heapString(inputBytes.asChars());
    auto normalized = normalizeNfc(input);
    bool encoded = false;
    ZC_IF_SOME(value, normalized) {
      encoder.encodeByteString(inputBytes);
      encoder.encodeByteString(value.asBytes());
      encoded = true;
    }
    ZC_REQUIRE(encoded, ordinal);
    ++ordinal;
  }

  auto frame = encoder.finish();
  auto digest = sha256(frame.asPtr());
  bool matched = false;
  ZC_IF_SOME(value, digest) {
    ZC_EXPECT(zc::encodeHex(value.bytes()) == test::kUnicodeNormalizationExpectedDigest);
    matched = true;
  }
  ZC_EXPECT(matched);
}

}  // namespace zomlang::compiler::identity
