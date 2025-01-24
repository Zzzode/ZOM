// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
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

#include "zc/core/string.h"

#include <locale.h>
#include <stdint.h>
#include <zc/ztest/gtest.h>

#include <string>

#include "zc/core/vector.h"

namespace zc {
namespace _ {  // private
namespace {

TEST(String, Str) {
  EXPECT_EQ("foobar", str("foo", "bar"));
  EXPECT_EQ("1 2 3 4", str(1, " ", 2u, " ", 3l, " ", 4ll));
  EXPECT_EQ("1.5 foo 1e15 bar -3", str(1.5f, " foo ", 1e15, " bar ", -3));
  EXPECT_EQ("foo", str('f', 'o', 'o'));
  EXPECT_EQ("123 234 -123 e7",
            str((int8_t)123, " ", (uint8_t)234, " ", (int8_t)-123, " ", hex((uint8_t)0xe7)));
  EXPECT_EQ("-128 -32768 -2147483648 -9223372036854775808",
            str((signed char)-128, ' ', (signed short)-32768, ' ', ((int)-2147483647) - 1, ' ',
                ((long long)-9223372036854775807ll) - 1))
  EXPECT_EQ("ff ffff ffffffff ffffffffffffffff",
            str(hex((uint8_t)0xff), ' ', hex((uint16_t)0xffff), ' ', hex((uint32_t)0xffffffffu),
                ' ', hex((uint64_t)0xffffffffffffffffull)));

  char buf[3] = {'f', 'o', 'o'};
  ArrayPtr<char> a = buf;
  ArrayPtr<const char> ca = a;
  Vector<char> v;
  v.addAll(a);
  FixedArray<char, 3> f;
  memcpy(f.begin(), buf, 3);

  EXPECT_EQ("foo", str(a));
  EXPECT_EQ("foo", str(ca));
  EXPECT_EQ("foo", str(v));
  EXPECT_EQ("foo", str(f));
  EXPECT_EQ("foo", str(mv(a)));
  EXPECT_EQ("foo", str(mv(ca)));
  EXPECT_EQ("foo", str(mv(v)));
  EXPECT_EQ("foo", str(mv(f)));
}

TEST(String, Nullptr) {
  EXPECT_EQ(String(nullptr), "");
  EXPECT_EQ(StringPtr(String(nullptr)).size(), 0u);
  EXPECT_EQ(StringPtr(String(nullptr))[0], '\0');
}

TEST(String, StartsEndsWith) {
  EXPECT_TRUE(StringPtr("foobar").startsWith("foo"));
  EXPECT_FALSE(StringPtr("foobar").startsWith("bar"));
  EXPECT_FALSE(StringPtr("foobar").endsWith("foo"));
  EXPECT_TRUE(StringPtr("foobar").endsWith("bar"));

  EXPECT_FALSE(StringPtr("fo").startsWith("foo"));
  EXPECT_FALSE(StringPtr("fo").endsWith("foo"));

  EXPECT_TRUE(StringPtr("foobar").startsWith(""));
  EXPECT_TRUE(StringPtr("foobar").endsWith(""));
}

TEST(String, parseAs) {
  EXPECT_EQ(StringPtr("0").parseAs<double>(), 0.0);
  EXPECT_EQ(StringPtr("0.0").parseAs<double>(), 0.0);
  EXPECT_EQ(StringPtr("1").parseAs<double>(), 1.0);
  EXPECT_EQ(StringPtr("1.0").parseAs<double>(), 1.0);
  EXPECT_EQ(StringPtr("1e100").parseAs<double>(), 1e100);
  EXPECT_EQ(StringPtr("inf").parseAs<double>(), inf());
  EXPECT_EQ(StringPtr("infinity").parseAs<double>(), inf());
  EXPECT_EQ(StringPtr("INF").parseAs<double>(), inf());
  EXPECT_EQ(StringPtr("INFINITY").parseAs<double>(), inf());
  EXPECT_EQ(StringPtr("1e100000").parseAs<double>(), inf());
  EXPECT_EQ(StringPtr("-inf").parseAs<double>(), -inf());
  EXPECT_EQ(StringPtr("-infinity").parseAs<double>(), -inf());
  EXPECT_EQ(StringPtr("-INF").parseAs<double>(), -inf());
  EXPECT_EQ(StringPtr("-INFINITY").parseAs<double>(), -inf());
  EXPECT_EQ(StringPtr("-1e100000").parseAs<double>(), -inf());
  EXPECT_TRUE(isNaN(StringPtr("nan").parseAs<double>()));
  EXPECT_TRUE(isNaN(StringPtr("NAN").parseAs<double>()));
  EXPECT_TRUE(isNaN(StringPtr("NaN").parseAs<double>()));
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", StringPtr("").parseAs<double>());
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", StringPtr("a").parseAs<double>());
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", StringPtr("1a").parseAs<double>());
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", StringPtr("+-1").parseAs<double>());

  EXPECT_EQ(StringPtr("1").parseAs<float>(), 1.0);

  EXPECT_EQ(StringPtr("1").parseAs<int64_t>(), 1);
  EXPECT_EQ(StringPtr("9223372036854775807").parseAs<int64_t>(), 9223372036854775807LL);
  EXPECT_EQ(StringPtr("-9223372036854775808").parseAs<int64_t>(), -9223372036854775808ULL);
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("out-of-range",
                                      StringPtr("9223372036854775808").parseAs<int64_t>());
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("out-of-range",
                                      StringPtr("-9223372036854775809").parseAs<int64_t>());
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", StringPtr("").parseAs<int64_t>());
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", StringPtr("a").parseAs<int64_t>());
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", StringPtr("1a").parseAs<int64_t>());
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", StringPtr("+-1").parseAs<int64_t>());
  EXPECT_EQ(StringPtr("010").parseAs<int64_t>(), 10);
  EXPECT_EQ(StringPtr("0010").parseAs<int64_t>(), 10);
  EXPECT_EQ(StringPtr("0x10").parseAs<int64_t>(), 16);
  EXPECT_EQ(StringPtr("0X10").parseAs<int64_t>(), 16);
  EXPECT_EQ(StringPtr("-010").parseAs<int64_t>(), -10);
  EXPECT_EQ(StringPtr("-0x10").parseAs<int64_t>(), -16);
  EXPECT_EQ(StringPtr("-0X10").parseAs<int64_t>(), -16);

  EXPECT_EQ(StringPtr("1").parseAs<uint64_t>(), 1);
  EXPECT_EQ(StringPtr("0").parseAs<uint64_t>(), 0);
  EXPECT_EQ(StringPtr("18446744073709551615").parseAs<uint64_t>(), 18446744073709551615ULL);
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("out-of-range", StringPtr("-1").parseAs<uint64_t>());
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("out-of-range",
                                      StringPtr("18446744073709551616").parseAs<uint64_t>());
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", StringPtr("").parseAs<uint64_t>());
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", StringPtr("a").parseAs<uint64_t>());
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", StringPtr("1a").parseAs<uint64_t>());
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", StringPtr("+-1").parseAs<uint64_t>());

  EXPECT_EQ(StringPtr("1").parseAs<int32_t>(), 1);
  EXPECT_EQ(StringPtr("2147483647").parseAs<int32_t>(), 2147483647);
  EXPECT_EQ(StringPtr("-2147483648").parseAs<int32_t>(), -2147483648);
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("out-of-range", StringPtr("2147483648").parseAs<int32_t>());
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("out-of-range", StringPtr("-2147483649").parseAs<int32_t>());

  EXPECT_EQ(StringPtr("1").parseAs<uint32_t>(), 1);
  EXPECT_EQ(StringPtr("0").parseAs<uint32_t>(), 0U);
  EXPECT_EQ(StringPtr("4294967295").parseAs<uint32_t>(), 4294967295U);
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("out-of-range", StringPtr("-1").parseAs<uint32_t>());
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("out-of-range", StringPtr("4294967296").parseAs<uint32_t>());

  EXPECT_EQ(StringPtr("1").parseAs<int16_t>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<uint16_t>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<int8_t>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<uint8_t>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<char>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<signed char>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<unsigned char>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<short>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<unsigned short>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<int>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<unsigned>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<long>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<unsigned long>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<long long>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<unsigned long long>(), 1);

  EXPECT_EQ(heapString("1").parseAs<int>(), 1);
}

TEST(String, tryParseAs) {
  ZC_EXPECT(StringPtr("0").tryParseAs<double>() == 0.0);
  ZC_EXPECT(StringPtr("0").tryParseAs<double>() == 0.0);
  ZC_EXPECT(StringPtr("0.0").tryParseAs<double>() == 0.0);
  ZC_EXPECT(StringPtr("1").tryParseAs<double>() == 1.0);
  ZC_EXPECT(StringPtr("1.0").tryParseAs<double>() == 1.0);
  ZC_EXPECT(StringPtr("1e100").tryParseAs<double>() == 1e100);
  ZC_EXPECT(StringPtr("inf").tryParseAs<double>() == inf());
  ZC_EXPECT(StringPtr("infinity").tryParseAs<double>() == inf());
  ZC_EXPECT(StringPtr("INF").tryParseAs<double>() == inf());
  ZC_EXPECT(StringPtr("INFINITY").tryParseAs<double>() == inf());
  ZC_EXPECT(StringPtr("1e100000").tryParseAs<double>() == inf());
  ZC_EXPECT(StringPtr("-inf").tryParseAs<double>() == -inf());
  ZC_EXPECT(StringPtr("-infinity").tryParseAs<double>() == -inf());
  ZC_EXPECT(StringPtr("-INF").tryParseAs<double>() == -inf());
  ZC_EXPECT(StringPtr("-INFINITY").tryParseAs<double>() == -inf());
  ZC_EXPECT(StringPtr("-1e100000").tryParseAs<double>() == -inf());
  ZC_EXPECT(isNaN(StringPtr("nan").tryParseAs<double>().orDefault(0.0)) == true);
  ZC_EXPECT(isNaN(StringPtr("NAN").tryParseAs<double>().orDefault(0.0)) == true);
  ZC_EXPECT(isNaN(StringPtr("NaN").tryParseAs<double>().orDefault(0.0)) == true);
  ZC_EXPECT(StringPtr("").tryParseAs<double>() == zc::none);
  ZC_EXPECT(StringPtr("a").tryParseAs<double>() == zc::none);
  ZC_EXPECT(StringPtr("1a").tryParseAs<double>() == zc::none);
  ZC_EXPECT(StringPtr("+-1").tryParseAs<double>() == zc::none);

  ZC_EXPECT(StringPtr("1").tryParseAs<float>() == 1.0);

  ZC_EXPECT(StringPtr("1").tryParseAs<int64_t>() == 1);
  ZC_EXPECT(StringPtr("9223372036854775807").tryParseAs<int64_t>() == 9223372036854775807LL);
  ZC_EXPECT(StringPtr("-9223372036854775808").tryParseAs<int64_t>() == -9223372036854775808ULL);
  ZC_EXPECT(StringPtr("9223372036854775808").tryParseAs<int64_t>() == zc::none);
  ZC_EXPECT(StringPtr("-9223372036854775809").tryParseAs<int64_t>() == zc::none);
  ZC_EXPECT(StringPtr("").tryParseAs<int64_t>() == zc::none);
  ZC_EXPECT(StringPtr("a").tryParseAs<int64_t>() == zc::none);
  ZC_EXPECT(StringPtr("1a").tryParseAs<int64_t>() == zc::none);
  ZC_EXPECT(StringPtr("+-1").tryParseAs<int64_t>() == zc::none);
  ZC_EXPECT(StringPtr("010").tryParseAs<int64_t>() == 10);
  ZC_EXPECT(StringPtr("0010").tryParseAs<int64_t>() == 10);
  ZC_EXPECT(StringPtr("0x10").tryParseAs<int64_t>() == 16);
  ZC_EXPECT(StringPtr("0X10").tryParseAs<int64_t>() == 16);
  ZC_EXPECT(StringPtr("-010").tryParseAs<int64_t>() == -10);
  ZC_EXPECT(StringPtr("-0x10").tryParseAs<int64_t>() == -16);
  ZC_EXPECT(StringPtr("-0X10").tryParseAs<int64_t>() == -16);

  ZC_EXPECT(StringPtr("1").tryParseAs<uint64_t>() == 1);
  ZC_EXPECT(StringPtr("0").tryParseAs<uint64_t>() == 0);
  ZC_EXPECT(StringPtr("18446744073709551615").tryParseAs<uint64_t>() == 18446744073709551615ULL);
  ZC_EXPECT(StringPtr("-1").tryParseAs<uint64_t>() == zc::none);
  ZC_EXPECT(StringPtr("18446744073709551616").tryParseAs<uint64_t>() == zc::none);
  ZC_EXPECT(StringPtr("").tryParseAs<uint64_t>() == zc::none);
  ZC_EXPECT(StringPtr("a").tryParseAs<uint64_t>() == zc::none);
  ZC_EXPECT(StringPtr("1a").tryParseAs<uint64_t>() == zc::none);
  ZC_EXPECT(StringPtr("+-1").tryParseAs<uint64_t>() == zc::none);

  ZC_EXPECT(StringPtr("1").tryParseAs<int32_t>() == 1);
  ZC_EXPECT(StringPtr("2147483647").tryParseAs<int32_t>() == 2147483647);
  ZC_EXPECT(StringPtr("-2147483648").tryParseAs<int32_t>() == -2147483648);
  ZC_EXPECT(StringPtr("2147483648").tryParseAs<int32_t>() == zc::none);
  ZC_EXPECT(StringPtr("-2147483649").tryParseAs<int32_t>() == zc::none);

  ZC_EXPECT(StringPtr("1").tryParseAs<uint32_t>() == 1);
  ZC_EXPECT(StringPtr("0").tryParseAs<uint32_t>() == 0U);
  ZC_EXPECT(StringPtr("4294967295").tryParseAs<uint32_t>() == 4294967295U);
  ZC_EXPECT(StringPtr("-1").tryParseAs<uint32_t>() == zc::none);
  ZC_EXPECT(StringPtr("4294967296").tryParseAs<uint32_t>() == zc::none);

  ZC_EXPECT(StringPtr("1").tryParseAs<int16_t>() == 1);
  ZC_EXPECT(StringPtr("1").tryParseAs<uint16_t>() == 1);
  ZC_EXPECT(StringPtr("1").tryParseAs<int8_t>() == 1);
  ZC_EXPECT(StringPtr("1").tryParseAs<uint8_t>() == 1);
  ZC_EXPECT(StringPtr("1").tryParseAs<char>() == 1);
  ZC_EXPECT(StringPtr("1").tryParseAs<signed char>() == 1);
  ZC_EXPECT(StringPtr("1").tryParseAs<unsigned char>() == 1);
  ZC_EXPECT(StringPtr("1").tryParseAs<short>() == 1);
  ZC_EXPECT(StringPtr("1").tryParseAs<unsigned short>() == 1);
  ZC_EXPECT(StringPtr("1").tryParseAs<int>() == 1);
  ZC_EXPECT(StringPtr("1").tryParseAs<unsigned>() == 1);
  ZC_EXPECT(StringPtr("1").tryParseAs<long>() == 1);
  ZC_EXPECT(StringPtr("1").tryParseAs<unsigned long>() == 1);
  ZC_EXPECT(StringPtr("1").tryParseAs<long long>() == 1);
  ZC_EXPECT(StringPtr("1").tryParseAs<unsigned long long>() == 1);

  ZC_EXPECT(heapString("1").tryParseAs<int>() == 1);
}

#if ZC_COMPILER_SUPPORTS_STL_STRING_INTEROP
TEST(String, StlInterop) {
  std::string foo = "foo";
  StringPtr ptr = foo;
  EXPECT_EQ("foo", ptr);

  std::string bar = ptr;
  EXPECT_EQ("foo", bar);

  EXPECT_EQ("foo", zc::str(foo));
  EXPECT_EQ("foo", zc::heapString(foo));
}

struct Stringable {
  zc::StringPtr toString() { return "foo"; }
};

TEST(String, ToString) { EXPECT_EQ("foo", zc::str(Stringable())); }
#endif

ZC_TEST("string literals with _zc suffix") {
  static constexpr StringPtr FOO = "foo"_zc;
  ZC_EXPECT(FOO == "foo", FOO);
  ZC_EXPECT(FOO[3] == 0);

  ZC_EXPECT("foo\0bar"_zc == StringPtr("foo\0bar", 7));

  static constexpr ArrayPtr<const char> ARR = "foo"_zc;
  ZC_EXPECT(ARR.size() == 3);
  ZC_EXPECT(zc::str(ARR) == "foo");
}

ZC_TEST("zc::delimited() and zc::strPreallocated()") {
  int rawArray[] = {1, 23, 456, 78};
  ArrayPtr<int> array = rawArray;
  ZC_EXPECT(str(delimited(array, "::")) == "1::23::456::78");

  {
    char buffer[256]{};
    ZC_EXPECT(strPreallocated(buffer, delimited(array, "::"), 'x') == "1::23::456::78x");
    ZC_EXPECT(strPreallocated(buffer, "foo", 123, true) == "foo123true");
  }

  {
    char buffer[5]{};
    ZC_EXPECT(strPreallocated(buffer, delimited(array, "::"), 'x') == "1::2");
    ZC_EXPECT(strPreallocated(buffer, "foo", 123, true) == "foo1");
  }
}

ZC_TEST("parsing 'nan' returns canonical NaN value") {
  // There are many representations of NaN. We would prefer that parsing "NaN" produces exactly the
  // same bits that zc::nan() returns.
  {
    double parsedNan = StringPtr("NaN").parseAs<double>();
    double canonicalNan = zc::nan();
    ZC_EXPECT(zc::arrayPtr(parsedNan).asBytes() == zc::arrayPtr(canonicalNan).asBytes());
  }
  {
    float parsedNan = StringPtr("NaN").parseAs<float>();
    float canonicalNan = zc::nan();
    ZC_EXPECT(zc::arrayPtr(parsedNan).asBytes() == zc::arrayPtr(canonicalNan).asBytes());
  }
}

ZC_TEST("stringify array-of-array") {
  int arr1[] = {1, 23};
  int arr2[] = {456, 7890};
  ArrayPtr<int> arr3[] = {arr1, arr2};
  ArrayPtr<ArrayPtr<int>> array = arr3;

  ZC_EXPECT(str(array) == "1, 23, 456, 7890");
}

ZC_TEST("ArrayPtr == StringPtr") {
  StringPtr s = "foo"_zc;
  ArrayPtr<const char> a = s;

  ZC_EXPECT(a == s);
#if __cplusplus >= 202000L
  ZC_EXPECT(s == a);
#endif
}

ZC_TEST("String == String") {
  String a = zc::str("foo");
  String b = zc::str("foo");
  String c = zc::str("bar");

  // We're trying to trigger the -Wambiguous-reversed-operator warning in Clang, but it seems
  // magic asserts inadvertently squelch it. So, we use an alternate macro with no magic.
#define ZC_EXPECT_NOMAGIC(cond)        \
  if (cond) {                          \
  } else {                             \
    ZC_FAIL_ASSERT("expected " #cond); \
  }

  ZC_EXPECT_NOMAGIC(a == a);
  ZC_EXPECT_NOMAGIC(a == b);
  ZC_EXPECT_NOMAGIC(a != c);
}

ZC_TEST("float stringification and parsing is not locale-dependent") {
  // Remember the old locale, set it back when we're done.
  char* oldLocaleCstr = setlocale(LC_NUMERIC, nullptr);
  ZC_ASSERT(oldLocaleCstr != nullptr);
  auto oldLocale = zc::str(oldLocaleCstr);
  ZC_DEFER(setlocale(LC_NUMERIC, oldLocale.cStr()));

  // Set the locale to "C".
  ZC_ASSERT(setlocale(LC_NUMERIC, "C") != nullptr);

  ZC_ASSERT(zc::str(1.5) == "1.5");
  ZC_ASSERT(zc::str(1.5f) == "1.5");
  ZC_EXPECT("1.5"_zc.parseAs<float>() == 1.5);
  ZC_EXPECT("1.5"_zc.parseAs<double>() == 1.5);

  if (setlocale(LC_NUMERIC, "es_ES") == nullptr && setlocale(LC_NUMERIC, "es_ES.utf8") == nullptr &&
      setlocale(LC_NUMERIC, "es_ES.UTF-8") == nullptr) {
    // Some systems may not have the desired locale available.
    ZC_LOG(WARNING, "Couldn't set locale to es_ES. Skipping this test.");
  } else {
    ZC_EXPECT(zc::str(1.5) == "1.5");
    ZC_EXPECT(zc::str(1.5f) == "1.5");
    ZC_EXPECT("1.5"_zc.parseAs<float>() == 1.5);
    ZC_EXPECT("1.5"_zc.parseAs<double>() == 1.5);
  }
}

ZC_TEST("ConstString literal operator") {
  zc::ConstString theString = "it's a const string!"_zcc;
  ZC_EXPECT(theString == "it's a const string!");
}

ZC_TEST("ConstString promotion") {
  zc::StringPtr theString = "it's a const string!";
  zc::ConstString constString = theString.attach();
  ZC_EXPECT(constString == "it's a const string!");
}

struct DestructionOrderRecorder {
  DestructionOrderRecorder(uint& counter, uint& recordTo) : counter(counter), recordTo(recordTo) {}
  ~DestructionOrderRecorder() { recordTo = ++counter; }

  uint& counter;
  uint& recordTo;
};

ZC_TEST("ConstString attachment lifetimes") {
  uint counter = 0;
  uint destroyed1 = 0;
  uint destroyed2 = 0;
  uint destroyed3 = 0;

  auto obj1 = zc::heap<DestructionOrderRecorder>(counter, destroyed1);
  auto obj2 = zc::heap<DestructionOrderRecorder>(counter, destroyed2);
  auto obj3 = zc::heap<DestructionOrderRecorder>(counter, destroyed3);

  StringPtr theString = "it's a string!";
  const char* ptr = theString.begin();

  ConstString combined = theString.attach(zc::mv(obj1), zc::mv(obj2), zc::mv(obj3));

  ZC_EXPECT(combined.begin() == ptr);

  ZC_EXPECT(obj1.get() == nullptr);
  ZC_EXPECT(obj2.get() == nullptr);
  ZC_EXPECT(obj3.get() == nullptr);
  ZC_EXPECT(destroyed1 == 0);
  ZC_EXPECT(destroyed2 == 0);
  ZC_EXPECT(destroyed3 == 0);

  combined = nullptr;

  ZC_EXPECT(destroyed1 == 1, destroyed1);
  ZC_EXPECT(destroyed2 == 2, destroyed2);
  ZC_EXPECT(destroyed3 == 3, destroyed3);
}

ZC_TEST("StringPtr find") {
  // Empty string doesn't find anything
  StringPtr empty("");
  ZC_EXPECT(empty.find("") == 0);
  ZC_EXPECT(empty.find("foo") == zc::none);

  StringPtr foobar("foobar"_zc);
  ZC_EXPECT(foobar.find("") == 0);
  ZC_EXPECT(foobar.find("baz") == zc::none);
  ZC_EXPECT(foobar.find("foobar") == 0);
  ZC_EXPECT(foobar.find("f") == 0);
  ZC_EXPECT(foobar.find("oobar") == 1);
  ZC_EXPECT(foobar.find("ar") == 4);
  ZC_EXPECT(foobar.find("o") == 1);
  ZC_EXPECT(foobar.find("oo") == 1);
  ZC_EXPECT(foobar.find("r") == 5);
  ZC_EXPECT(foobar.find("foobar!") == zc::none);

  // Self pointers shouldn't cause issues, but it's worth testing.
  ZC_EXPECT(foobar.find(foobar) == 0);
  ZC_EXPECT(foobar.find(foobar.slice(1)) == 1);
  ZC_EXPECT(foobar.slice(1).find(foobar.slice(1)) == 0);
  ZC_EXPECT(foobar.slice(2).find(foobar.slice(1)) == zc::none);
}

ZC_TEST("StringPtr contains") {
  // Empty string doesn't find anything
  StringPtr empty("");
  ZC_EXPECT(empty.contains("") == true);
  ZC_EXPECT(empty.contains("foo") == false);

  StringPtr foobar("foobar"_zc);
  ZC_EXPECT(foobar.contains("") == true);
  ZC_EXPECT(foobar.contains("baz") == false);
  ZC_EXPECT(foobar.contains("foobar") == true);
  ZC_EXPECT(foobar.contains("f") == true);
  ZC_EXPECT(foobar.contains("oobar") == true);
  ZC_EXPECT(foobar.contains("ar") == true);
  ZC_EXPECT(foobar.contains("o") == true);
  ZC_EXPECT(foobar.contains("oo") == true);
  ZC_EXPECT(foobar.contains("r") == true);
  ZC_EXPECT(foobar.contains("foobar!") == false);

  // Self pointers shouldn't cause issues, but it's worth testing.
  ZC_EXPECT(foobar.contains(foobar) == true);
  ZC_EXPECT(foobar.contains(foobar.slice(1)) == true);
  ZC_EXPECT(foobar.slice(1).contains(foobar.slice(1)) == true);
  ZC_EXPECT(foobar.slice(2).contains(foobar.slice(1)) == false);
}

struct Std {
  static std::string from(const String* str) { return std::string(str->cStr()); }

  static std::string from(const StringPtr* str) { return std::string(str->cStr()); }
};

ZC_TEST("as<Std>") {
  String str = zc::str("foo"_zc);
  std::string stdStr = str.as<Std>();
  ZC_EXPECT(stdStr == "foo");

  StringPtr ptr = "bar"_zc;
  std::string stdPtr = ptr.as<Std>();
  ZC_EXPECT(stdPtr == "bar");
}

// Supports constexpr
constexpr const StringPtr HELLO_WORLD = "hello world"_zc;
static_assert(HELLO_WORLD.size() == 11);
static_assert(HELLO_WORLD.startsWith("hello"_zc));
static_assert(HELLO_WORLD.endsWith("world"_zc));
static_assert(HELLO_WORLD[0] == *"h");
static_assert(HELLO_WORLD.asArray().size() == 11);
static_assert(*HELLO_WORLD.asArray().begin() == 'h');
static_assert(HELLO_WORLD.asArray().front() == 'h');
static_assert(HELLO_WORLD.first(2).size() == 2);
static_assert(HELLO_WORLD.slice(5).size() == 6);
static_assert(StringPtr().size() == 0);
static_assert(StringPtr(nullptr).size() == 0);
static_assert(StringPtr(HELLO_WORLD.begin(), HELLO_WORLD.size()).size() == 11);
static_assert(HELLO_WORLD > StringPtr());
static_assert(StringPtr("const"_zc).size() == 5);

}  // namespace
}  // namespace _
}  // namespace zc
