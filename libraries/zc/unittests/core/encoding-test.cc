// Copyright (c) 2017 Cloudflare, Inc. and contributors
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "zc/core/encoding.h"

#include <stdint.h>

#include "zc/ztest/test.h"

namespace zc {
namespace {

CappedArray<char, sizeof(char) * 2 + 1> hex(byte i) { return zc::hex((uint8_t)i); }
CappedArray<char, sizeof(char) * 2 + 1> hex(char i) { return zc::hex((uint8_t)i); }
#if __cpp_char8_t
[[maybe_unused]]
CappedArray<char, sizeof(char8_t) * 2 + 1> hex(char8_t i) {
  return zc::hex((uint8_t)i);
}
#endif
CappedArray<char, sizeof(char16_t) * 2 + 1> hex(char16_t i) { return zc::hex((uint16_t)i); }
CappedArray<char, sizeof(char32_t) * 2 + 1> hex(char32_t i) { return zc::hex((uint32_t)i); }
CappedArray<char, sizeof(uint32_t) * 2 + 1> hex(wchar_t i) { return zc::hex((uint32_t)i); }
// Hexify chars correctly.
//
// TODO(cleanup): Should this go into string.h with the other definitions of hex()?

template <typename T, typename U>
void expectResImpl(EncodingResult<T> result, ArrayPtr<const U> expected, bool errors = false) {
  if (errors) {
    ZC_EXPECT(result.hadErrors);
  } else {
    ZC_EXPECT(!result.hadErrors);
  }

  ZC_EXPECT(result.size() == expected.size(), result.size(), expected.size());
  for (auto i : zc::zeroTo(zc::min(result.size(), expected.size()))) {
    ZC_EXPECT(result[i] == expected[i], i, hex(result[i]), hex(expected[i]));
  }
}

template <typename T, typename U, size_t s>
void expectRes(EncodingResult<T> result, const U (&expected)[s], bool errors = false) {
  expectResImpl(zc::mv(result), arrayPtr(expected, s - 1), errors);
}

#if __cpp_char8_t
template <typename T, size_t s>
void expectRes(EncodingResult<T> result, const char8_t (&expected)[s], bool errors = false) {
  expectResImpl(zc::mv(result), arrayPtr(reinterpret_cast<const char*>(expected), s - 1), errors);
}
#endif

template <typename T, size_t s>
void expectRes(EncodingResult<T> result, byte (&expected)[s], bool errors = false) {
  expectResImpl(zc::mv(result), arrayPtr<const byte>(expected, s), errors);
}

// Handy reference for surrogate pair edge cases:
//
// \ud800 -> \xed\xa0\x80
// \udc00 -> \xed\xb0\x80
// \udbff -> \xed\xaf\xbf
// \udfff -> \xed\xbf\xbf

ZC_TEST("encode UTF-8 to UTF-16") {
  expectRes(encodeUtf16(u8"foo"), u"foo");
  expectRes(encodeUtf16(u8"Здравствуйте"), u"Здравствуйте");
  expectRes(encodeUtf16(u8"中国网络"), u"中国网络");
  expectRes(encodeUtf16(u8"😺☁☄🐵"), u"😺☁☄🐵");
}

ZC_TEST("invalid UTF-8 to UTF-16") {
  // Disembodied continuation byte.
  expectRes(encodeUtf16("\x80"), u"\ufffd", true);
  expectRes(encodeUtf16("f\xbfo"), u"f\ufffdo", true);
  expectRes(encodeUtf16("f\xbf\x80\xb0o"), u"f\ufffdo", true);

  // Missing continuation bytes.
  expectRes(encodeUtf16("\xc2x"), u"\ufffdx", true);
  expectRes(encodeUtf16("\xe0x"), u"\ufffdx", true);
  expectRes(encodeUtf16("\xe0\xa0x"), u"\ufffdx", true);
  expectRes(encodeUtf16("\xf0x"), u"\ufffdx", true);
  expectRes(encodeUtf16("\xf0\x90x"), u"\ufffdx", true);
  expectRes(encodeUtf16("\xf0\x90\x80x"), u"\ufffdx", true);

  // Overlong sequences.
  expectRes(encodeUtf16("\xc0\x80"), u"\ufffd", true);
  expectRes(encodeUtf16("\xc1\xbf"), u"\ufffd", true);
  expectRes(encodeUtf16("\xc2\x80"), u"\u0080", false);
  expectRes(encodeUtf16("\xdf\xbf"), u"\u07ff", false);

  expectRes(encodeUtf16("\xe0\x80\x80"), u"\ufffd", true);
  expectRes(encodeUtf16("\xe0\x9f\xbf"), u"\ufffd", true);
  expectRes(encodeUtf16("\xe0\xa0\x80"), u"\u0800", false);
  expectRes(encodeUtf16("\xef\xbf\xbe"), u"\ufffe", false);

  // Due to a classic off-by-one error, GCC 4.x rather hilariously encodes '\uffff' as the
  // "surrogate pair" 0xd7ff, 0xdfff: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=41698
  if (zc::size(u"\uffff") == 2) { expectRes(encodeUtf16("\xef\xbf\xbf"), u"\uffff", false); }

  expectRes(encodeUtf16("\xf0\x80\x80\x80"), u"\ufffd", true);
  expectRes(encodeUtf16("\xf0\x8f\xbf\xbf"), u"\ufffd", true);
  expectRes(encodeUtf16("\xf0\x90\x80\x80"), u"\U00010000", false);
  expectRes(encodeUtf16("\xf4\x8f\xbf\xbf"), u"\U0010ffff", false);

  // Out of Unicode range.
  expectRes(encodeUtf16("\xf5\x80\x80\x80"), u"\ufffd", true);
  expectRes(encodeUtf16("\xf8\xbf\x80\x80\x80"), u"\ufffd", true);
  expectRes(encodeUtf16("\xfc\xbf\x80\x80\x80\x80"), u"\ufffd", true);
  expectRes(encodeUtf16("\xfe\xbf\x80\x80\x80\x80\x80"), u"\ufffd", true);
  expectRes(encodeUtf16("\xff\xbf\x80\x80\x80\x80\x80\x80"), u"\ufffd", true);

  // Surrogates encoded as separate UTF-8 code points are flagged as errors but allowed to decode
  // to UTF-16 surrogate values.
  expectRes(encodeUtf16("\xed\xb0\x80\xed\xaf\xbf"), u"\xdc00\xdbff", true);
  expectRes(encodeUtf16("\xed\xbf\xbf\xed\xa0\x80"), u"\xdfff\xd800", true);

  expectRes(encodeUtf16("\xed\xb0\x80\xed\xbf\xbf"), u"\xdc00\xdfff", true);
  expectRes(encodeUtf16("f\xed\xa0\x80"), u"f\xd800", true);
  expectRes(encodeUtf16("f\xed\xa0\x80x"), u"f\xd800x", true);
  expectRes(encodeUtf16("f\xed\xa0\x80\xed\xa0\x80x"), u"f\xd800\xd800x", true);

  // However, if successive UTF-8 codepoints decode to a proper surrogate pair, the second
  // surrogate is replaced with the Unicode replacement character to avoid creating valid UTF-16.
  expectRes(encodeUtf16("\xed\xa0\x80\xed\xbf\xbf"), u"\xd800\xfffd", true);
  expectRes(encodeUtf16("\xed\xaf\xbf\xed\xb0\x80"), u"\xdbff\xfffd", true);
}

ZC_TEST("encode UTF-8 to UTF-32") {
  expectRes(encodeUtf32(u8"foo"), U"foo");
  expectRes(encodeUtf32(u8"Здравствуйте"), U"Здравствуйте");
  expectRes(encodeUtf32(u8"中国网络"), U"中国网络");
  expectRes(encodeUtf32(u8"😺☁☄🐵"), U"😺☁☄🐵");
}

ZC_TEST("invalid UTF-8 to UTF-32") {
  // Disembodied continuation byte.
  expectRes(encodeUtf32("\x80"), U"\ufffd", true);
  expectRes(encodeUtf32("f\xbfo"), U"f\ufffdo", true);
  expectRes(encodeUtf32("f\xbf\x80\xb0o"), U"f\ufffdo", true);

  // Missing continuation bytes.
  expectRes(encodeUtf32("\xc2x"), U"\ufffdx", true);
  expectRes(encodeUtf32("\xe0x"), U"\ufffdx", true);
  expectRes(encodeUtf32("\xe0\xa0x"), U"\ufffdx", true);
  expectRes(encodeUtf32("\xf0x"), U"\ufffdx", true);
  expectRes(encodeUtf32("\xf0\x90x"), U"\ufffdx", true);
  expectRes(encodeUtf32("\xf0\x90\x80x"), U"\ufffdx", true);

  // Overlong sequences.
  expectRes(encodeUtf32("\xc0\x80"), U"\ufffd", true);
  expectRes(encodeUtf32("\xc1\xbf"), U"\ufffd", true);
  expectRes(encodeUtf32("\xc2\x80"), U"\u0080", false);
  expectRes(encodeUtf32("\xdf\xbf"), U"\u07ff", false);

  expectRes(encodeUtf32("\xe0\x80\x80"), U"\ufffd", true);
  expectRes(encodeUtf32("\xe0\x9f\xbf"), U"\ufffd", true);
  expectRes(encodeUtf32("\xe0\xa0\x80"), U"\u0800", false);
  expectRes(encodeUtf32("\xef\xbf\xbf"), U"\uffff", false);

  expectRes(encodeUtf32("\xf0\x80\x80\x80"), U"\ufffd", true);
  expectRes(encodeUtf32("\xf0\x8f\xbf\xbf"), U"\ufffd", true);
  expectRes(encodeUtf32("\xf0\x90\x80\x80"), U"\U00010000", false);
  expectRes(encodeUtf32("\xf4\x8f\xbf\xbf"), U"\U0010ffff", false);

  // Out of Unicode range.
  expectRes(encodeUtf32("\xf5\x80\x80\x80"), U"\ufffd", true);
  expectRes(encodeUtf32("\xf8\xbf\x80\x80\x80"), U"\ufffd", true);
  expectRes(encodeUtf32("\xfc\xbf\x80\x80\x80\x80"), U"\ufffd", true);
  expectRes(encodeUtf32("\xfe\xbf\x80\x80\x80\x80\x80"), U"\ufffd", true);
  expectRes(encodeUtf32("\xff\xbf\x80\x80\x80\x80\x80\x80"), U"\ufffd", true);
}

ZC_TEST("decode UTF-16 to UTF-8") {
  expectRes(decodeUtf16(u"foo"), u8"foo");
  expectRes(decodeUtf16(u"Здравствуйте"), u8"Здравствуйте");
  expectRes(decodeUtf16(u"中国网络"), u8"中国网络");
  expectRes(decodeUtf16(u"😺☁☄🐵"), u8"😺☁☄🐵");
}

ZC_TEST("invalid UTF-16 to UTF-8") {
  // Surrogates in wrong order.
  expectRes(decodeUtf16(u"\xdc00\xdbff"), "\xed\xb0\x80\xed\xaf\xbf", true);
  expectRes(decodeUtf16(u"\xdfff\xd800"), "\xed\xbf\xbf\xed\xa0\x80", true);

  // Missing second surrogate.
  expectRes(decodeUtf16(u"f\xd800"), "f\xed\xa0\x80", true);
  expectRes(decodeUtf16(u"f\xd800x"), "f\xed\xa0\x80x", true);
  expectRes(decodeUtf16(u"f\xd800\xd800x"), "f\xed\xa0\x80\xed\xa0\x80x", true);
}

ZC_TEST("decode UTF-32 to UTF-8") {
  expectRes(decodeUtf32(U"foo"), u8"foo");
  expectRes(decodeUtf32(U"Здравствуйте"), u8"Здравствуйте");
  expectRes(decodeUtf32(U"中国网络"), u8"中国网络");
  expectRes(decodeUtf32(U"😺☁☄🐵"), u8"😺☁☄🐵");
}

ZC_TEST("invalid UTF-32 to UTF-8") {
  // Surrogates rejected.
  expectRes(decodeUtf32(U"\xdfff\xd800"), "\xed\xbf\xbf\xed\xa0\x80", true);

  // Even if it would be a valid surrogate pair in UTF-16.
  expectRes(decodeUtf32(U"\xd800\xdfff"), "\xed\xa0\x80\xed\xbf\xbf", true);
}

ZC_TEST("round-trip invalid UTF-16") {
  const char16_t INVALID[] = u"\xdfff foo \xd800\xdc00 bar \xdc00\xd800 baz \xdbff qux \xd800";

  expectRes(encodeUtf16(decodeUtf16(INVALID)), INVALID, true);
  expectRes(encodeUtf16(decodeUtf32(encodeUtf32(decodeUtf16(INVALID)))), INVALID, true);
}

ZC_TEST("EncodingResult as a Maybe") {
  {
    auto result = encodeUtf16("\x80");
    ZC_EXPECT(result != nullptr);   // It has bytes ...
    ZC_EXPECT(result == zc::none);  // But an error.
    ZC_IF_SOME(unused, result) {
      (void)unused;
      ZC_FAIL_EXPECT("expected failure");
    }
  }

  {
    auto result = encodeUtf16("foo");
    ZC_EXPECT(result != nullptr);
    ZC_EXPECT(result != zc::none);
    ZC_IF_SOME(unused, result) {
      (void)unused;
      // good
    }
    else { ZC_FAIL_EXPECT("expected success"); }
  }

  ZC_EXPECT(ZC_ASSERT_NONNULL(decodeUtf16(u"foo")) == "foo");
}

ZC_TEST("encode to wchar_t") {
  expectRes(encodeWideString(u8"foo"), L"foo");
  expectRes(encodeWideString(u8"Здравствуйте"), L"Здравствуйте");
  expectRes(encodeWideString(u8"中国网络"), L"中国网络");
  expectRes(encodeWideString(u8"😺☁☄🐵"), L"😺☁☄🐵");
}

ZC_TEST("decode from wchar_t") {
  expectRes(decodeWideString(L"foo"), u8"foo");
  expectRes(decodeWideString(L"Здравствуйте"), u8"Здравствуйте");
  expectRes(decodeWideString(L"中国网络"), u8"中国网络");
  expectRes(decodeWideString(L"😺☁☄🐵"), u8"😺☁☄🐵");
}

// =======================================================================================

ZC_TEST("hex encoding/decoding") {
  byte bytes[] = {0x12, 0x34, 0xab, 0xf2};

  ZC_EXPECT(encodeHex(bytes) == "1234abf2");

  expectRes(decodeHex("1234abf2"), bytes);

  expectRes(decodeHex("1234abf21"), bytes, true);

  bytes[2] = 0xa0;
  expectRes(decodeHex("1234axf2"), bytes, true);

  bytes[2] = 0x0b;
  expectRes(decodeHex("1234xbf2"), bytes, true);
}

constexpr char RFC2396_FRAGMENT_SET_DIFF[] = "#$&+,/:;=?@[\\]^{|}";
// These are the characters reserved in RFC 2396, but not in the fragment percent encode set.

ZC_TEST("URI encoding/decoding") {
  ZC_EXPECT(encodeUriComponent("foo") == "foo");
  ZC_EXPECT(encodeUriComponent("foo bar") == "foo%20bar");
  ZC_EXPECT(encodeUriComponent("\xab\xba") == "%AB%BA");
  ZC_EXPECT(encodeUriComponent(StringPtr("foo\0bar", 7)) == "foo%00bar");

  ZC_EXPECT(encodeUriComponent(RFC2396_FRAGMENT_SET_DIFF) ==
            "%23%24%26%2B%2C%2F%3A%3B%3D%3F%40%5B%5C%5D%5E%7B%7C%7D");

  // Encode characters reserved by application/x-www-form-urlencoded, but not by RFC 2396.
  ZC_EXPECT(encodeUriComponent("'foo'! (~)") == "'foo'!%20(~)");

  expectRes(decodeUriComponent("foo%20bar"), "foo bar");
  expectRes(decodeUriComponent("%ab%BA"), "\xab\xba");

  expectRes(decodeUriComponent("foo%1xxx"), "foo\1xxx", true);
  expectRes(decodeUriComponent("foo%1"), "foo\1", true);
  expectRes(decodeUriComponent("foo%xxx"), "fooxxx", true);
  expectRes(decodeUriComponent("foo%"), "foo", true);

  {
    byte bytes[] = {12, 34, 56};
    ZC_EXPECT(decodeBinaryUriComponent(encodeUriComponent(bytes)).asPtr() == bytes);

    // decodeBinaryUriComponent() takes a DecodeUriOptions struct as its second parameter, but it
    // once took a single `bool nulTerminate`. Verify that the old behavior still compiles and
    // works.
    auto bytesWithNul = decodeBinaryUriComponent(encodeUriComponent(bytes), true);
    ZC_ASSERT(bytesWithNul.size() == 4);
    ZC_EXPECT(bytesWithNul[3] == '\0');
    ZC_EXPECT(bytesWithNul.first(3) == bytes);
  }
}

ZC_TEST("URL component encoding") {
  ZC_EXPECT(encodeUriFragment("foo") == "foo");
  ZC_EXPECT(encodeUriFragment("foo bar") == "foo%20bar");
  ZC_EXPECT(encodeUriFragment("\xab\xba") == "%AB%BA");
  ZC_EXPECT(encodeUriFragment(StringPtr("foo\0bar", 7)) == "foo%00bar");

  ZC_EXPECT(encodeUriFragment(RFC2396_FRAGMENT_SET_DIFF) == RFC2396_FRAGMENT_SET_DIFF);

  ZC_EXPECT(encodeUriPath("foo") == "foo");
  ZC_EXPECT(encodeUriPath("foo bar") == "foo%20bar");
  ZC_EXPECT(encodeUriPath("\xab\xba") == "%AB%BA");
  ZC_EXPECT(encodeUriPath(StringPtr("foo\0bar", 7)) == "foo%00bar");

  ZC_EXPECT(encodeUriPath(RFC2396_FRAGMENT_SET_DIFF) == "%23$&+,%2F:;=%3F@[%5C]^%7B|%7D");

  ZC_EXPECT(encodeUriUserInfo("foo") == "foo");
  ZC_EXPECT(encodeUriUserInfo("foo bar") == "foo%20bar");
  ZC_EXPECT(encodeUriUserInfo("\xab\xba") == "%AB%BA");
  ZC_EXPECT(encodeUriUserInfo(StringPtr("foo\0bar", 7)) == "foo%00bar");

  ZC_EXPECT(encodeUriUserInfo(RFC2396_FRAGMENT_SET_DIFF) ==
            "%23$&+,%2F%3A%3B%3D%3F%40%5B%5C%5D%5E%7B%7C%7D");

  // NOTE: None of these functions have explicit decode equivalents.
}

ZC_TEST("application/x-www-form-urlencoded encoding/decoding") {
  ZC_EXPECT(encodeWwwForm("foo") == "foo");
  ZC_EXPECT(encodeWwwForm("foo bar") == "foo+bar");
  ZC_EXPECT(encodeWwwForm("\xab\xba") == "%AB%BA");
  ZC_EXPECT(encodeWwwForm(StringPtr("foo\0bar", 7)) == "foo%00bar");

  // Encode characters reserved by application/x-www-form-urlencoded, but not by RFC 2396.
  ZC_EXPECT(encodeWwwForm("'foo'! (~)") == "%27foo%27%21+%28%7E%29");

  expectRes(decodeWwwForm("foo%20bar"), "foo bar");
  expectRes(decodeWwwForm("foo+bar"), "foo bar");
  expectRes(decodeWwwForm("%ab%BA"), "\xab\xba");

  expectRes(decodeWwwForm("foo%1xxx"), "foo\1xxx", true);
  expectRes(decodeWwwForm("foo%1"), "foo\1", true);
  expectRes(decodeWwwForm("foo%xxx"), "fooxxx", true);
  expectRes(decodeWwwForm("foo%"), "foo", true);

  {
    byte bytes[] = {12, 34, 56};
    DecodeUriOptions options{/*.nulTerminate=*/false, /*.plusToSpace=*/true};
    ZC_EXPECT(decodeBinaryUriComponent(encodeWwwForm(bytes), options) == bytes);
  }
}

ZC_TEST("C escape encoding/decoding") {
  ZC_EXPECT(encodeCEscape("fooo\a\b\f\n\r\t\v\'\"\\barПривет, Мир! Ж=О") ==
            "fooo\\a\\b\\f\\n\\r\\t\\v\\\'\\\"\\\\bar\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1"
            "\x82\x2c\x20\xd0\x9c\xd0\xb8\xd1\x80\x21\x20\xd0\x96\x3d\xd0\x9e");
  ZC_EXPECT(encodeCEscape("foo\x01\x7fxxx") == "foo\\001\\177xxx");
  byte bytes[] = {'f', 'o', 'o', 0, '\x01', '\x7f', 'x', 'x', 'x', 128, 254, 255};
  ZC_EXPECT(encodeCEscape(bytes) == "foo\\000\\001\\177xxx\\200\\376\\377");

  expectRes(decodeCEscape("fooo\\a\\b\\f\\n\\r\\t\\v\\\'\\\"\\\\bar"),
            "fooo\a\b\f\n\r\t\v\'\"\\bar");
  expectRes(decodeCEscape("foo\\x01\\x7fxxx"), "foo\x01\x7fxxx");
  expectRes(decodeCEscape("foo\\001\\177234"), "foo\001\177234");
  expectRes(decodeCEscape("foo\\x1"), "foo\x1");
  expectRes(decodeCEscape("foo\\1"), "foo\1");

  expectRes(decodeCEscape("foo\\u1234bar"), u8"foo\u1234bar");
  expectRes(decodeCEscape("foo\\U00045678bar"), u8"foo\U00045678bar");

  // Error cases.
  expectRes(decodeCEscape("foo\\"), "foo", true);
  expectRes(decodeCEscape("foo\\x123x"), u8"foo\x23x", true);
  expectRes(decodeCEscape("foo\\u12"), u8"foo\u0012", true);
  expectRes(decodeCEscape("foo\\u12xxx"), u8"foo\u0012xxx", true);
  expectRes(decodeCEscape("foo\\U12"), u8"foo\u0012", true);
  expectRes(decodeCEscape("foo\\U12xxxxxxxx"), u8"foo\u0012xxxxxxxx", true);
}

ZC_TEST("base64 encoding/decoding") {
  {
    auto encoded = encodeBase64(""_zcb, false);
    ZC_EXPECT(encoded == "", encoded, encoded.size());
    ZC_EXPECT(heapString(decodeBase64(encoded.asArray()).asChars()) == "");
  }

  {
    auto encoded = encodeBase64("foo"_zcb, false);
    ZC_EXPECT(encoded == "Zm9v", encoded, encoded.size());
    auto decoded = decodeBase64(encoded.asArray());
    ZC_EXPECT(!decoded.hadErrors);
    ZC_EXPECT(heapString(decoded.asChars()) == "foo");
  }

  {
    auto encoded = encodeBase64("quux"_zcb, false);
    ZC_EXPECT(encoded == "cXV1eA==", encoded, encoded.size());
    ZC_EXPECT(heapString(decodeBase64(encoded.asArray()).asChars()) == "quux");
  }

  {
    auto encoded = encodeBase64("corge"_zcb, false);
    ZC_EXPECT(encoded == "Y29yZ2U=", encoded);
    auto decoded = decodeBase64(encoded.asArray());
    ZC_EXPECT(!decoded.hadErrors);
    ZC_EXPECT(heapString(decoded.asChars()) == "corge");
  }

  {
    auto decoded = decodeBase64("Y29yZ2U");
    ZC_EXPECT(!decoded.hadErrors);
    ZC_EXPECT(heapString(decoded.asChars()) == "corge");
  }

  {
    auto decoded = decodeBase64("Y\n29y Z@2U=\n");
    ZC_EXPECT(decoded.hadErrors);  // @-sign is invalid base64 input.
    ZC_EXPECT(heapString(decoded.asChars()) == "corge");
  }

  {
    auto decoded = decodeBase64("Y\n29y Z2U=\n");
    ZC_EXPECT(!decoded.hadErrors);
    ZC_EXPECT(heapString(decoded.asChars()) == "corge");
  }

  // Too much padding.
  ZC_EXPECT(decodeBase64("Y29yZ2U==").hadErrors);
  ZC_EXPECT(decodeBase64("Y29yZ===").hadErrors);

  // Non-terminal padding.
  ZC_EXPECT(decodeBase64("ab=c").hadErrors);

  {
    auto encoded = encodeBase64("corge"_zcb, true);
    ZC_EXPECT(encoded == "Y29yZ2U=\n", encoded);
  }

  StringPtr fullLine = "012345678901234567890123456789012345678901234567890123";
  {
    auto encoded = encodeBase64(fullLine.asBytes(), false);
    ZC_EXPECT(encoded == "MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIzNDU2Nzg5MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIz",
              encoded);
  }
  {
    auto encoded = encodeBase64(fullLine.asBytes(), true);
    ZC_EXPECT(
        encoded == "MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIzNDU2Nzg5MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIz\n",
        encoded);
  }

  String multiLine = str(fullLine, "456");
  {
    auto encoded = encodeBase64(multiLine.asBytes(), false);
    ZC_EXPECT(
        encoded == "MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIzNDU2Nzg5MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIzNDU2",
        encoded);
  }
  {
    auto encoded = encodeBase64(multiLine.asBytes(), true);
    ZC_EXPECT(encoded ==
                  "MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIzNDU2Nzg5MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIz\n"
                  "NDU2\n",
              encoded);
  }
}

ZC_TEST("base64 url encoding") {
  {
    // Handles empty.
    auto encoded = encodeBase64Url(""_zcb);
    ZC_EXPECT(encoded == "", encoded, encoded.size());
  }

  {
    // Handles paddingless encoding.
    auto encoded = encodeBase64Url("foo"_zcb);
    ZC_EXPECT(encoded == "Zm9v", encoded, encoded.size());
  }

  {
    // Handles padded encoding.
    auto encoded1 = encodeBase64Url("quux"_zcb);
    ZC_EXPECT(encoded1 == "cXV1eA", encoded1, encoded1.size());
    auto encoded2 = encodeBase64Url("corge"_zcb);
    ZC_EXPECT(encoded2 == "Y29yZ2U", encoded2, encoded2.size());
  }

  {
    // No line breaks.
    StringPtr fullLine = "012345678901234567890123456789012345678901234567890123";
    auto encoded = encodeBase64Url(StringPtr(fullLine).asBytes());
    ZC_EXPECT(encoded == "MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIzNDU2Nzg5MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIz",
              encoded);
  }

  {
    // Replaces plusses.
    const byte data[] = {0b11111011, 0b11101111, 0b10111110};
    auto encoded = encodeBase64Url(data);
    ZC_EXPECT(encoded == "----", encoded, encoded.size(), data);
  }

  {
    // Replaces slashes.
    const byte data[] = {0b11111111, 0b11111111, 0b11111111};
    auto encoded = encodeBase64Url(data);
    ZC_EXPECT(encoded == "____", encoded, encoded.size(), data);
  }
}

}  // namespace
}  // namespace zc
