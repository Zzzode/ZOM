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
// See the License for the specific language governing permissions and
// limitations under the License.

// RFC 0023 "IDE Semantic Snapshots" LSP transport (T1): prove the JSON layer,
// which reads with the vendored yyjson library and converts its DOM into the
// closed JsonValue model, parses well-formed documents, enforces conservative
// bounds against untrusted input (nesting depth, input/string/array/object/node
// sizes, total string bytes), rejects everything JSON forbids (duplicate keys,
// NaN/Infinity/overflow numbers, integers that lose precision as a double,
// malformed UTF-8, invalid surrogates, bad escapes, trailing bytes), and
// serializes back to compact, deterministic, order-preserving bytes that
// round-trip.

#include "compiler/lsp/json-value.h"

#include <limits>

#include "compiler/lsp/json-parse.h"
#include "compiler/lsp/json-serialize.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::lsp {
namespace {

zc::ArrayPtr<const uint8_t> bytes(zc::StringPtr text) { return text.asBytes(); }

zc::Maybe<JsonValue> parse(zc::StringPtr text) { return parseJson(bytes(text)); }

zc::String serializeText(const JsonValue& value) {
  auto out = serializeJson(value);
  return zc::heapString(zc::arrayPtr(reinterpret_cast<const char*>(out.begin()), out.size()));
}

ZC_TEST("parseJson accepts the JSON scalar literals") {
  ZC_EXPECT(ZC_ASSERT_NONNULL(parse("null"_zc)).isNull());
  ZC_EXPECT(ZC_ASSERT_NONNULL(parse("true"_zc)).asBool() == true);
  ZC_EXPECT(ZC_ASSERT_NONNULL(parse("false"_zc)).asBool() == false);
  ZC_EXPECT(ZC_ASSERT_NONNULL(parse("  \n\t 42 "_zc)).asNumber() == 42.0);
  ZC_EXPECT(ZC_ASSERT_NONNULL(parse("-1.5e3"_zc)).asNumber() == -1500.0);
  ZC_EXPECT(ZC_ASSERT_NONNULL(parse("0"_zc)).asNumber() == 0.0);
  ZC_EXPECT(ZC_ASSERT_NONNULL(parse("\"hi\""_zc)).asString() == "hi"_zc);
}

ZC_TEST("parseJson builds arrays and objects in document order") {
  auto array = parse("[1, 2, 3]"_zc);
  ZC_REQUIRE(array != zc::none);
  ZC_REQUIRE(ZC_ASSERT_NONNULL(array).isArray());
  ZC_EXPECT(ZC_ASSERT_NONNULL(array).asArray().size() == 3);
  ZC_EXPECT(ZC_ASSERT_NONNULL(array).asArray()[1].asNumber() == 2.0);

  auto object = parse("{\"b\": 1, \"a\": 2}"_zc);
  ZC_REQUIRE(object != zc::none);
  ZC_REQUIRE(ZC_ASSERT_NONNULL(object).isObject());
  auto members = ZC_ASSERT_NONNULL(object).asObject();
  ZC_REQUIRE(members.size() == 2);
  // Order preserved: "b" before "a".
  ZC_EXPECT(members[0].key == "b"_zc);
  ZC_EXPECT(members[1].key == "a"_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(ZC_ASSERT_NONNULL(object).find("a"_zc)).asNumber() == 2.0);
  ZC_EXPECT(ZC_ASSERT_NONNULL(object).find("missing"_zc) == zc::none);
}

ZC_TEST("parseJson decodes string escapes and unicode surrogate pairs") {
  ZC_EXPECT(ZC_ASSERT_NONNULL(parse("\"a\\nb\\t\\\"\\\\\""_zc)).asString() == "a\nb\t\"\\"_zc);
  // \u0041 == 'A'.
  ZC_EXPECT(ZC_ASSERT_NONNULL(parse("\"\\u0041\""_zc)).asString() == "A"_zc);
  // Surrogate pair for U+1F600 (grinning face) -> 4-byte UTF-8 F0 9F 98 80.
  auto emoji = parse("\"\\uD83D\\uDE00\""_zc);
  ZC_REQUIRE(emoji != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(emoji).asString() == "\xF0\x9F\x98\x80"_zc);
}

ZC_TEST("parseJson round-trips through serializeJson deterministically") {
  const zc::StringPtr inputs[] = {
      "null"_zc,
      "true"_zc,
      "[1,2,3]"_zc,
      "{\"b\":1,\"a\":[true,null,\"x\"]}"_zc,
      "\"line\\nbreak\""_zc,
  };
  for (const auto& text : inputs) {
    auto value = parse(text);
    ZC_REQUIRE(value != zc::none, text);
    auto once = serializeText(ZC_ASSERT_NONNULL(value));
    // Re-parse and re-serialize: the second serialization is byte-identical.
    auto reparsed = parse(once);
    ZC_REQUIRE(reparsed != zc::none, text);
    auto twice = serializeText(ZC_ASSERT_NONNULL(reparsed));
    ZC_EXPECT(once == twice, text);
  }
  // Object key order is preserved verbatim through a round trip.
  auto object = parse("{\"b\":1,\"a\":2}"_zc);
  ZC_EXPECT(serializeText(ZC_ASSERT_NONNULL(object)) == "{\"b\":1,\"a\":2}"_zc);
}

ZC_TEST("parseJson rejects malformed structure and trailing bytes") {
  const zc::StringPtr bad[] = {
      ""_zc,          // empty
      "   "_zc,       // whitespace only
      "["_zc,         // unterminated array
      "{"_zc,         // unterminated object
      "[1,]"_zc,      // trailing comma
      "[1 2]"_zc,     // missing comma
      "{\"a\"}"_zc,   // missing colon+value
      "{\"a\":}"_zc,  // missing value
      "{a:1}"_zc,     // unquoted key
      "nul"_zc,       // truncated literal
      "truee"_zc,     // trailing bytes after literal
      "1 2"_zc,       // two top-level values
      "[1] "_zc,      // (valid; sanity below flips this)
  };
  for (size_t i = 0; i + 1 < (sizeof(bad) / sizeof(bad[0])); ++i) {
    ZC_EXPECT(parse(bad[i]) == zc::none, bad[i]);
  }
  // The last entry has valid trailing whitespace and must parse.
  ZC_EXPECT(parse("[1] "_zc) != zc::none);
}

ZC_TEST("parseJson rejects invalid numbers including NaN, Infinity, and overflow") {
  const zc::StringPtr bad[] = {
      "01"_zc,        // leading zero
      "1."_zc,        // fraction with no digits
      ".5"_zc,        // no integer part
      "1e"_zc,        // exponent with no digits
      "-"_zc,         // lone minus
      "+1"_zc,        // leading plus
      "NaN"_zc,       // not a JSON literal
      "Infinity"_zc,  // not a JSON literal
      "-Infinity"_zc,
      "1e400"_zc,  // overflows a double to +Infinity
  };
  for (const auto& text : bad) { ZC_EXPECT(parse(text) == zc::none, text); }
}

ZC_TEST("parseJson rejects duplicate object keys") {
  ZC_EXPECT(parse("{\"a\":1,\"a\":2}"_zc) == zc::none);
  // Distinct keys with the same value are fine.
  ZC_EXPECT(parse("{\"a\":1,\"b\":1}"_zc) != zc::none);
}

ZC_TEST("parseJson rejects bad escapes, control characters, and invalid surrogates") {
  const zc::StringPtr bad[] = {
      "\"\\x\""_zc,         // unknown escape
      "\"\\u12\""_zc,       // short unicode escape
      "\"\\uZZZZ\""_zc,     // non-hex unicode escape
      "\"\\uD83D\""_zc,     // lone high surrogate
      "\"\\uDE00\""_zc,     // lone low surrogate
      "\"\\uD83Dx\""_zc,    // high surrogate not followed by \u
      "\"unterminated"_zc,  // no closing quote
  };
  for (const auto& text : bad) { ZC_EXPECT(parse(text) == zc::none, text); }
  // A literal newline (control char) inside a string is rejected.
  const uint8_t rawControl[] = {'"', '\n', '"'};
  ZC_EXPECT(parseJson(zc::arrayPtr(rawControl, sizeof(rawControl))) == zc::none);
  // Malformed UTF-8 (a lone continuation byte) inside a string is rejected.
  const uint8_t rawUtf8[] = {'"', 0x80, '"'};
  ZC_EXPECT(parseJson(zc::arrayPtr(rawUtf8, sizeof(rawUtf8))) == zc::none);
}

ZC_TEST("parseJson enforces the semantic nesting depth bound") {
  JsonLimits limits;
  limits.maxDepth = 8;
  // Depth exactly at the bound parses; one deeper is rejected. yyjson's reader is
  // iterative and does not overflow the stack, so this is a semantic policy bound
  // enforced during conversion, not a crash guard.
  auto atBound = [&](uint32_t depth) {
    zc::Vector<char> text;
    for (uint32_t i = 0; i < depth; ++i) { text.add('['); }
    for (uint32_t i = 0; i < depth; ++i) { text.add(']'); }
    auto view = zc::arrayPtr(reinterpret_cast<const uint8_t*>(text.begin()), text.size());
    return parseJson(view, limits);
  };
  ZC_EXPECT(atBound(8) != zc::none);
  ZC_EXPECT(atBound(9) == zc::none);

  // A pathological deep input far beyond the bound is rejected, not a crash.
  zc::Vector<char> deep;
  for (uint32_t i = 0; i < 100000; ++i) { deep.add('['); }
  auto view = zc::arrayPtr(reinterpret_cast<const uint8_t*>(deep.begin()), deep.size());
  ZC_EXPECT(parseJson(view, limits) == zc::none);
}

ZC_TEST("parseJson enforces the input, array, object, and string size bounds") {
  {
    JsonLimits limits;
    limits.maxInputBytes = 3;
    ZC_EXPECT(parseJson(bytes("[1]"_zc), limits) != zc::none);
    ZC_EXPECT(parseJson(bytes("[10]"_zc), limits) == zc::none);  // 4 bytes > 3
  }
  {
    JsonLimits limits;
    limits.maxArrayElements = 2;
    ZC_EXPECT(parseJson(bytes("[1,2]"_zc), limits) != zc::none);
    ZC_EXPECT(parseJson(bytes("[1,2,3]"_zc), limits) == zc::none);
  }
  {
    JsonLimits limits;
    limits.maxObjectMembers = 1;
    ZC_EXPECT(parseJson(bytes("{\"a\":1}"_zc), limits) != zc::none);
    ZC_EXPECT(parseJson(bytes("{\"a\":1,\"b\":2}"_zc), limits) == zc::none);
  }
  {
    JsonLimits limits;
    limits.maxStringBytes = 2;
    ZC_EXPECT(parseJson(bytes("\"ab\""_zc), limits) != zc::none);
    ZC_EXPECT(parseJson(bytes("\"abc\""_zc), limits) == zc::none);
  }
}

ZC_TEST("serializeJson escapes strings and emits compact output") {
  auto value = JsonValue::string(zc::heapString("a\"b\\c\nd"));
  ZC_EXPECT(serializeText(value) == "\"a\\\"b\\\\c\\nd\""_zc);

  zc::Vector<JsonValue> elements;
  auto one = JsonValue::number(1.0);
  elements.add(zc::mv(ZC_ASSERT_NONNULL(one)));
  elements.add(JsonValue::null());
  ZC_EXPECT(serializeText(JsonValue::array(elements.releaseAsArray())) == "[1,null]"_zc);
}

ZC_TEST("JsonValue::number fails closed on non-finite values") {
  // NaN and the infinities are not JSON; construction returns none rather than a
  // value that would later have to be silently rewritten.
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  ZC_EXPECT(JsonValue::number(nan) == zc::none);
  ZC_EXPECT(JsonValue::number(inf) == zc::none);
  ZC_EXPECT(JsonValue::number(-inf) == zc::none);
  // A finite value constructs and serializes normally.
  ZC_EXPECT(JsonValue::number(0.0) != zc::none);
  auto negative = JsonValue::number(-2.5);
  ZC_EXPECT(serializeText(zc::mv(ZC_ASSERT_NONNULL(negative))) == "-2.5"_zc);
}

ZC_TEST("JsonValue clone deep-copies a nested value") {
  auto value = ZC_ASSERT_NONNULL(parse("{\"a\":[1,{\"b\":true}]}"_zc));
  auto copy = value.clone();
  ZC_EXPECT(serializeText(copy) == serializeText(value));
}

ZC_TEST("parseJson rejects integers that lose precision as a double") {
  // 2^53 is the largest integer a double represents exactly; it is accepted.
  ZC_EXPECT(parse("9007199254740992"_zc) != zc::none);   // 2^53
  ZC_EXPECT(parse("-9007199254740992"_zc) != zc::none);  // -2^53
  // 2^53 + 1 cannot round-trip through a double, so it is rejected rather than
  // silently rounded (matters for JSON-RPC ids and other precise integers).
  ZC_EXPECT(parse("9007199254740993"_zc) == zc::none);   // 2^53 + 1
  ZC_EXPECT(parse("-9007199254740993"_zc) == zc::none);  // -(2^53 + 1)
  // A very large integer and INT64/UINT64-scale values are rejected.
  ZC_EXPECT(parse("9223372036854775807"_zc) == zc::none);             // 2^63 - 1
  ZC_EXPECT(parse("123456789012345678901234567890"_zc) == zc::none);  // 30 digits
  // A small integer and an exact fractional value are accepted.
  ZC_EXPECT(ZC_ASSERT_NONNULL(parse("42"_zc)).asNumber() == 42.0);
  ZC_EXPECT(ZC_ASSERT_NONNULL(parse("1.5"_zc)).asNumber() == 1.5);
}

ZC_TEST("parseJson enforces the node and total-string budgets") {
  {
    JsonLimits limits;
    limits.maxNodes = 3;
    // [1,2] is three nodes (array + two numbers): accepted.
    ZC_EXPECT(parseJson(bytes("[1,2]"_zc), limits) != zc::none);
    // [1,2,3] is four nodes: rejected.
    ZC_EXPECT(parseJson(bytes("[1,2,3]"_zc), limits) == zc::none);
  }
  {
    JsonLimits limits;
    limits.maxTotalStringBytes = 4;
    // Two 2-byte strings sum to exactly the bound: accepted.
    ZC_EXPECT(parseJson(bytes("[\"ab\",\"cd\"]"_zc), limits) != zc::none);
    // A third byte over the summed bound: rejected.
    ZC_EXPECT(parseJson(bytes("[\"ab\",\"cde\"]"_zc), limits) == zc::none);
  }
}

}  // namespace
}  // namespace zomlang::compiler::lsp
