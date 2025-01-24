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

#include "zc/core/one-of.h"

#include <zc/ztest/gtest.h>

#include "zc/core/string.h"

namespace zc {

TEST(OneOf, Basic) {
  OneOf<int, float, String> var;

  EXPECT_FALSE(var.is<int>());
  EXPECT_FALSE(var.is<float>());
  EXPECT_FALSE(var.is<String>());
  EXPECT_TRUE(var.tryGet<int>() == zc::none);
  EXPECT_TRUE(var.tryGet<float>() == zc::none);
  EXPECT_TRUE(var.tryGet<String>() == zc::none);

  var.init<int>(123);

  EXPECT_TRUE(var.is<int>());
  EXPECT_FALSE(var.is<float>());
  EXPECT_FALSE(var.is<String>());

  EXPECT_EQ(123, var.get<int>());
#ifdef ZC_DEBUG
  EXPECT_ANY_THROW(var.get<float>());
  EXPECT_ANY_THROW(var.get<String>());
#endif

  EXPECT_EQ(123, ZC_ASSERT_NONNULL(var.tryGet<int>()));
  EXPECT_TRUE(var.tryGet<float>() == zc::none);
  EXPECT_TRUE(var.tryGet<String>() == zc::none);

  var.init<String>(zc::str("foo"));

  EXPECT_FALSE(var.is<int>());
  EXPECT_FALSE(var.is<float>());
  EXPECT_TRUE(var.is<String>());

  EXPECT_EQ("foo", var.get<String>());

  EXPECT_TRUE(var.tryGet<int>() == zc::none);
  EXPECT_TRUE(var.tryGet<float>() == zc::none);
  EXPECT_EQ("foo", ZC_ASSERT_NONNULL(var.tryGet<String>()));

  OneOf<int, float, String> var2 = zc::mv(var);
  EXPECT_EQ("", var.get<String>());
  EXPECT_EQ("foo", var2.get<String>());

  var = zc::mv(var2);
  EXPECT_EQ("foo", var.get<String>());
  EXPECT_EQ("", var2.get<String>());

  auto canCompile ZC_UNUSED = [&]() {
    var.allHandled<3>();
    // var.allHandled<2>();  // doesn't compile
  };
}

TEST(OneOf, Copy) {
  OneOf<int, float, const char*> var;

  OneOf<int, float, const char*> var2 = var;
  EXPECT_FALSE(var2.is<int>());
  EXPECT_FALSE(var2.is<float>());
  EXPECT_FALSE(var2.is<const char*>());

  var.init<int>(123);

  var2 = var;
  EXPECT_TRUE(var2.is<int>());
  EXPECT_EQ(123, var2.get<int>());

  var.init<const char*>("foo");

  var2 = var;
  EXPECT_TRUE(var2.is<const char*>());
  EXPECT_STREQ("foo", var2.get<const char*>());
}

TEST(OneOf, Switch) {
  OneOf<int, float, const char*> var;
  var = "foo";
  uint count = 0;

  {
    ZC_SWITCH_ONEOF(var) {
      ZC_CASE_ONEOF(i, int) { ZC_FAIL_ASSERT("expected char*, got int", i); }
      ZC_CASE_ONEOF(s, const char*) {
        ZC_EXPECT(zc::StringPtr(s) == "foo");
        ++count;
      }
      ZC_CASE_ONEOF(n, float) { ZC_FAIL_ASSERT("expected char*, got float", n); }
    }
  }

  ZC_EXPECT(count == 1);

  {
    ZC_SWITCH_ONEOF(zc::cp(var)) {
      ZC_CASE_ONEOF(i, int) { ZC_FAIL_ASSERT("expected char*, got int", i); }
      ZC_CASE_ONEOF(s, const char*) { ZC_EXPECT(zc::StringPtr(s) == "foo"); }
      ZC_CASE_ONEOF(n, float) { ZC_FAIL_ASSERT("expected char*, got float", n); }
    }
  }

  {
    // At one time this failed to compile.
    const auto& constVar = var;
    ZC_SWITCH_ONEOF(constVar) {
      ZC_CASE_ONEOF(i, int) { ZC_FAIL_ASSERT("expected char*, got int", i); }
      ZC_CASE_ONEOF(s, const char*) { ZC_EXPECT(zc::StringPtr(s) == "foo"); }
      ZC_CASE_ONEOF(n, float) { ZC_FAIL_ASSERT("expected char*, got float", n); }
    }
  }
}

TEST(OneOf, Maybe) {
  Maybe<OneOf<int, float>> var;
  var = OneOf<int, float>(123);

  ZC_IF_SOME(v, var) {
    // At one time this failed to compile. Note that a Maybe<OneOf<...>> isn't necessarily great
    // style -- you might be better off with an explicit OneOf<Empty, ...>. Nevertheless, it should
    // compile.
    ZC_SWITCH_ONEOF(v) {
      ZC_CASE_ONEOF(i, int) { ZC_EXPECT(i == 123); }
      ZC_CASE_ONEOF(n, float) { ZC_FAIL_ASSERT("expected int, got float", n); }
    }
  }
}

ZC_TEST("OneOf copy/move from alternative variants") {
  {
    // Test const copy.
    const OneOf<int, float> src = 23.5f;
    OneOf<int, bool, float> dst = src;
    ZC_ASSERT(dst.is<float>());
    ZC_EXPECT(dst.get<float>() == 23.5);
  }

  {
    // Test case that requires non-const copy.
    int arr[3] = {1, 2, 3};
    OneOf<int, ArrayPtr<int>> src = ArrayPtr<int>(arr);
    OneOf<int, bool, ArrayPtr<int>> dst = src;
    ZC_ASSERT(dst.is<ArrayPtr<int>>());
    ZC_EXPECT(dst.get<ArrayPtr<int>>().begin() == arr);
    ZC_EXPECT(dst.get<ArrayPtr<int>>().size() == zc::size(arr));
  }

  {
    // Test move.
    OneOf<int, String> src = zc::str("foo");
    OneOf<int, bool, String> dst = zc::mv(src);
    ZC_ASSERT(dst.is<String>());
    ZC_EXPECT(dst.get<String>() == "foo");

    String s = zc::mv(dst).get<String>();
    ZC_EXPECT(s == "foo");
  }

  {
    // We can still have nested OneOfs.
    OneOf<int, float> src = 23.5f;
    OneOf<bool, OneOf<int, float>> dst = src;
    ZC_ASSERT((dst.is<OneOf<int, float>>()));
    ZC_ASSERT((dst.get<OneOf<int, float>>().is<float>()));
    ZC_EXPECT((dst.get<OneOf<int, float>>().get<float>() == 23.5));
  }
}

template <unsigned int N>
struct T {
  unsigned int n = N;
};

TEST(OneOf, MaxVariants) {
  zc::OneOf<T<1>, T<2>, T<3>, T<4>, T<5>, T<6>, T<7>, T<8>, T<9>, T<10>, T<11>, T<12>, T<13>, T<14>,
            T<15>, T<16>, T<17>, T<18>, T<19>, T<20>, T<21>, T<22>, T<23>, T<24>, T<25>, T<26>,
            T<27>, T<28>, T<29>, T<30>, T<31>, T<32>, T<33>, T<34>, T<35>, T<36>, T<37>, T<38>,
            T<39>, T<40>, T<41>, T<42>, T<43>, T<44>, T<45>, T<46>, T<47>, T<48>, T<49>, T<50>>
      v;

  v = T<1>();
  EXPECT_TRUE(v.is<T<1>>());

  v = T<50>();
  EXPECT_TRUE(v.is<T<50>>());
}

}  // namespace zc
