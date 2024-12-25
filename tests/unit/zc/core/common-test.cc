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

#include "src/zc/core/common.h"

#include <inttypes.h>
#include <src/zc/ztest/gtest.h>

#include <span>

#include "src/zc/ztest/test.h"

namespace zc {
namespace {

ZC_TEST("zc::size() on native arrays") {
  int arr[] = {12, 34, 56, 78};

  size_t expected = 0;
  for (size_t i : indices(arr)) { ZC_EXPECT(i == expected++); }
  ZC_EXPECT(expected == 4u);
}

struct ImplicitToInt {
  int i;

  operator int() const { return i; }
};

struct Immovable {
  Immovable() = default;
  ZC_DISALLOW_COPY_AND_MOVE(Immovable);
};

struct CopyOrMove {
  // Type that detects the difference between copy and move.
  CopyOrMove(int i) : i(i) {}
  CopyOrMove(CopyOrMove&& other) : i(other.i) { other.i = -1; }
  CopyOrMove(const CopyOrMove&) = default;

  int i;
};

TEST(Common, Maybe) {
  {
    Maybe<int> m = 123;
    EXPECT_FALSE(m == zc::none);
    EXPECT_TRUE(m != zc::none);
    ZC_IF_SOME(v, m) { EXPECT_EQ(123, v); }
    else { ADD_FAILURE(); }
    ZC_IF_SOME(v, mv(m)) { EXPECT_EQ(123, v); }
    else { ADD_FAILURE(); }
    EXPECT_EQ(123, m.orDefault(456));
    bool ranLazy = false;
    EXPECT_EQ(123, m.orDefault([&] {
      ranLazy = true;
      return 456;
    }));
    EXPECT_FALSE(ranLazy);

    ZC_IF_SOME(v, m) {
      int notUsedForRef = 5;
      const int& ref = m.orDefault([&]() -> int& { return notUsedForRef; });

      EXPECT_EQ(ref, v);
      EXPECT_EQ(&ref, &v);

      const int& ref2 = m.orDefault([notUsed = 5]() -> int { return notUsed; });
      EXPECT_NE(&ref, &ref2);
      EXPECT_EQ(ref2, 123);
    }
    else { ADD_FAILURE(); }
  }

  {
    Maybe<Own<CopyOrMove>> m = zc::heap<CopyOrMove>(123);
    EXPECT_FALSE(m == zc::none);
    EXPECT_TRUE(m != zc::none);
    ZC_IF_SOME(v, m) { EXPECT_EQ(123, v->i); }
    else { ADD_FAILURE(); }
    ZC_IF_SOME(v, mv(m)) { EXPECT_EQ(123, v->i); }
    else { ADD_FAILURE(); }
    // We have moved the zc::Own away, so this should give us the default and leave the Maybe empty.
    EXPECT_EQ(456, m.orDefault(heap<CopyOrMove>(456))->i);
    EXPECT_TRUE(m == zc::none);

    bool ranLazy = false;
    EXPECT_EQ(123, mv(m)
                       .orDefault([&] {
                         ranLazy = true;
                         return heap<CopyOrMove>(123);
                       })
                       ->i);
    EXPECT_TRUE(ranLazy);
    EXPECT_TRUE(m == zc::none);

    m = heap<CopyOrMove>(123);
    EXPECT_TRUE(m != zc::none);
    ranLazy = false;
    EXPECT_EQ(123, mv(m)
                       .orDefault([&] {
                         ranLazy = true;
                         return heap<CopyOrMove>(456);
                       })
                       ->i);
    EXPECT_FALSE(ranLazy);
    EXPECT_TRUE(m == zc::none);
  }

  {
    Maybe<int> empty;
    int defaultValue = 5;
    auto& ref1 = empty.orDefault([&defaultValue]() -> int& { return defaultValue; });
    EXPECT_EQ(&ref1, &defaultValue);

    auto ref2 = empty.orDefault([&]() -> int { return defaultValue; });
    EXPECT_NE(&ref2, &defaultValue);
  }

  {
    Maybe<int> m = 0;
    EXPECT_FALSE(m == zc::none);
    EXPECT_TRUE(m != zc::none);
    ZC_IF_SOME(v, m) { EXPECT_EQ(0, v); }
    else { ADD_FAILURE(); }
    ZC_IF_SOME(v, mv(m)) { EXPECT_EQ(0, v); }
    else { ADD_FAILURE(); }
    EXPECT_EQ(0, m.orDefault(456));
    bool ranLazy = false;
    EXPECT_EQ(0, m.orDefault([&] {
      ranLazy = true;
      return 456;
    }));
    EXPECT_FALSE(ranLazy);
  }

  {
    Maybe<int> m = zc::none;
    EXPECT_TRUE(m == zc::none);
    EXPECT_FALSE(m != zc::none);
    ZC_IF_SOME(v, m) {
      ADD_FAILURE();
      EXPECT_EQ(0, v);  // avoid unused warning
    }
    ZC_IF_SOME(v, mv(m)) {
      ADD_FAILURE();
      EXPECT_EQ(0, v);  // avoid unused warning
    }
    EXPECT_EQ(456, m.orDefault(456));
    bool ranLazy = false;
    EXPECT_EQ(456, m.orDefault([&] {
      ranLazy = true;
      return 456;
    }));
    EXPECT_TRUE(ranLazy);
  }

  int i = 234;
  {
    Maybe<int&> m = i;
    EXPECT_FALSE(m == zc::none);
    EXPECT_TRUE(m != zc::none);
    ZC_IF_SOME(v, m) { EXPECT_EQ(&i, &v); }
    else { ADD_FAILURE(); }
    ZC_IF_SOME(v, mv(m)) { EXPECT_EQ(&i, &v); }
    else { ADD_FAILURE(); }
    EXPECT_EQ(234, m.orDefault(456));
  }

  {
    Maybe<int&> m = zc::none;
    EXPECT_TRUE(m == zc::none);
    EXPECT_FALSE(m != zc::none);
    ZC_IF_SOME(v, m) {
      ADD_FAILURE();
      EXPECT_EQ(0, v);  // avoid unused warning
    }
    ZC_IF_SOME(v, mv(m)) {
      ADD_FAILURE();
      EXPECT_EQ(0, v);  // avoid unused warning
    }
    EXPECT_EQ(456, m.orDefault(456));
  }

  {
    Maybe<int&> m = &i;
    EXPECT_FALSE(m == zc::none);
    EXPECT_TRUE(m != zc::none);
    ZC_IF_SOME(v, m) { EXPECT_EQ(&i, &v); }
    else { ADD_FAILURE(); }
    ZC_IF_SOME(v, mv(m)) { EXPECT_EQ(&i, &v); }
    else { ADD_FAILURE(); }
    EXPECT_EQ(234, m.orDefault(456));
  }

  {
    const Maybe<int&> m2 = &i;
    Maybe<const int&> m = m2;
    EXPECT_FALSE(m == zc::none);
    EXPECT_TRUE(m != zc::none);
    ZC_IF_SOME(v, m) { EXPECT_EQ(&i, &v); }
    else { ADD_FAILURE(); }
    ZC_IF_SOME(v, mv(m)) { EXPECT_EQ(&i, &v); }
    else { ADD_FAILURE(); }
    EXPECT_EQ(234, m.orDefault(456));
  }

  {
    Maybe<int&> m = implicitCast<int*>(nullptr);
    EXPECT_TRUE(m == zc::none);
    EXPECT_FALSE(m != zc::none);
    ZC_IF_SOME(v, m) {
      ADD_FAILURE();
      EXPECT_EQ(0, v);  // avoid unused warning
    }
    ZC_IF_SOME(v, mv(m)) {
      ADD_FAILURE();
      EXPECT_EQ(0, v);  // avoid unused warning
    }
    EXPECT_EQ(456, m.orDefault(456));
  }

  {
    Maybe<int> mi = i;
    Maybe<int&> m = mi;
    EXPECT_FALSE(m == zc::none);
    EXPECT_TRUE(m != zc::none);
    ZC_IF_SOME(v, m) { EXPECT_EQ(&ZC_ASSERT_NONNULL(mi), &v); }
    else { ADD_FAILURE(); }
    ZC_IF_SOME(v, mv(m)) { EXPECT_EQ(&ZC_ASSERT_NONNULL(mi), &v); }
    else { ADD_FAILURE(); }
    EXPECT_EQ(234, m.orDefault(456));
  }

  {
    Maybe<int> mi = zc::none;
    Maybe<int&> m = mi;
    EXPECT_TRUE(m == zc::none);
    ZC_IF_SOME(v, m) { ZC_FAIL_EXPECT(v); }
  }

  {
    const Maybe<int> mi = i;
    Maybe<const int&> m = mi;
    EXPECT_FALSE(m == zc::none);
    EXPECT_TRUE(m != zc::none);
    ZC_IF_SOME(v, m) { EXPECT_EQ(&ZC_ASSERT_NONNULL(mi), &v); }
    else { ADD_FAILURE(); }
    ZC_IF_SOME(v, mv(m)) { EXPECT_EQ(&ZC_ASSERT_NONNULL(mi), &v); }
    else { ADD_FAILURE(); }
    EXPECT_EQ(234, m.orDefault(456));
  }

  {
    const Maybe<int> mi = zc::none;
    Maybe<const int&> m = mi;
    EXPECT_TRUE(m == zc::none);
    ZC_IF_SOME(v, m) { ZC_FAIL_EXPECT(v); }
  }

  {
    // Verify orDefault() works with move-only types.
    Maybe<zc::String> m = zc::none;
    zc::String s = zc::mv(m).orDefault(zc::str("foo"));
    EXPECT_EQ("foo", s);
    EXPECT_EQ("foo", zc::mv(m).orDefault([] { return zc::str("foo"); }));
  }

  {
    // Test a case where an implicit conversion didn't used to happen correctly.
    Maybe<ImplicitToInt> m(ImplicitToInt{123});
    Maybe<uint> m2(m);
    Maybe<uint> m3(zc::mv(m));
    ZC_IF_SOME(v, m2) { EXPECT_EQ(123, v); }
    else { ADD_FAILURE(); }
    ZC_IF_SOME(v, m3) { EXPECT_EQ(123, v); }
    else { ADD_FAILURE(); }
  }

  {
    // Test usage of immovable types.
    Maybe<Immovable> m;
    ZC_EXPECT(m == zc::none);
    m.emplace();
    ZC_EXPECT(m != zc::none);
    m = zc::none;
    ZC_EXPECT(m == zc::none);
  }

  {
    // Test that initializing Maybe<T> from Maybe<T&>&& does a copy, not a move.
    CopyOrMove x(123);
    Maybe<CopyOrMove&> m(x);
    Maybe<CopyOrMove> m2 = zc::mv(m);
    ZC_EXPECT(m == zc::none);                   // m is moved out of and cleared
    ZC_EXPECT(x.i == 123);                      // but what m *referenced* was not moved out of
    ZC_EXPECT(ZC_ASSERT_NONNULL(m2).i == 123);  // m2 is a copy of what m referenced
  }

  {
    // Test that a moved-out-of Maybe<T> is left empty after move constructor.
    Maybe<int> m = 123;
    ZC_EXPECT(m != zc::none);

    Maybe<int> n(zc::mv(m));
    ZC_EXPECT(m == zc::none);
    ZC_EXPECT(n != zc::none);
  }

  {
    // Test that a moved-out-of Maybe<T> is left empty after move constructor.
    Maybe<int> m = 123;
    ZC_EXPECT(m != zc::none);

    Maybe<int> n = zc::mv(m);
    ZC_EXPECT(m == zc::none);
    ZC_EXPECT(n != zc::none);
  }

  {
    // Test that a moved-out-of Maybe<T&> is left empty when moved to a Maybe<T>.
    int x = 123;
    Maybe<int&> m = x;
    ZC_EXPECT(m != zc::none);

    Maybe<int> n(zc::mv(m));
    ZC_EXPECT(m == zc::none);
    ZC_EXPECT(n != zc::none);
  }

  {
    // Test that a moved-out-of Maybe<T&> is left empty when moved to another Maybe<T&>.
    int x = 123;
    Maybe<int&> m = x;
    ZC_EXPECT(m != zc::none);

    Maybe<int&> n(zc::mv(m));
    ZC_EXPECT(m == zc::none);
    ZC_EXPECT(n != zc::none);
  }

  {
    Maybe<int> m1 = 123;
    Maybe<int> m2 = 123;
    Maybe<int> m3 = 456;
    Maybe<int> m4 = zc::none;
    Maybe<int> m5 = zc::none;

    ZC_EXPECT(m1 == m2);
    ZC_EXPECT(m1 != m3);
    ZC_EXPECT(m1 != m4);
    ZC_EXPECT(m4 == m5);
    ZC_EXPECT(m4 != m1);
  }
}

TEST(Common, MaybeConstness) {
  int i;

  Maybe<int&> mi = i;
  const Maybe<int&> cmi = mi;
  //  const Maybe<int&> cmi2 = cmi;    // shouldn't compile!  Transitive const violation.

  ZC_IF_SOME(i2, cmi) { EXPECT_EQ(&i, &i2); }
  else { ADD_FAILURE(); }

  Maybe<const int&> mci = mi;
  const Maybe<const int&> cmci = mci;
  const Maybe<const int&> cmci2 = cmci;

  ZC_IF_SOME(i2, cmci2) { EXPECT_EQ(&i, &i2); }
  else { ADD_FAILURE(); }
}

#if __GNUC__
TEST(Common, MaybeUnwrapOrReturn) {
  {
    auto func = [](Maybe<int> i) -> int {
      int& j = ZC_UNWRAP_OR_RETURN(i, -1);
      ZC_EXPECT(&j == &ZC_ASSERT_NONNULL(i));
      return j + 2;
    };

    ZC_EXPECT(func(123) == 125);
    ZC_EXPECT(func(zc::none) == -1);
  }

  {
    auto func = [&](Maybe<String> maybe) -> int {
      String str = ZC_UNWRAP_OR_RETURN(zc::mv(maybe), -1);
      return str.parseAs<int>();
    };

    ZC_EXPECT(func(zc::str("123")) == 123);
    ZC_EXPECT(func(zc::none) == -1);
  }

  // Test void return.
  {
    int val = 0;
    auto func = [&](Maybe<int> i) { val = ZC_UNWRAP_OR_RETURN(i); };

    func(123);
    ZC_EXPECT(val == 123);
    val = 321;
    func(zc::none);
    ZC_EXPECT(val == 321);
  }

  // Test ZC_UNWRAP_OR
  {
    bool wasNull = false;
    auto func = [&](Maybe<int> i) -> int {
      int& j = ZC_UNWRAP_OR(i, {
        wasNull = true;
        return -1;
      });
      ZC_EXPECT(&j == &ZC_ASSERT_NONNULL(i));
      return j + 2;
    };

    ZC_EXPECT(func(123) == 125);
    ZC_EXPECT(!wasNull);
    ZC_EXPECT(func(zc::none) == -1);
    ZC_EXPECT(wasNull);
  }

  {
    bool wasNull = false;
    auto func = [&](Maybe<String> maybe) -> int {
      String str = ZC_UNWRAP_OR(zc::mv(maybe), {
        wasNull = true;
        return -1;
      });
      return str.parseAs<int>();
    };

    ZC_EXPECT(func(zc::str("123")) == 123);
    ZC_EXPECT(!wasNull);
    ZC_EXPECT(func(zc::none) == -1);
    ZC_EXPECT(wasNull);
  }

  // Test void return.
  {
    int val = 0;
    auto func = [&](Maybe<int> i) { val = ZC_UNWRAP_OR(i, { return; }); };

    func(123);
    ZC_EXPECT(val == 123);
    val = 321;
    func(zc::none);
    ZC_EXPECT(val == 321);
  }
}
#endif

class Foo {
public:
  ZC_DISALLOW_COPY_AND_MOVE(Foo);
  virtual ~Foo() {}

protected:
  Foo() = default;
};

class Bar : public Foo {
public:
  Bar() = default;
  ZC_DISALLOW_COPY_AND_MOVE(Bar);
  virtual ~Bar() {}
};

class Baz : public Foo {
public:
  Baz() = delete;
  ZC_DISALLOW_COPY_AND_MOVE(Baz);
  virtual ~Baz() {}
};

TEST(Common, Downcast) {
  Bar bar;
  Foo& foo = bar;

  EXPECT_EQ(&bar, &downcast<Bar>(foo));
#if defined(ZC_DEBUG) && !ZC_NO_RTTI
  ZC_EXPECT_THROW_MESSAGE("Value cannot be downcast", (void)downcast<Baz>(foo));
#endif

#if ZC_NO_RTTI
  EXPECT_TRUE(dynamicDowncastIfAvailable<Bar>(foo) == zc::none);
  EXPECT_TRUE(dynamicDowncastIfAvailable<Baz>(foo) == zc::none);
#else
  ZC_IF_SOME(m, dynamicDowncastIfAvailable<Bar>(foo)) { EXPECT_EQ(&bar, &m); }
  else { ZC_FAIL_ASSERT("Dynamic downcast returned null."); }
  EXPECT_TRUE(dynamicDowncastIfAvailable<Baz>(foo) == zc::none);
#endif
}

TEST(Common, MinMax) {
  EXPECT_EQ(5, zc::min(5, 9));
  EXPECT_EQ(5, zc::min(9, 5));
  EXPECT_EQ(5, zc::min(5, 5));
  EXPECT_EQ(9, zc::max(5, 9));
  EXPECT_EQ(9, zc::max(9, 5));
  EXPECT_EQ(5, zc::min(5, 5));

  // Hey look, we can handle the types mismatching.  Eat your heart out, std.
  EXPECT_EQ(5, zc::min(5, 'a'));
  EXPECT_EQ(5, zc::min('a', 5));
  EXPECT_EQ('a', zc::max(5, 'a'));
  EXPECT_EQ('a', zc::max('a', 5));

  EXPECT_EQ('a', zc::min(1234567890123456789ll, 'a'));
  EXPECT_EQ('a', zc::min('a', 1234567890123456789ll));
  EXPECT_EQ(1234567890123456789ll, zc::max(1234567890123456789ll, 'a'));
  EXPECT_EQ(1234567890123456789ll, zc::max('a', 1234567890123456789ll));
}

TEST(Common, MinMaxValue) {
  EXPECT_EQ(0x7f, int8_t(maxValue));
  EXPECT_EQ(0xffu, uint8_t(maxValue));
  EXPECT_EQ(0x7fff, int16_t(maxValue));
  EXPECT_EQ(0xffffu, uint16_t(maxValue));
  EXPECT_EQ(0x7fffffff, int32_t(maxValue));
  EXPECT_EQ(0xffffffffu, uint32_t(maxValue));
  EXPECT_EQ(0x7fffffffffffffffll, int64_t(maxValue));
  EXPECT_EQ(0xffffffffffffffffull, uint64_t(maxValue));

  EXPECT_EQ(-0x80, int8_t(minValue));
  EXPECT_EQ(0, uint8_t(minValue));
  EXPECT_EQ(-0x8000, int16_t(minValue));
  EXPECT_EQ(0, uint16_t(minValue));
  EXPECT_EQ(-0x80000000, int32_t(minValue));
  EXPECT_EQ(0, uint32_t(minValue));
  EXPECT_EQ(-0x8000000000000000ll, int64_t(minValue));
  EXPECT_EQ(0, uint64_t(minValue));

  double f = inf();
  EXPECT_TRUE(f * 2 == f);

  f = nan();
  EXPECT_FALSE(f == f);

  // `char`'s signedness is platform-specific.
  EXPECT_LE(char(minValue), '\0');
  EXPECT_GE(char(maxValue), '\x7f');
}

TEST(Common, Defer) {
  uint i = 0;
  uint j = 1;
  bool k = false;

  {
    ZC_DEFER(++i);
    ZC_DEFER(j += 3; k = true);
    EXPECT_EQ(0u, i);
    EXPECT_EQ(1u, j);
    EXPECT_FALSE(k);
  }

  EXPECT_EQ(1u, i);
  EXPECT_EQ(4u, j);
  EXPECT_TRUE(k);
}

TEST(Common, CanConvert) {
  static_assert(canConvert<long, int>(), "failure");
  static_assert(!canConvert<long, void*>(), "failure");

  struct Super {};
  struct Sub : public Super {};

  static_assert(canConvert<Sub, Super>(), "failure");
  static_assert(!canConvert<Super, Sub>(), "failure");
  static_assert(canConvert<Sub*, Super*>(), "failure");
  static_assert(!canConvert<Super*, Sub*>(), "failure");

  static_assert(canConvert<void*, const void*>(), "failure");
  static_assert(!canConvert<const void*, void*>(), "failure");
}

TEST(Common, ArrayAsBytes) {
  uint32_t raw[] = {0x12345678u, 0x9abcdef0u};

  ArrayPtr<uint32_t> array = raw;
  ASSERT_EQ(2, array.size());
  EXPECT_EQ(0x12345678u, array[0]);
  EXPECT_EQ(0x9abcdef0u, array[1]);

  {
    ArrayPtr<byte> bytes = array.asBytes();
    ASSERT_EQ(8, bytes.size());

    if (bytes[0] == '\x12') {
      // big-endian
      EXPECT_EQ(0x12u, bytes[0]);
      EXPECT_EQ(0x34u, bytes[1]);
      EXPECT_EQ(0x56u, bytes[2]);
      EXPECT_EQ(0x78u, bytes[3]);
      EXPECT_EQ(0x9au, bytes[4]);
      EXPECT_EQ(0xbcu, bytes[5]);
      EXPECT_EQ(0xdeu, bytes[6]);
      EXPECT_EQ(0xf0u, bytes[7]);
    } else {
      // little-endian
      EXPECT_EQ(0x12u, bytes[3]);
      EXPECT_EQ(0x34u, bytes[2]);
      EXPECT_EQ(0x56u, bytes[1]);
      EXPECT_EQ(0x78u, bytes[0]);
      EXPECT_EQ(0x9au, bytes[7]);
      EXPECT_EQ(0xbcu, bytes[6]);
      EXPECT_EQ(0xdeu, bytes[5]);
      EXPECT_EQ(0xf0u, bytes[4]);
    }
  }

  {
    ArrayPtr<char> chars = array.asChars();
    ASSERT_EQ(8, chars.size());

    if (chars[0] == '\x12') {
      // big-endian
      EXPECT_EQ('\x12', chars[0]);
      EXPECT_EQ('\x34', chars[1]);
      EXPECT_EQ('\x56', chars[2]);
      EXPECT_EQ('\x78', chars[3]);
      EXPECT_EQ('\x9a', chars[4]);
      EXPECT_EQ('\xbc', chars[5]);
      EXPECT_EQ('\xde', chars[6]);
      EXPECT_EQ('\xf0', chars[7]);
    } else {
      // little-endian
      EXPECT_EQ('\x12', chars[3]);
      EXPECT_EQ('\x34', chars[2]);
      EXPECT_EQ('\x56', chars[1]);
      EXPECT_EQ('\x78', chars[0]);
      EXPECT_EQ('\x9a', chars[7]);
      EXPECT_EQ('\xbc', chars[6]);
      EXPECT_EQ('\xde', chars[5]);
      EXPECT_EQ('\xf0', chars[4]);
    }
  }

  ArrayPtr<const uint32_t> constArray = array;

  {
    ArrayPtr<const byte> bytes = constArray.asBytes();
    ASSERT_EQ(8, bytes.size());

    if (bytes[0] == '\x12') {
      // big-endian
      EXPECT_EQ(0x12u, bytes[0]);
      EXPECT_EQ(0x34u, bytes[1]);
      EXPECT_EQ(0x56u, bytes[2]);
      EXPECT_EQ(0x78u, bytes[3]);
      EXPECT_EQ(0x9au, bytes[4]);
      EXPECT_EQ(0xbcu, bytes[5]);
      EXPECT_EQ(0xdeu, bytes[6]);
      EXPECT_EQ(0xf0u, bytes[7]);
    } else {
      // little-endian
      EXPECT_EQ(0x12u, bytes[3]);
      EXPECT_EQ(0x34u, bytes[2]);
      EXPECT_EQ(0x56u, bytes[1]);
      EXPECT_EQ(0x78u, bytes[0]);
      EXPECT_EQ(0x9au, bytes[7]);
      EXPECT_EQ(0xbcu, bytes[6]);
      EXPECT_EQ(0xdeu, bytes[5]);
      EXPECT_EQ(0xf0u, bytes[4]);
    }
  }

  {
    ArrayPtr<const char> chars = constArray.asChars();
    ASSERT_EQ(8, chars.size());

    if (chars[0] == '\x12') {
      // big-endian
      EXPECT_EQ('\x12', chars[0]);
      EXPECT_EQ('\x34', chars[1]);
      EXPECT_EQ('\x56', chars[2]);
      EXPECT_EQ('\x78', chars[3]);
      EXPECT_EQ('\x9a', chars[4]);
      EXPECT_EQ('\xbc', chars[5]);
      EXPECT_EQ('\xde', chars[6]);
      EXPECT_EQ('\xf0', chars[7]);
    } else {
      // little-endian
      EXPECT_EQ('\x12', chars[3]);
      EXPECT_EQ('\x34', chars[2]);
      EXPECT_EQ('\x56', chars[1]);
      EXPECT_EQ('\x78', chars[0]);
      EXPECT_EQ('\x9a', chars[7]);
      EXPECT_EQ('\xbc', chars[6]);
      EXPECT_EQ('\xde', chars[5]);
      EXPECT_EQ('\xf0', chars[4]);
    }
  }
}

enum TestOrdering {
  UNORDERED,
  EQUAL,
  LESS,
  GREATER,
  NOTEQUAL,
};

template <typename A, typename B>
void verifyEqualityComparisons(A a, B b, TestOrdering ord) {
  const bool expectedEq = ord == EQUAL;
  ZC_EXPECT((a == b) == expectedEq);
  ZC_EXPECT((b == a) == expectedEq);
  ZC_EXPECT((a != b) == !expectedEq);
  ZC_EXPECT((b != a) == !expectedEq);
}

template <typename T>
void strongComparisonsTests(T a, T b, TestOrdering ord) {
  const bool expectedEq = ord == EQUAL;
  const bool expectedLT = ord == LESS;
  verifyEqualityComparisons(a, b, ord);
  ZC_EXPECT((a <= b) == (expectedEq || expectedLT));
  ZC_EXPECT((b <= a) == !expectedLT);
  ZC_EXPECT((a >= b) == !expectedLT);
  ZC_EXPECT((b >= a) == (expectedEq || expectedLT));
  ZC_EXPECT((a < b) == expectedLT);
  ZC_EXPECT((b < a) == !(expectedEq || expectedLT));
  ZC_EXPECT((a > b) == !(expectedEq || expectedLT));
  ZC_EXPECT((b > a) == expectedLT);
}

template <typename A, typename B>
struct ArrayComparisonTest {
  Array<A> left;
  Array<B> right;
  TestOrdering expectedResult;
  ArrayComparisonTest(std::initializer_list<A> left, std::initializer_list<B> right,
                      TestOrdering expectedResult)
      : left(heapArray(left)), right(heapArray(right)), expectedResult(expectedResult) {}

  template <size_t N, size_t M>
  ArrayComparisonTest(A (&left)[N], B (&right)[M], TestOrdering expectedResult)
      : left(heapArray(left, N)), right(heapArray(right, M)), expectedResult(expectedResult) {}
};

ZC_TEST("ArrayPtr comparators for nullptr type") {
  verifyEqualityComparisons(ArrayPtr<const int>({}), nullptr, EQUAL);
  verifyEqualityComparisons(ArrayPtr<const int>({123}), nullptr, GREATER);
}

ZC_TEST("ArrayPtr comparators for same int type") {
  using Test = ArrayComparisonTest<const int, const int>;
  Test testCases[] = {
      {{1, 2}, {1, 2}, EQUAL},
      {{1, 2}, {1, 3}, LESS},
      {{1, 3}, {1, 2}, GREATER},
      {{1}, {1, 2}, LESS},
      {{2}, {1, 2}, GREATER},
      {{257, 258}, {257, 258}, EQUAL},
      {{0xFF, 0xFF}, {0x101, 0xFF}, LESS},
      {{0xFF, 0x101}, {0xFF, 0xFF}, GREATER},
      {{0xFF}, {0xFF, 0x101}, LESS},
      {{0x101}, {0xFF, 0x101}, GREATER},
      {{-1, -2}, {-1, -2}, EQUAL},
      {{-1, -3}, {-1, -2}, LESS},
      {{-1, -2}, {-1, -3}, GREATER},
      {{-1}, {-1, -2}, LESS},
      {{-1}, {-2, -3}, GREATER},
      {{-1, 1}, {-1, 1}, EQUAL},
      {{-1, -1}, {-1, 1}, LESS},
      {{-1, 1}, {-1, -1}, GREATER},
      {{-1}, {1, -2}, LESS},
      {{1}, {-1, 2}, GREATER},
  };

  for (auto const& testCase : testCases) {
    strongComparisonsTests(testCase.left.asPtr(), testCase.right.asPtr(), testCase.expectedResult);
  }
}
ZC_TEST("ArrayPtr comparators for same int type") {
  using Test = ArrayComparisonTest<const unsigned int, const unsigned int>;
  Test testCases[] = {
      {{1, 2}, {1, 2}, EQUAL},
      {{1, 2}, {1, 3}, LESS},
      {{1, 3}, {1, 2}, GREATER},
      {{1}, {1, 2}, LESS},
      {{2}, {1, 2}, GREATER},
      {{257, 258}, {257, 258}, EQUAL},
      {{0xFF, 0xFF}, {0x101, 0xFF}, LESS},
      {{0xFF, 0x101}, {0xFF, 0xFF}, GREATER},
      {{0xFF}, {0xFF, 0x101}, LESS},
      {{0x101}, {0xFF, 0x101}, GREATER},
      {{0x101}, {0xFF}, GREATER},
  };

  for (auto const& testCase : testCases) {
    strongComparisonsTests(testCase.left.asPtr(), testCase.right.asPtr(), testCase.expectedResult);
  }
}

ZC_TEST("ArrayPtr equality comparisons for different int type") {
  using Test = ArrayComparisonTest<const int, const short>;
  Test testCases[] = {
      {{1, 2}, {1, 2}, EQUAL}, {{1, 2}, {1, 3}, LESS}, {{1, 3}, {1, 2}, GREATER},
      {{1}, {1, 2}, LESS},     {{2}, {1, 2}, GREATER},
  };

  for (auto const& testCase : testCases) {
    verifyEqualityComparisons(testCase.left.asPtr(), testCase.right.asPtr(),
                              testCase.expectedResult);
  }
}

ZC_TEST("ArrayPtr comparators for doubles (testing partial orderings)") {
  using Test = ArrayComparisonTest<const double, const double>;
  const double d = nan();
  Test testCases[] = {
      {{0.0}, {0.0}, EQUAL},          {{1.0}, {0.0}, NOTEQUAL},      {{0.0}, {1.0}, NOTEQUAL},
      {{0, 0, 0.0}, {0.0}, NOTEQUAL}, {{0.0, 0.0}, {1.0}, NOTEQUAL}, {{d}, {d}, UNORDERED},
  };

  for (auto const& testCase : testCases) {
    verifyEqualityComparisons(testCase.left.asPtr(), testCase.right.asPtr(),
                              testCase.expectedResult);
  }
}

ZC_TEST("ArrayPtr comparator for arrays of the same string type") {
  using TestCase = ArrayComparisonTest<const StringPtr, const StringPtr>;
  TestCase testCases[] = {
      {{"foo", "bar"}, {"foo", "bar"}, EQUAL},
      {{"foo", "bar"}, {"foo", "baz"}, LESS},
      {{"foo", "bar"}, {"foo"}, GREATER},
  };

  for (auto const& testCase : testCases) {
    strongComparisonsTests(testCase.left.asPtr(), testCase.right.asPtr(), testCase.expectedResult);
  }
}

ZC_TEST("ArrayPtr equality comparisons for UTF-8") {
  using TestCase = ArrayComparisonTest<const char, const char>;

  TestCase testCases[] = {
      {"hello", "żółć", LESS},
  };

  for (auto const& testCase : testCases) {
    strongComparisonsTests(testCase.left.asPtr(), testCase.right.asPtr(), testCase.expectedResult);
    strongComparisonsTests(testCase.left.asBytes(), testCase.right.asBytes(),
                           testCase.expectedResult);
  }
}

ZC_TEST("ArrayPtr equality for arrays of different string types") {
  using Test = ArrayComparisonTest<const StringPtr, const char* const>;
  Test testCases[] = {
      {{"foo", "bar"}, {"foo", "bar"}, EQUAL},
      {{"foo", "bar"}, {"foo", "baz"}, LESS},
      {{"foo", "bar"}, {"foo"}, GREATER},
  };

  for (auto const& testCase : testCases) {
    verifyEqualityComparisons(testCase.left.asPtr(), testCase.right.asPtr(),
                              testCase.expectedResult);
  }
}

ZC_TEST("asBytes Tests") {
  const char helloMessage[] = "helloThere";

  // Use size to specify
  {
    auto helloPtr = zc::asBytes(helloMessage, 5);
    static_assert(isSameType<decltype(helloPtr), ArrayPtr<const byte>>());
    ZC_EXPECT(helloPtr.size(), 5);
    ZC_EXPECT(memcmp(helloPtr.begin(), helloMessage, 5) == 0);
  }

  // Use begin and end
  {
    auto helloPtr = zc::asBytes(helloMessage, helloMessage + 5);
    static_assert(isSameType<decltype(helloPtr), ArrayPtr<const byte>>());
    ZC_EXPECT(helloPtr.size(), 5);
    ZC_EXPECT(memcmp(helloPtr.begin(), helloMessage, 5) == 0);
  }

  // Check struct to ArrayPtr<byte>
  {
    struct Foo {
      size_t i = 0;
      size_t j = 1;
    };
    const Foo foo{};
    auto fooBytesPtr = asBytes(foo);
    static_assert(isSameType<decltype(fooBytesPtr), ArrayPtr<const byte>>());
    ZC_EXPECT(fooBytesPtr.size(), sizeof(Foo));
    ZC_EXPECT(memcmp(fooBytesPtr.begin(), &foo, sizeof(Foo)) == 0);
  }
  {
    const int simpleInts[] = {0, 100, 200, 300, -100};
    auto simpleIntsPtr = asBytes(simpleInts);
    static_assert(isSameType<decltype(simpleIntsPtr), ArrayPtr<const byte>>());
    ZC_EXPECT(simpleIntsPtr.size(), sizeof(simpleInts));
    ZC_EXPECT(memcmp(simpleIntsPtr.begin(), simpleInts, sizeof(simpleInts)) == 0);
  }
}

ZC_TEST("zc::range()") {
  uint expected = 5;
  for (uint i : range(5, 10)) { ZC_EXPECT(i == expected++); }
  ZC_EXPECT(expected == 10);

  expected = 0;
  for (uint i : range(0, 8)) { ZC_EXPECT(i == expected++); }
  ZC_EXPECT(expected == 8);
}

ZC_TEST("zc::defer()") {
  {
    // rvalue reference
    bool executed = false;
    {
      auto deferred = zc::defer([&executed]() { executed = true; });
      ZC_EXPECT(!executed);
    }

    ZC_EXPECT(executed);
  }

  {
    // lvalue reference
    bool executed = false;
    auto executor = [&executed]() { executed = true; };

    {
      auto deferred = zc::defer(executor);
      ZC_EXPECT(!executed);
    }

    ZC_EXPECT(executed);
  }

  {
    // Cancellation via `cancel()`.
    bool executed = false;
    {
      auto deferred = zc::defer([&executed]() { executed = true; });
      ZC_EXPECT(!executed);

      // Cancel and release the functor.
      deferred.cancel();
      ZC_EXPECT(!executed);
    }

    ZC_EXPECT(!executed);
  }

  {
    // Execution via `run()`.
    size_t runCount = 0;
    {
      auto deferred = zc::defer([&runCount]() { ++runCount; });

      // Run and release the functor.
      deferred.run();
      ZC_EXPECT(runCount == 1);
    }

    // `deferred` is already been run, so nothing is run when we destruct it.
    ZC_EXPECT(runCount == 1);
  }
}

ZC_TEST("zc::ArrayPtr startsWith / endsWith / findFirst / findLast") {
  // Note: char-/byte- optimized versions are covered by string-test.c++.

  int rawArray[] = {12, 34, 56, 34, 12};
  ArrayPtr<int> arr(rawArray);

  ZC_EXPECT(arr.startsWith({12, 34}));
  ZC_EXPECT(arr.startsWith({12, 34, 56}));
  ZC_EXPECT(!arr.startsWith({12, 34, 56, 78}));
  ZC_EXPECT(arr.startsWith({12, 34, 56, 34, 12}));
  ZC_EXPECT(!arr.startsWith({12, 34, 56, 34, 12, 12}));

  ZC_EXPECT(arr.endsWith({34, 12}));
  ZC_EXPECT(arr.endsWith({56, 34, 12}));
  ZC_EXPECT(!arr.endsWith({78, 56, 34, 12}));
  ZC_EXPECT(arr.endsWith({12, 34, 56, 34, 12}));
  ZC_EXPECT(!arr.endsWith({12, 12, 34, 56, 34, 12}));

  ZC_EXPECT(arr.findFirst(12).orDefault(100) == 0);
  ZC_EXPECT(arr.findFirst(34).orDefault(100) == 1);
  ZC_EXPECT(arr.findFirst(56).orDefault(100) == 2);
  ZC_EXPECT(arr.findFirst(78).orDefault(100) == 100);

  ZC_EXPECT(arr.findLast(12).orDefault(100) == 4);
  ZC_EXPECT(arr.findLast(34).orDefault(100) == 3);
  ZC_EXPECT(arr.findLast(56).orDefault(100) == 2);
  ZC_EXPECT(arr.findLast(78).orDefault(100) == 100);
}

ZC_TEST("zc::ArrayPtr fill") {
  int64_t int64Array[] = {12, 34, 56, 34, 12};
  arrayPtr(int64Array).fill(42);
  for (auto i : int64Array) { ZC_EXPECT(i == 42); }

  // test small sizes separately, since compilers do a memset optimization
  byte byteArray[256]{};
  arrayPtr(byteArray).fill(42);
  for (auto b : byteArray) { ZC_EXPECT(b == 42); }

  // test an object
  struct SomeObject {
    int64_t i;
    double d;
  };
  SomeObject objs[256];
  arrayPtr(objs).fill(SomeObject{42, 3.1415926});
  for (auto& o : objs) {
    ZC_EXPECT(o.i == 42);
    ZC_EXPECT(o.d == 3.1415926);
  }

  // test filling from an Array
  byte byteArray2[10]{};
  auto source = "abc"_zcb;
  arrayPtr(byteArray2).fill(source);
  ZC_EXPECT("abcabcabca"_zcb == byteArray2);
}

struct Std {
  template <typename T>
  static std::span<T> from(ArrayPtr<T>* arr) {
    return std::span<T>(arr->begin(), arr->size());
  }
};

ZC_TEST("ArrayPtr::as<Std>") {
  int rawArray[] = {12, 34, 56, 34, 12};
  ArrayPtr<int> arr(rawArray);
  std::span<int> stdPtr = arr.as<Std>();
  ZC_EXPECT(stdPtr.size() == 5);
}

ZC_TEST("ArrayPtr::copyFrom") {
  int arr1[] = {12, 34, 56, 34, 12};
  int arr2[] = {98, 67, 9, 22, 107};
  int arr3[] = {98, 67, 9, 22, 107};

  ZC_EXPECT(arrayPtr(arr1) != arrayPtr(arr2));
  ZC_EXPECT(arrayPtr(arr2) == arrayPtr(arr3));

  arrayPtr(arr1).copyFrom(arr2);
  ZC_EXPECT(arrayPtr(arr1) == arrayPtr(arr2));
  ZC_EXPECT(arrayPtr(arr2) == arrayPtr(arr3));
}

// Verifies the expected values of zc::isDisallowedInCoroutine<T>

struct DisallowedInCoroutineStruct {
  ZC_DISALLOW_AS_COROUTINE_PARAM;
};
class DisallowedInCoroutinePublic {
public:
  ZC_DISALLOW_AS_COROUTINE_PARAM;
};
class DisallowedInCoroutinePrivate {
private:
  ZC_DISALLOW_AS_COROUTINE_PARAM;
};
struct AllowedInCoroutine {};

static_assert(_::isDisallowedInCoroutine<DisallowedInCoroutineStruct>());
static_assert(_::isDisallowedInCoroutine<DisallowedInCoroutineStruct&>());
static_assert(_::isDisallowedInCoroutine<DisallowedInCoroutineStruct*>());
static_assert(_::isDisallowedInCoroutine<DisallowedInCoroutinePublic>());
static_assert(_::isDisallowedInCoroutine<DisallowedInCoroutinePublic&>());
static_assert(_::isDisallowedInCoroutine<DisallowedInCoroutinePublic*>());
static_assert(_::isDisallowedInCoroutine<DisallowedInCoroutinePrivate>());
static_assert(_::isDisallowedInCoroutine<DisallowedInCoroutinePrivate&>());
static_assert(_::isDisallowedInCoroutine<DisallowedInCoroutinePrivate*>());
static_assert(!_::isDisallowedInCoroutine<AllowedInCoroutine>());
static_assert(!_::isDisallowedInCoroutine<AllowedInCoroutine&>());
static_assert(!_::isDisallowedInCoroutine<AllowedInCoroutine*>());

ZC_TEST("_zcb") {
  {
    ArrayPtr<const byte> arr = "abc"_zcb;
    ZC_EXPECT(arr.size() == 3);
    ZC_EXPECT(arr[0] == 'a');
    ZC_EXPECT(arr[1] == 'b');
    ZC_EXPECT(arr[2] == 'c');
    ZC_EXPECT(arr == "abc"_zcb);
  }

  {
    // _zcb literals can be constexpr too
    constexpr ArrayPtr<const byte> arr2 = "def"_zcb;
    ZC_EXPECT(arr2.size() == 3);
    ZC_EXPECT(arr2[0] == 'd');
    ZC_EXPECT(arr2[1] == 'e');
    ZC_EXPECT(arr2[2] == 'f');
    ZC_EXPECT(arr2 == "def"_zcb);
  }

  // empty array
  ZC_EXPECT(""_zcb.size() == 0);
  ZC_EXPECT(""_zcb == nullptr);
}

ZC_TEST("arrayPtr()") {
  // arrayPtr can be used to create ArrayPtr from a fixed-size array without spelling out types
  byte buffer[1024]{};
  auto ptr = arrayPtr(buffer);
  ZC_EXPECT(ptr.size() == 1024);
}

ZC_TEST("single item arrayPtr()") {
  byte b = 42;
  ZC_EXPECT(arrayPtr(b).size() == 1);
  ZC_EXPECT(arrayPtr(b).begin() == &b);

  // test an object
  struct SomeObject {
    int64_t i;
    double d;
  };
  SomeObject obj = {42, 3.1415};
  zc::arrayPtr(obj).asBytes().fill(0);
  ZC_EXPECT(obj.i == 0);
  ZC_EXPECT(obj.d == 0);
}

ZC_TEST("memzero<T>()") {
  // memzero() works for primitive types
  int64_t x = 42;
  memzero(x);
  ZC_EXPECT(x == 0);

  // memzero() works for trivially constructible types
  struct ZeroTest {
    int64_t x;
    double pi;
  };
  ZeroTest t1;

  memzero(t1);
  ZC_EXPECT(t1.x == 0);
  ZC_EXPECT(t1.pi == 0.0);

  // memzero works on statically-sized arrays
  ZeroTest arr[256];
  memset(arr, 0xff, 256 * sizeof(ZeroTest));
  memzero(arr);
  for (auto& t : arr) { ZC_EXPECT(t.pi == 0); }
}

}  // namespace
}  // namespace zc
