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

#pragma once
// This file defines compatibility macros converting Google Test tests into ZC tests.
//
// This is only intended to cover the most common functionality. Many tests will likely need
// additional tweaks. For instance:
// - Using operator<< to print information on failure is not supported. Instead, switch to
//   ZC_ASSERT/ZC_EXPECT and pass in stuff to print as additional parameters.
// - Test fixtures are not supported. Allocate your "test fixture" on the stack instead. Do setup
//   in the constructor, teardown in the destructor.

#include <zc/core/windows-sanity.h>  // work-around macro conflict with `ERROR`
#include <zc/ztest/test.h>

ZC_BEGIN_HEADER

namespace zc {

namespace _ {  // private

template <typename T>
T abs(T value) {
  return value < 0 ? -value : value;
}

inline bool floatAlmostEqual(float a, float b) {
  return a == b || abs(a - b) < (abs(a) + abs(b)) * 1e-5;
}

inline bool doubleAlmostEqual(double a, double b) {
  return a == b || abs(a - b) < (abs(a) + abs(b)) * 1e-12;
}

}  // namespace _

#define EXPECT_FALSE(x) ZC_EXPECT(!(x))
#define EXPECT_TRUE(x) ZC_EXPECT(x)
#define EXPECT_EQ(x, y) ZC_EXPECT((x) == (y), x, y)
#define EXPECT_NE(x, y) ZC_EXPECT((x) != (y), x, y)
#define EXPECT_LE(x, y) ZC_EXPECT((x) <= (y), x, y)
#define EXPECT_GE(x, y) ZC_EXPECT((x) >= (y), x, y)
#define EXPECT_LT(x, y) ZC_EXPECT((x) < (y), x, y)
#define EXPECT_GT(x, y) ZC_EXPECT((x) > (y), x, y)
#define EXPECT_STREQ(x, y) ZC_EXPECT(::strcmp(x, y) == 0, x, y)
#define EXPECT_FLOAT_EQ(x, y) ZC_EXPECT(::zc::_::floatAlmostEqual(y, x), y, x);
#define EXPECT_DOUBLE_EQ(x, y) ZC_EXPECT(::zc::_::doubleAlmostEqual(y, x), y, x);

#define ASSERT_FALSE(x) ZC_ASSERT(!(x))
#define ASSERT_TRUE(x) ZC_ASSERT(x)
#define ASSERT_EQ(x, y) ZC_ASSERT((x) == (y), x, y)
#define ASSERT_NE(x, y) ZC_ASSERT((x) != (y), x, y)
#define ASSERT_LE(x, y) ZC_ASSERT((x) <= (y), x, y)
#define ASSERT_GE(x, y) ZC_ASSERT((x) >= (y), x, y)
#define ASSERT_LT(x, y) ZC_ASSERT((x) < (y), x, y)
#define ASSERT_GT(x, y) ZC_ASSERT((x) > (y), x, y)
#define ASSERT_STREQ(x, y) ZC_ASSERT(::strcmp(x, y) == 0, x, y)
#define ASSERT_FLOAT_EQ(x, y) ZC_ASSERT(::zc::_::floatAlmostEqual(y, x), y, x);
#define ASSERT_DOUBLE_EQ(x, y) ZC_ASSERT(::zc::_::doubleAlmostEqual(y, x), y, x);

class AddFailureAdapter {
public:
  AddFailureAdapter(const char* file, int line) : file(file), line(line) {}

  ~AddFailureAdapter() {
    if (!handled) { _::Debug::log(file, line, LogSeverity::ERROR, "expectation failed"); }
  }

  template <typename T>
  void operator<<(T&& info) {
    handled = true;
    _::Debug::log(file, line, LogSeverity::ERROR, "\"expectation failed\", info",
                  "expectation failed", zc::fwd<T>(info));
  }

private:
  bool handled = false;
  const char* file;
  int line;
};

#define ADD_FAILURE() ::zc::AddFailureAdapter(__FILE__, __LINE__)

#define EXPECT_ANY_THROW(code) ZC_EXPECT(::zc::runCatchingExceptions([&]() { code; }) != zc::none)

#define EXPECT_NONFATAL_FAILURE(code) \
  EXPECT_TRUE(zc::runCatchingExceptions([&]() { code; }) != zc::none);

#ifdef ZC_DEBUG
#define EXPECT_DEBUG_ANY_THROW EXPECT_ANY_THROW
#else
#define EXPECT_DEBUG_ANY_THROW(EXP)
#endif

#define TEST(x, y) ZC_TEST("legacy test: " #x "/" #y)

}  // namespace zc

ZC_END_HEADER
