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

#include "zc/core/exception.h"

#include <stdint.h>
#include <zc/ztest/gtest.h>

#include <stdexcept>

#include "zc/core/debug.h"

namespace zc {
namespace _ {  // private
namespace {

TEST(Exception, TrimSourceFilename) {
#if _WIN32
  EXPECT_TRUE(trimSourceFilename(__FILE__) == "unit/zc/core/exception-test.cc" ||
              trimSourceFilename(__FILE__) == "unit\\zc\\core\\exception-test.cc");
#else
  EXPECT_EQ(trimSourceFilename(__FILE__), "unit/zc/core/exception-test.cc");
#endif
}

TEST(Exception, RunCatchingExceptions) {
  bool recovered = false;
  Maybe<Exception> e = zc::runCatchingExceptions([&]() {
    ZC_FAIL_ASSERT("foo") { break; }
    recovered = true;
  });

  EXPECT_FALSE(recovered);

  ZC_IF_SOME(ex, e) { EXPECT_EQ("foo", ex.getDescription()); }
  else { ADD_FAILURE() << "Expected exception"; }
}

TEST(Exception, RunCatchingExceptionsStdException) {
  Maybe<Exception> e = zc::runCatchingExceptions([&]() { throw std::logic_error("foo"); });

  ZC_IF_SOME(ex, e) { EXPECT_EQ("std::exception: foo", ex.getDescription()); }
  else { ADD_FAILURE() << "Expected exception"; }
}

TEST(Exception, RunCatchingExceptionsOtherException) {
  Maybe<Exception> e = zc::runCatchingExceptions([&]() { throw 123; });

  ZC_IF_SOME(ex, e) {
#if __GNUC__ && !ZC_NO_RTTI
    EXPECT_EQ("unknown non-ZC exception of type: int", ex.getDescription());
#else
    EXPECT_EQ("unknown non-ZC exception", ex.getDescription());
#endif
  }
  else { ADD_FAILURE() << "Expected exception"; }
}

class ThrowingDestructor : public UnwindDetector {
public:
  ~ThrowingDestructor() noexcept(false) {
    catchExceptionsIfUnwinding([]() { ZC_FAIL_ASSERT("this is a test, not a real bug"); });
  }
};

TEST(Exception, UnwindDetector) {
  // If no other exception is happening, ThrowingDestructor's destructor throws one.
  Maybe<Exception> e = zc::runCatchingExceptions([&]() { ThrowingDestructor t; });

  ZC_IF_SOME(ex, e) { EXPECT_EQ("this is a test, not a real bug", ex.getDescription()); }
  else { ADD_FAILURE() << "Expected exception"; }

  // If another exception is happening, ThrowingDestructor's destructor's exception is squelched.
  e = zc::runCatchingExceptions([&]() {
    ThrowingDestructor t;
    ZC_FAIL_ASSERT("baz") { break; }
  });

  ZC_IF_SOME(ex, e) { EXPECT_EQ("baz", ex.getDescription()); }
  else { ADD_FAILURE() << "Expected exception"; }
}

#if defined(FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION) || \
    ZC_HAS_COMPILER_FEATURE(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
// The implementation skips this check in these cases.
#else
#if !__MINGW32__  // Inexplicably crashes when exception is thrown from constructor.
TEST(Exception, ExceptionCallbackMustBeOnStack) {
  ZC_EXPECT_THROW_MESSAGE("must be allocated on the stack", new ExceptionCallback);
}
#endif
#endif  // !__MINGW32__

TEST(Exception, ScopeSuccessFail) {
  bool success = false;
  bool failure = false;

  {
    ZC_ON_SCOPE_SUCCESS(success = true);
    ZC_ON_SCOPE_FAILURE(failure = true);

    EXPECT_FALSE(success);
    EXPECT_FALSE(failure);
  }

  EXPECT_TRUE(success);
  EXPECT_FALSE(failure);

  success = false;
  failure = false;

  try {
    ZC_ON_SCOPE_SUCCESS(success = true);
    ZC_ON_SCOPE_FAILURE(failure = true);

    EXPECT_FALSE(success);
    EXPECT_FALSE(failure);

    throw 1;
  } catch (int) {}

  EXPECT_FALSE(success);
  EXPECT_TRUE(failure);
}

#if __GNUG__ || defined(__clang__)
zc::String testStackTrace() __attribute__((noinline));
#elif _MSC_VER
__declspec(noinline) zc::String testStackTrace();
#endif

zc::String testStackTrace() {
  // getStackTrace() normally skips its immediate caller, so we wrap it in another layer.
  return getStackTrace();
}

ZC_TEST("getStackTrace() returns correct line number, not line + 1") {
  // Backtraces normally produce the return address of each stack frame, but that's usually the
  // address immediately after the one that made the call. As a result, it used to be that stack
  // traces often pointed to the line after the one that made a call, which was confusing. This
  // checks that this bug is fixed.
  //
  // This is not a very robust test, because:
  // 1) Since symbolic stack traces are not available in many situations (e.g. release builds
  //    lacking debug symbols, systems where addr2line isn't present, etc.), we only check that
  //    the stack trace does *not* contain the *wrong* value, rather than checking that it does
  //    contain the right one.
  // 2) This test only detects the problem if the call instruction to testStackTrace() is the
  //    *last* instruction attributed to its line of code. Whether or not this is true seems to be
  //    dependent on obscure compiler behavior. For example, below, it could only be the case if
  //    RVO is applied -- but in my testing, RVO does seem to be applied here. I tried several
  //    variations involving passing via an output parameter or a global variable rather than
  //    returning, but found some variations detected the problem and others didn't, essentially
  //    at random.

  auto trace = testStackTrace();
  auto wrong = zc::str("exception-test.cc:", __LINE__);

  ZC_ASSERT(!trace.contains(wrong), trace, wrong);
}

ZC_TEST("InFlightExceptionIterator works") {
  bool caught = false;
  try {
    ZC_DEFER({
      try {
        ZC_FAIL_ASSERT("bar");
      } catch (const zc::Exception& e) {
        InFlightExceptionIterator iter;
        ZC_IF_SOME(e2, iter.next()) { ZC_EXPECT(&e2 == &e, e2.getDescription()); }
        else { ZC_FAIL_EXPECT("missing first exception"); }

        ZC_IF_SOME(e2, iter.next()) {
          ZC_EXPECT(e2.getDescription() == "foo", e2.getDescription());
        }
        else { ZC_FAIL_EXPECT("missing second exception"); }

        ZC_EXPECT(iter.next() == zc::none, "more than two exceptions");

        caught = true;
      }
    });
    ZC_FAIL_ASSERT("foo");
  } catch (const zc::Exception& e) {
    // expected
  }

  ZC_EXPECT(caught);
}

ZC_TEST("computeRelativeTrace") {
  auto testCase = [](uint expectedPrefix, ArrayPtr<const uintptr_t> trace,
                     ArrayPtr<const uintptr_t> relativeTo) {
    auto tracePtr = ZC_MAP(x, trace) { return (void*)x; };
    auto relativeToPtr = ZC_MAP(x, relativeTo) { return (void*)x; };

    auto result = computeRelativeTrace(tracePtr, relativeToPtr);
    ZC_EXPECT(result.begin() == tracePtr.begin());

    ZC_EXPECT(result.size() == expectedPrefix, trace, relativeTo, result);
  };

  testCase(8, {1, 2, 3, 4, 5, 6, 7, 8}, {8, 7, 6, 5, 4, 3, 2, 1});

  testCase(5, {1, 2, 3, 4, 5, 6, 7, 8}, {8, 7, 6, 5, 5, 6, 7, 8});

  testCase(5, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, {8, 7, 6, 5, 5, 6, 7, 8});

  testCase(5, {1, 2, 3, 4, 5, 6, 7, 8, 6, 7, 8}, {8, 7, 6, 5, 5, 6, 7, 8});

  testCase(9, {1, 2, 3, 4, 5, 6, 7, 8, 5, 5, 6, 7, 8}, {8, 7, 6, 5, 5, 6, 7, 8});

  testCase(5, {1, 2, 3, 4, 5, 5, 6, 7, 8, 5, 6, 7, 8}, {8, 7, 6, 5, 5, 6, 7, 8});

  testCase(5, {1, 2, 3, 4, 5, 6, 7, 8}, {8, 7, 6, 5, 5, 6, 7, 8, 7, 8});

  testCase(5, {1, 2, 3, 4, 5, 6, 7, 8}, {8, 7, 6, 5, 6, 7, 8, 7, 8});
}

ZC_TEST("exception details") {
  zc::Exception e = ZC_EXCEPTION(FAILED, "foo");

  e.setDetail(123, zc::heapArray("foo"_zcb));
  e.setDetail(456, zc::heapArray("bar"_zcb));

  ZC_EXPECT(zc::str(ZC_ASSERT_NONNULL(e.getDetail(123)).asChars()) == "foo");
  ZC_EXPECT(zc::str(ZC_ASSERT_NONNULL(e.getDetail(456)).asChars()) == "bar");
  ZC_EXPECT(e.getDetail(789) == zc::none);

  zc::Exception e2 = zc::cp(e);
  ZC_EXPECT(zc::str(ZC_ASSERT_NONNULL(e2.getDetail(123)).asChars()) == "foo");
  ZC_EXPECT(zc::str(ZC_ASSERT_NONNULL(e2.getDetail(456)).asChars()) == "bar");
  ZC_EXPECT(e2.getDetail(789) == zc::none);

  ZC_EXPECT(zc::str(ZC_ASSERT_NONNULL(e2.releaseDetail(123)).asChars()) == "foo");
  ZC_EXPECT(e2.getDetail(123) == zc::none);
  ZC_EXPECT(zc::str(ZC_ASSERT_NONNULL(e2.getDetail(456)).asChars()) == "bar");
}

}  // namespace
}  // namespace _
}  // namespace zc
