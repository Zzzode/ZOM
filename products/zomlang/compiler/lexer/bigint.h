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

#include "zc/core/string.h"
#include "zc/core/vector.h"

namespace zomlang {
namespace compiler {
namespace lexer {

// Convert a BigInt literal text (e.g. "0b101n", "0o755n", "0xFFn", or decimal "123n")
// into a base-10 string without the trailing 'n'.
//
// Algorithm overview:
// - Detect base by the second character after leading '0' (b/B, o/O, x/X). If none, treat as
//   decimal, drop trailing 'n', and strip leading zeros.
// - For non-decimal bases, compute the number as a little-endian array of 16-bit segments by
//   packing each input digit shifted according to its bit width (log2Base).
// - Repeatedly divide the 16-bit segment array by 10, collecting remainders to produce the
//   decimal digits in reverse order.
// - Reverse the collected digits and return as a normalized base-10 string.
inline zc::String parsePseudoBigInt(zc::StringPtr stringValue) {
  size_t len = stringValue.size();
  char b1 = len > 1 ? stringValue[1] : 0;
  int log2Base = 0;
  switch (b1) {
    case 'b':
    case 'B':
      log2Base = 1;
      break;
    case 'o':
    case 'O':
      log2Base = 3;
      break;
    case 'x':
    case 'X':
      log2Base = 4;
      break;
    default: {
      // Decimal case: drop trailing 'n' if present and strip leading zeros.
      size_t nIndex = len;
      if (len > 0 && stringValue[len - 1] == 'n') { nIndex = len - 1; }
      size_t nonZeroStart = 0;
      while (nonZeroStart < nIndex && stringValue[nonZeroStart] == '0') { nonZeroStart++; }
      if (nonZeroStart >= nIndex) { return zc::str("0"); }
      return zc::heapString(stringValue.slice(nonZeroStart, nIndex));
    }
  }

  // Non-decimal case: omit prefix (0b/0o/0x) and optional trailing 'n'.
  size_t startIndex = 2;
  size_t endIndex = len;
  if (len > 0 && stringValue[len - 1] == 'n') { endIndex = len - 1; }
  size_t digitsCount = endIndex > startIndex ? (endIndex - startIndex) : 0;
  size_t bitsNeeded = digitsCount * static_cast<size_t>(log2Base);
  size_t segLen = (bitsNeeded >> 4) + ((bitsNeeded & 15) ? 1 : 0);
  if (segLen == 0) { return zc::str("0"); }

  // Accumulate value into 16-bit segments (little-endian) by shifting each digit's value.
  zc::Vector<uint16_t> segments;
  segments.resize(segLen);
  for (size_t i = 0; i < segLen; ++i) { segments[i] = 0; }

  size_t bitOffset = 0;
  for (size_t i = endIndex; i-- > startIndex; bitOffset += static_cast<size_t>(log2Base)) {
    char digitChar = stringValue[i];
    uint32_t digit = 0;
    if (digitChar <= '9') {
      digit = static_cast<uint32_t>(digitChar - '0');
    } else {
      digit = 10u + static_cast<uint32_t>(digitChar - (digitChar <= 'F' ? 'A' : 'a'));
    }
    size_t segment = bitOffset >> 4;
    uint32_t shiftedDigit = digit << (bitOffset & 15);
    segments[segment] = static_cast<uint16_t>(segments[segment] | (shiftedDigit & 0xFFFFu));
    uint32_t residual = shiftedDigit >> 16;
    if (residual && segment + 1 < segLen) {
      segments[segment + 1] = static_cast<uint16_t>(segments[segment + 1] | (residual & 0xFFFFu));
    }
  }

  // Convert segments to base-10 by repeated division by 10, collecting remainders as digits.
  zc::Vector<char> outDigits;
  size_t firstNonzeroSegment = segLen - 1;
  bool segmentsRemaining = true;
  while (segmentsRemaining) {
    uint32_t mod10 = 0;
    segmentsRemaining = false;
    for (size_t s = firstNonzeroSegment + 1; s-- > 0;) {
      uint32_t newSegment = (mod10 << 16) | segments[s];
      uint32_t segmentValue = newSegment / 10u;
      segments[s] = static_cast<uint16_t>(segmentValue & 0xFFFFu);
      mod10 = newSegment - segmentValue * 10u;
      if (segmentValue != 0 && !segmentsRemaining) {
        firstNonzeroSegment = s;
        segmentsRemaining = true;
      }
    }
    outDigits.add(static_cast<char>('0' + mod10));
  }

  // Reverse the collected digits to finalize the base-10 representation.
  if (outDigits.empty()) { return zc::str("0"); }
  zc::String result = zc::heapString(outDigits.size());
  char* buf = result.begin();
  for (size_t i = 0; i < outDigits.size(); ++i) { buf[i] = outDigits[outDigits.size() - 1 - i]; }
  return result;
}

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
