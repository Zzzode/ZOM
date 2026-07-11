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

#include "zomlang/compiler/identity/canonical-url.h"

#include "zc/core/vector.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/unicode-normalization.h"

namespace zomlang::compiler::identity {
namespace {

class TextBuilder final {
public:
  void add(char value) { bytes.add(value); }

  void add(zc::StringPtr value) { bytes.addAll(value); }

  ZC_NODISCARD size_t size() const noexcept { return bytes.size(); }

  ZC_NODISCARD char back() const { return bytes.back(); }

  ZC_NODISCARD zc::String finish() const {
    auto result = zc::heapString(bytes.size());
    for (size_t index = 0; index < bytes.size(); ++index) { result[index] = bytes[index]; }
    return result;
  }

private:
  zc::Vector<char> bytes;
};

bool isAsciiLetter(char value) {
  return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

bool isAsciiDigit(char value) { return value >= '0' && value <= '9'; }

bool isAsciiHex(char value) {
  return isAsciiDigit(value) || (value >= 'a' && value <= 'f') ||
         (value >= 'A' && value <= 'F');
}

uint8_t hexValue(char value) {
  if (isAsciiDigit(value)) { return static_cast<uint8_t>(value - '0'); }
  if (value >= 'a' && value <= 'f') { return static_cast<uint8_t>(value - 'a' + 10); }
  return static_cast<uint8_t>(value - 'A' + 10);
}

char lowerAscii(char value) {
  if (value >= 'A' && value <= 'Z') { return static_cast<char>(value - 'A' + 'a'); }
  return value;
}

bool isUnreserved(uint8_t value) {
  return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
         (value >= '0' && value <= '9') || value == '-' || value == '.' || value == '_' ||
         value == '~';
}

char upperHex(uint8_t value) {
  return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('A' + value - 10);
}

void addPercentEncoded(TextBuilder& output, uint8_t value) {
  output.add('%');
  output.add(upperHex(static_cast<uint8_t>(value >> 4)));
  output.add(upperHex(static_cast<uint8_t>(value & 0x0f)));
}

void addDecimal(TextBuilder& output, uint32_t value) {
  char digits[10];
  size_t count = 0;
  do {
    digits[count++] = static_cast<char>('0' + value % 10);
    value /= 10;
  } while (value != 0);
  while (count > 0) { output.add(digits[--count]); }
}

void addHexWord(TextBuilder& output, uint16_t value) {
  char digits[4];
  size_t count = 0;
  do {
    const uint8_t digit = static_cast<uint8_t>(value & 0x0f);
    digits[count++] = digit < 10 ? static_cast<char>('0' + digit)
                                 : static_cast<char>('a' + digit - 10);
    value = static_cast<uint16_t>(value >> 4);
  } while (value != 0);
  while (count > 0) { output.add(digits[--count]); }
}

zc::Maybe<uint32_t> parseDecimal(zc::StringPtr input, size_t start, size_t end,
                                 uint32_t maximum, bool rejectLeadingZero) {
  if (start == end || (rejectLeadingZero && end - start > 1 && input[start] == '0')) {
    return zc::none;
  }
  uint32_t value = 0;
  for (size_t index = start; index < end; ++index) {
    if (!isAsciiDigit(input[index])) { return zc::none; }
    const uint32_t digit = static_cast<uint32_t>(input[index] - '0');
    if (value > (maximum - digit) / 10) { return zc::none; }
    value = value * 10 + digit;
  }
  return value;
}

bool parseIpv4(zc::StringPtr input, size_t start, size_t end, uint8_t (&octets)[4]) {
  size_t componentStart = start;
  size_t component = 0;
  for (size_t index = start; index <= end; ++index) {
    if (index != end && input[index] != '.') { continue; }
    if (component >= 4) { return false; }
    auto value = parseDecimal(input, componentStart, index, 255, false);
    ZC_IF_SOME(parsed, value) { octets[component++] = static_cast<uint8_t>(parsed); }
    else {
      return false;
    }
    componentStart = index + 1;
  }
  return component == 4;
}

bool isIpv4Candidate(zc::StringPtr input, size_t start, size_t end) {
  size_t dots = 0;
  for (size_t index = start; index < end; ++index) {
    if (input[index] == '.') {
      ++dots;
    } else if (!isAsciiDigit(input[index])) {
      return false;
    }
  }
  return dots == 3;
}

bool parseHexWord(zc::StringPtr input, size_t start, size_t end, uint16_t& result) {
  if (start == end || end - start > 4) { return false; }
  uint16_t value = 0;
  for (size_t index = start; index < end; ++index) {
    if (!isAsciiHex(input[index])) { return false; }
    value = static_cast<uint16_t>((value << 4) | hexValue(input[index]));
  }
  result = value;
  return true;
}

bool parseIpv6Side(zc::StringPtr input, size_t start, size_t end,
                   zc::Vector<uint16_t>& words, bool finalSide) {
  if (start == end) { return true; }
  if (input[start] == ':' || input[end - 1] == ':') { return false; }
  size_t cursor = start;
  while (cursor < end) {
    size_t componentEnd = cursor;
    while (componentEnd < end && input[componentEnd] != ':') { ++componentEnd; }
    if (componentEnd == cursor) { return false; }

    bool containsDot = false;
    for (size_t index = cursor; index < componentEnd; ++index) {
      if (input[index] == '.') {
        containsDot = true;
        break;
      }
    }
    if (containsDot) {
      if (!finalSide || componentEnd != end) { return false; }
      uint8_t octets[4];
      if (!parseIpv4(input, cursor, componentEnd, octets)) { return false; }
      words.add(static_cast<uint16_t>((static_cast<uint16_t>(octets[0]) << 8) | octets[1]));
      words.add(static_cast<uint16_t>((static_cast<uint16_t>(octets[2]) << 8) | octets[3]));
    } else {
      uint16_t word = 0;
      if (!parseHexWord(input, cursor, componentEnd, word)) { return false; }
      words.add(word);
    }
    if (words.size() > 8) { return false; }
    cursor = componentEnd + 1;
  }
  return true;
}

bool parseIpv6(zc::StringPtr input, zc::Vector<uint16_t>& result) {
  if (input.size() == 0) { return false; }
  zc::Maybe<size_t> compression;
  for (size_t index = 0; index + 1 < input.size(); ++index) {
    if (input[index] == ':' && input[index + 1] == ':') {
      if (compression != zc::none) { return false; }
      compression = index;
      ++index;
    }
  }

  if (compression == zc::none) {
    if (!parseIpv6Side(input, 0, input.size(), result, true)) { return false; }
    return result.size() == 8;
  }

  size_t split = 0;
  ZC_IF_SOME(value, compression) { split = value; }
  zc::Vector<uint16_t> left;
  zc::Vector<uint16_t> right;
  if (!parseIpv6Side(input, 0, split, left, false) ||
      !parseIpv6Side(input, split + 2, input.size(), right, true) ||
      left.size() + right.size() >= 8) {
    return false;
  }
  for (uint16_t word : left) { result.add(word); }
  for (size_t count = left.size() + right.size(); count < 8; ++count) { result.add(0); }
  for (uint16_t word : right) { result.add(word); }
  return true;
}

zc::String renderIpv6(zc::ArrayPtr<const uint16_t> words) {
  size_t bestStart = words.size();
  size_t bestLength = 0;
  for (size_t index = 0; index < words.size();) {
    if (words[index] != 0) {
      ++index;
      continue;
    }
    const size_t start = index;
    while (index < words.size() && words[index] == 0) { ++index; }
    const size_t length = index - start;
    if (length >= 2 && length > bestLength) {
      bestStart = start;
      bestLength = length;
    }
  }

  TextBuilder output;
  for (size_t index = 0; index < words.size();) {
    if (index == bestStart) {
      output.add(':');
      output.add(':');
      index += bestLength;
      continue;
    }
    if (output.size() != 0 && output.back() != ':') { output.add(':'); }
    addHexWord(output, words[index++]);
  }
  return output.finish();
}

bool validateDnsHost(zc::StringPtr input, size_t end) {
  if (end == 0 || end > 253) { return false; }
  size_t labelStart = 0;
  for (size_t index = 0; index <= end; ++index) {
    if (index != end && input[index] != '.') { continue; }
    const size_t length = index - labelStart;
    if (length == 0 || length > 63 ||
        (!isAsciiLetter(input[labelStart]) && !isAsciiDigit(input[labelStart])) ||
        (!isAsciiLetter(input[index - 1]) && !isAsciiDigit(input[index - 1]))) {
      return false;
    }
    for (size_t part = labelStart; part < index; ++part) {
      if (!isAsciiLetter(input[part]) && !isAsciiDigit(input[part]) && input[part] != '-') {
        return false;
      }
    }
    labelStart = index + 1;
  }
  return true;
}

zc::Maybe<zc::String> canonicalizeHost(zc::StringPtr host) {
  if (host.size() == 0) { return zc::none; }
  if (host[0] == '[') {
    if (host.size() < 3 || host[host.size() - 1] != ']') { return zc::none; }
    auto inner = zc::heapString(host.slice(1, host.size() - 1));
    zc::Vector<uint16_t> words;
    if (!parseIpv6(inner, words)) { return zc::none; }
    auto rendered = renderIpv6(words.asPtr());
    TextBuilder output;
    output.add('[');
    output.add(rendered);
    output.add(']');
    return output.finish();
  }

  size_t effectiveEnd = host.size();
  if (host[effectiveEnd - 1] == '.') { --effectiveEnd; }
  if (effectiveEnd == 0) { return zc::none; }
  for (size_t index = 0; index < effectiveEnd; ++index) {
    if (static_cast<uint8_t>(host[index]) >= 0x80) { return zc::none; }
  }

  if (isIpv4Candidate(host, 0, effectiveEnd)) {
    uint8_t octets[4];
    if (!parseIpv4(host, 0, effectiveEnd, octets)) { return zc::none; }
    TextBuilder output;
    for (size_t index = 0; index < 4; ++index) {
      if (index != 0) { output.add('.'); }
      addDecimal(output, octets[index]);
    }
    return output.finish();
  }

  if (!validateDnsHost(host, effectiveEnd)) { return zc::none; }
  auto result = zc::heapString(effectiveEnd);
  for (size_t index = 0; index < effectiveEnd; ++index) {
    result[index] = lowerAscii(host[index]);
  }
  return result;
}

bool flushUnicodeText(zc::Vector<char>& pending, TextBuilder& output) {
  if (pending.size() == 0) { return true; }
  auto input = zc::heapString(pending.asPtr());
  auto normalized = normalizeNfc(input);
  ZC_IF_SOME(value, normalized) {
    for (uint8_t current : value.asBytes()) {
      if (isUnreserved(current)) {
        output.add(static_cast<char>(current));
      } else {
        addPercentEncoded(output, current);
      }
    }
    pending.clear();
    return true;
  }
  return false;
}

zc::Maybe<zc::String> canonicalizeSegment(zc::StringPtr input) {
  TextBuilder output;
  zc::Vector<char> pending;
  for (size_t index = 0; index < input.size(); ++index) {
    const uint8_t current = static_cast<uint8_t>(input[index]);
    if (current >= 0x80 || current < 0x21 || current > 0x7e) { return zc::none; }
    if (current == '%') {
      if (index + 2 >= input.size() || !isAsciiHex(input[index + 1]) ||
          !isAsciiHex(input[index + 2])) {
        return zc::none;
      }
      const uint8_t decoded = static_cast<uint8_t>((hexValue(input[index + 1]) << 4) |
                                                   hexValue(input[index + 2]));
      index += 2;
      if (decoded < 0x80 && !isUnreserved(decoded)) {
        if (!flushUnicodeText(pending, output)) { return zc::none; }
        addPercentEncoded(output, decoded);
      } else {
        pending.add(static_cast<char>(decoded));
      }
    } else if (isUnreserved(current)) {
      pending.add(static_cast<char>(current));
    } else {
      if (!flushUnicodeText(pending, output)) { return zc::none; }
      addPercentEncoded(output, current);
    }
  }
  if (!flushUnicodeText(pending, output)) { return zc::none; }
  return output.finish();
}

zc::Maybe<zc::String> canonicalizePath(zc::StringPtr input, size_t pathStart) {
  zc::Vector<zc::String> segments;
  bool trailingSlash = false;
  if (pathStart < input.size()) {
    if (input[pathStart] != '/') { return zc::none; }
    size_t segmentStart = pathStart + 1;
    for (size_t index = segmentStart; index <= input.size(); ++index) {
      if (index != input.size() && input[index] != '/') { continue; }
      auto raw = zc::heapString(input.slice(segmentStart, index));
      auto normalized = canonicalizeSegment(raw);
      bool admitted = false;
      ZC_IF_SOME(value, normalized) {
        if (value == "."_zc) {
          if (index == input.size()) { trailingSlash = true; }
        } else if (value == ".."_zc) {
          if (segments.size() != 0) { segments.removeLast(); }
          if (index == input.size()) { trailingSlash = true; }
        } else if (value.size() == 0 && index == input.size()) {
          trailingSlash = true;
        } else {
          segments.add(zc::mv(value));
        }
        admitted = true;
      }
      if (!admitted) { return zc::none; }
      segmentStart = index + 1;
    }
  }

  TextBuilder output;
  output.add('/');
  for (size_t index = 0; index < segments.size(); ++index) {
    if (index != 0) { output.add('/'); }
    output.add(segments[index]);
  }
  if (trailingSlash && segments.size() != 0) { output.add('/'); }
  return output.finish();
}

zc::Maybe<zc::String> canonicalizeUrl(zc::StringPtr input) {
  for (char current : input) {
    if (current == '?' || current == '#' || current == '\0') { return zc::none; }
  }

  size_t schemeEnd = 0;
  while (schemeEnd < input.size() && input[schemeEnd] != ':') { ++schemeEnd; }
  if (schemeEnd == input.size() || schemeEnd + 2 >= input.size() ||
      input[schemeEnd + 1] != '/' || input[schemeEnd + 2] != '/') {
    return zc::none;
  }
  auto schemeInput = zc::heapString(input.slice(0, schemeEnd));
  for (size_t index = 0; index < schemeInput.size(); ++index) {
    schemeInput[index] = lowerAscii(schemeInput[index]);
  }
  const bool isHttps = schemeInput == "https"_zc;
  const bool isSsh = schemeInput == "ssh"_zc;
  if (!isHttps && !isSsh) { return zc::none; }

  const size_t authorityStart = schemeEnd + 3;
  size_t authorityEnd = authorityStart;
  while (authorityEnd < input.size() && input[authorityEnd] != '/') { ++authorityEnd; }
  if (authorityStart == authorityEnd) { return zc::none; }
  for (size_t index = authorityStart; index < authorityEnd; ++index) {
    if (input[index] == '@') { return zc::none; }
  }

  size_t hostEnd = authorityEnd;
  zc::Maybe<uint32_t> port;
  if (input[authorityStart] == '[') {
    size_t close = authorityStart + 1;
    while (close < authorityEnd && input[close] != ']') { ++close; }
    if (close == authorityEnd) { return zc::none; }
    hostEnd = close + 1;
    if (hostEnd < authorityEnd) {
      if (input[hostEnd] != ':') { return zc::none; }
      port = parseDecimal(input, hostEnd + 1, authorityEnd, 65535, true);
      if (port == zc::none) { return zc::none; }
    }
  } else {
    size_t colon = authorityEnd;
    for (size_t index = authorityStart; index < authorityEnd; ++index) {
      if (input[index] == ':') {
        if (colon != authorityEnd) { return zc::none; }
        colon = index;
      }
    }
    if (colon != authorityEnd) {
      hostEnd = colon;
      port = parseDecimal(input, colon + 1, authorityEnd, 65535, true);
      if (port == zc::none) { return zc::none; }
    }
  }

  auto hostInput = zc::heapString(input.slice(authorityStart, hostEnd));
  auto canonicalHost = canonicalizeHost(hostInput);
  auto canonicalPath = canonicalizePath(input, authorityEnd);
  if (canonicalHost == zc::none || canonicalPath == zc::none) { return zc::none; }

  uint32_t portValue = 0;
  bool retainPort = false;
  ZC_IF_SOME(value, port) {
    if (value == 0) { return zc::none; }
    portValue = value;
    retainPort = (isHttps && value != 443) || (isSsh && value != 22);
  }

  TextBuilder output;
  output.add(schemeInput);
  output.add("://"_zc);
  ZC_IF_SOME(value, canonicalHost) { output.add(value); }
  if (retainPort) {
    output.add(':');
    addDecimal(output, portValue);
  }
  ZC_IF_SOME(value, canonicalPath) { output.add(value); }
  return output.finish();
}

}  // namespace

CanonicalUrl::CanonicalUrl(zc::String&& canonical) noexcept : value(zc::mv(canonical)) {}

zc::Maybe<CanonicalUrl> CanonicalUrl::fromSource(zc::StringPtr input) {
  ZC_IF_SOME(canonical, canonicalizeUrl(input)) { return CanonicalUrl(zc::mv(canonical)); }
  return zc::none;
}

zc::Maybe<CanonicalUrl> CanonicalUrl::fromCanonical(zc::StringPtr input) {
  ZC_IF_SOME(canonical, canonicalizeUrl(input)) {
    if (canonical != input) { return zc::none; }
    return CanonicalUrl(zc::mv(canonical));
  }
  return zc::none;
}

CanonicalUrl CanonicalUrl::clone() const { return CanonicalUrl(zc::heapString(value)); }

zc::StringPtr CanonicalUrl::text() const noexcept { return value; }

void CanonicalUrl::encode(CanonicalEncoder& encoder) const {
  encoder.encodeByteString(value.asBytes());
}

bool CanonicalUrl::operator==(const CanonicalUrl& other) const noexcept {
  return value == other.value;
}

bool CanonicalUrl::operator<(const CanonicalUrl& other) const noexcept {
  return value < other.value;
}

}  // namespace zomlang::compiler::identity
