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

#include "compiler/lsp/json-parse.h"

#include "zc/core/debug.h"
#include "zc/core/encoding.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::lsp {
namespace {

// A cursor over the input bytes. All parse routines advance `pos` and fail closed
// by returning false / none without partial commitment. The recursion depth is an
// explicit parameter checked before each descent, so malicious deep nesting is
// rejected before it can overflow the native stack.
class Parser final {
public:
  Parser(zc::ArrayPtr<const uint8_t> bytes, const JsonLimits& limits)
      : bytes(bytes), limits(limits) {}

  // Parses a whole document: optional whitespace, one value, optional whitespace,
  // then end of input. Any trailing non-whitespace byte rejects.
  ZC_NODISCARD zc::Maybe<JsonValue> parseDocument() {
    skipWhitespace();
    auto value = parseValue(0);
    if (value == zc::none) { return zc::none; }
    skipWhitespace();
    if (pos != bytes.size()) { return zc::none; }  // trailing bytes
    return value;
  }

private:
  ZC_NODISCARD bool atEnd() const { return pos >= bytes.size(); }
  ZC_NODISCARD uint8_t peek() const { return bytes[pos]; }

  void skipWhitespace() {
    while (!atEnd()) {
      const uint8_t c = bytes[pos];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++pos;
      } else {
        break;
      }
    }
  }

  // Consumes a bare literal (`true`/`false`/`null`) if it matches exactly at pos.
  ZC_NODISCARD bool consumeLiteral(zc::StringPtr literal) {
    if (bytes.size() - pos < literal.size()) { return false; }
    for (size_t i = 0; i < literal.size(); ++i) {
      if (bytes[pos + i] != static_cast<uint8_t>(literal[i])) { return false; }
    }
    pos += literal.size();
    return true;
  }

  ZC_NODISCARD zc::Maybe<JsonValue> parseValue(uint32_t depth) {
    if (atEnd()) { return zc::none; }
    const uint8_t c = peek();
    switch (c) {
      case '{':
        return parseObject(depth);
      case '[':
        return parseArray(depth);
      case '"': {
        ZC_IF_SOME(str, parseString()) { return JsonValue::string(zc::mv(str)); }
        return zc::none;
      }
      case 't':
        if (consumeLiteral("true"_zc)) { return JsonValue::boolean(true); }
        return zc::none;
      case 'f':
        if (consumeLiteral("false"_zc)) { return JsonValue::boolean(false); }
        return zc::none;
      case 'n':
        if (consumeLiteral("null"_zc)) { return JsonValue::null(); }
        return zc::none;
      default:
        return parseNumber();
    }
  }

  ZC_NODISCARD zc::Maybe<JsonValue> parseArray(uint32_t depth) {
    // Check depth before descending into elements so nesting cannot overflow.
    if (depth >= limits.maxDepth) { return zc::none; }
    ++pos;  // consume '['
    skipWhitespace();
    zc::Vector<JsonValue> elements;
    if (!atEnd() && peek() == ']') {
      ++pos;
      return JsonValue::array(elements.releaseAsArray());
    }
    for (;;) {
      skipWhitespace();
      auto element = parseValue(depth + 1);
      if (element == zc::none) { return zc::none; }
      if (elements.size() >= limits.maxArrayElements) { return zc::none; }
      elements.add(zc::mv(ZC_ASSERT_NONNULL(element)));
      skipWhitespace();
      if (atEnd()) { return zc::none; }
      const uint8_t c = peek();
      if (c == ',') {
        ++pos;
        continue;
      }
      if (c == ']') {
        ++pos;
        return JsonValue::array(elements.releaseAsArray());
      }
      return zc::none;  // neither ',' nor ']'
    }
  }

  ZC_NODISCARD zc::Maybe<JsonValue> parseObject(uint32_t depth) {
    if (depth >= limits.maxDepth) { return zc::none; }
    ++pos;  // consume '{'
    skipWhitespace();
    zc::Vector<JsonMember> members;
    if (!atEnd() && peek() == '}') {
      ++pos;
      return JsonValue::object(members.releaseAsArray());
    }
    for (;;) {
      skipWhitespace();
      if (atEnd() || peek() != '"') { return zc::none; }  // key must be a string
      auto key = parseString();
      if (key == zc::none) { return zc::none; }
      zc::String keyStr = zc::mv(ZC_ASSERT_NONNULL(key));
      // Reject a duplicate key before building the member.
      for (const auto& existing : members) {
        if (existing.key == keyStr) { return zc::none; }
      }
      skipWhitespace();
      if (atEnd() || peek() != ':') { return zc::none; }
      ++pos;  // consume ':'
      skipWhitespace();
      auto value = parseValue(depth + 1);
      if (value == zc::none) { return zc::none; }
      if (members.size() >= limits.maxObjectMembers) { return zc::none; }
      members.add(JsonMember{zc::mv(keyStr), zc::mv(ZC_ASSERT_NONNULL(value))});
      skipWhitespace();
      if (atEnd()) { return zc::none; }
      const uint8_t c = peek();
      if (c == ',') {
        ++pos;
        continue;
      }
      if (c == '}') {
        ++pos;
        return JsonValue::object(members.releaseAsArray());
      }
      return zc::none;
    }
  }

  // Parses a JSON string starting at the opening quote. Emits decoded UTF-8 bytes,
  // resolving `\uXXXX` escapes (including surrogate pairs) and rejecting lone or
  // malformed surrogates, unknown escapes, unescaped control characters, and
  // malformed UTF-8 in the raw span.
  ZC_NODISCARD zc::Maybe<zc::String> parseString() {
    ++pos;  // consume opening '"'
    zc::Vector<char> out;
    for (;;) {
      if (atEnd()) { return zc::none; }  // unterminated
      if (out.size() > limits.maxStringBytes) { return zc::none; }
      const uint8_t c = bytes[pos];
      if (c == '"') {
        ++pos;
        break;
      }
      if (c == '\\') {
        ++pos;
        if (!parseEscape(out)) { return zc::none; }
        continue;
      }
      if (c < 0x20) { return zc::none; }  // unescaped control character
      // A raw byte: copy it. UTF-8 validity of the whole decoded string is checked
      // once at the end.
      out.add(static_cast<char>(c));
      ++pos;
    }
    // Validate the decoded bytes are well-formed UTF-8 (encodeUtf16 flags any
    // ill-formed input, including unpaired surrogates round-tripped from escapes).
    auto view = zc::arrayPtr(out.begin(), out.size());
    if (zc::encodeUtf16(view).hadErrors) { return zc::none; }
    return zc::heapString(view);
  }

  // Handles the character after a backslash, appending decoded UTF-8 to `out`.
  ZC_NODISCARD bool parseEscape(zc::Vector<char>& out) {
    if (atEnd()) { return false; }
    const uint8_t e = bytes[pos];
    ++pos;
    switch (e) {
      case '"':
        out.add('"');
        return true;
      case '\\':
        out.add('\\');
        return true;
      case '/':
        out.add('/');
        return true;
      case 'b':
        out.add('\b');
        return true;
      case 'f':
        out.add('\f');
        return true;
      case 'n':
        out.add('\n');
        return true;
      case 'r':
        out.add('\r');
        return true;
      case 't':
        out.add('\t');
        return true;
      case 'u':
        return parseUnicodeEscape(out);
      default:
        return false;  // unknown escape
    }
  }

  // Reads exactly four hex digits into a code unit.
  ZC_NODISCARD bool readHex4(uint32_t& codeUnit) {
    if (bytes.size() - pos < 4) { return false; }
    uint32_t value = 0;
    for (size_t i = 0; i < 4; ++i) {
      const uint8_t d = bytes[pos + i];
      value <<= 4;
      if (d >= '0' && d <= '9') {
        value |= static_cast<uint32_t>(d - '0');
      } else if (d >= 'a' && d <= 'f') {
        value |= static_cast<uint32_t>(d - 'a' + 10);
      } else if (d >= 'A' && d <= 'F') {
        value |= static_cast<uint32_t>(d - 'A' + 10);
      } else {
        return false;  // not a hex digit
      }
    }
    pos += 4;
    codeUnit = value;
    return true;
  }

  // Handles `\uXXXX`, joining a high+low surrogate pair into one scalar. A lone or
  // reversed surrogate is rejected.
  ZC_NODISCARD bool parseUnicodeEscape(zc::Vector<char>& out) {
    uint32_t unit = 0;
    if (!readHex4(unit)) { return false; }
    uint32_t scalar;
    if (unit >= 0xD800 && unit <= 0xDBFF) {
      // High surrogate: must be followed by `\uXXXX` naming a low surrogate.
      if (bytes.size() - pos < 2 || bytes[pos] != '\\' || bytes[pos + 1] != 'u') { return false; }
      pos += 2;
      uint32_t low = 0;
      if (!readHex4(low)) { return false; }
      if (low < 0xDC00 || low > 0xDFFF) { return false; }  // not a low surrogate
      scalar = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
    } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
      return false;  // lone low surrogate
    } else {
      scalar = unit;
    }
    appendUtf8(out, scalar);
    return true;
  }

  // Encodes one Unicode scalar as UTF-8 into `out`. `scalar` is guaranteed to be a
  // valid scalar value (not a surrogate) by the caller.
  static void appendUtf8(zc::Vector<char>& out, uint32_t scalar) {
    if (scalar <= 0x7F) {
      out.add(static_cast<char>(scalar));
    } else if (scalar <= 0x7FF) {
      out.add(static_cast<char>(0xC0 | (scalar >> 6)));
      out.add(static_cast<char>(0x80 | (scalar & 0x3F)));
    } else if (scalar <= 0xFFFF) {
      out.add(static_cast<char>(0xE0 | (scalar >> 12)));
      out.add(static_cast<char>(0x80 | ((scalar >> 6) & 0x3F)));
      out.add(static_cast<char>(0x80 | (scalar & 0x3F)));
    } else {
      out.add(static_cast<char>(0xF0 | (scalar >> 18)));
      out.add(static_cast<char>(0x80 | ((scalar >> 12) & 0x3F)));
      out.add(static_cast<char>(0x80 | ((scalar >> 6) & 0x3F)));
      out.add(static_cast<char>(0x80 | (scalar & 0x3F)));
    }
  }

  // Parses a JSON number per the grammar: optional '-', integer part with no
  // leading zeros, optional fraction, optional exponent. The matched span is then
  // parsed to a double and rejected unless finite.
  ZC_NODISCARD zc::Maybe<JsonValue> parseNumber() {
    const size_t start = pos;
    if (!atEnd() && peek() == '-') { ++pos; }
    // Integer part.
    if (atEnd()) { return zc::none; }
    if (peek() == '0') {
      ++pos;  // a leading zero must stand alone
    } else if (peek() >= '1' && peek() <= '9') {
      while (!atEnd() && peek() >= '0' && peek() <= '9') { ++pos; }
    } else {
      return zc::none;  // no digits
    }
    // Fraction.
    if (!atEnd() && peek() == '.') {
      ++pos;
      if (atEnd() || peek() < '0' || peek() > '9') { return zc::none; }
      while (!atEnd() && peek() >= '0' && peek() <= '9') { ++pos; }
    }
    // Exponent.
    if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
      ++pos;
      if (!atEnd() && (peek() == '+' || peek() == '-')) { ++pos; }
      if (atEnd() || peek() < '0' || peek() > '9') { return zc::none; }
      while (!atEnd() && peek() >= '0' && peek() <= '9') { ++pos; }
    }
    auto span = zc::arrayPtr(reinterpret_cast<const char*>(bytes.begin() + start), pos - start);
    auto text = zc::heapString(span);
    ZC_IF_SOME(value, text.asPtr().tryParseAs<double>()) {
      // JsonValue::number is the single fail-closed authority: it rejects a value
      // that overflowed to +/-Infinity (or any non-finite result).
      return JsonValue::number(value);
    }
    return zc::none;
  }

  zc::ArrayPtr<const uint8_t> bytes;
  const JsonLimits& limits;
  size_t pos = 0;
};

}  // namespace

zc::Maybe<JsonValue> parseJson(zc::ArrayPtr<const uint8_t> bytes, const JsonLimits& limits) {
  if (bytes.size() > limits.maxInputBytes) { return zc::none; }
  Parser parser(bytes, limits);
  return parser.parseDocument();
}

}  // namespace zomlang::compiler::lsp
