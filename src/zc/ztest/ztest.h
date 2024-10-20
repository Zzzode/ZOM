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

#ifndef ZC_ZTEST_ZTEST_H_
#define ZC_ZTEST_ZTEST_H_

#include "src/zc/base/debug.h"
#include "src/zc/base/windows_sanity.h"  // work-around macro conflict with `ERROR`

ZC_BEGIN_HEADER

namespace zc {
template <typename T>
class Function;

template <typename T>
class FunctionParam;

class TestRunner;

class TestCase {
public:
  TestCase(const char* file, uint line, const char* description);
  ~TestCase();

  virtual void run() = 0;

protected:
  template <typename Func>
  void doBenchmark(Func&& func) {
    // Perform a benchmark with configurable iterations. func() will be called N
    // times, where N is set by the --benchmark CLI flag. This defaults to 1, so
    // that when --benchmark is not specified, we only test that the benchmark
    // works.
    //
    // In the future, this could adaptively choose iteration count by running a
    // few iterations to find out how fast the benchmark is, then scaling.

    for (size_t i = iterCount(); i-- > 0;) { func(); }
  }

private:
  const char* file;
  uint line;
  const char* description;
  TestCase* next;
  TestCase** prev;
  bool matchedFilter;

  static size_t iterCount();

  friend class TestRunner;
};

#define ZC_TEST(description)                                                         \
  /* Make sure the linker fails if tests are not in anonymous namespaces. */         \
  extern int ZC_CONCAT(YouMustWrapTestsInAnonymousNamespace, __COUNTER__) ZC_UNUSED; \
  class ZC_UNIQUE_NAME(TestCase) : public ::zc::TestCase {                           \
  public:                                                                            \
    ZC_UNIQUE_NAME(TestCase)                                                         \
    () : ::zc::TestCase(__FILE__, __LINE__, description) {}                          \
    void run() override;                                                             \
  } ZC_UNIQUE_NAME(testCase);                                                        \
  void ZC_UNIQUE_NAME(TestCase)::run()

#if ZC_MSVC_TRADITIONAL_CPP
#define ZC_INDIRECT_EXPAND(m, vargs) m vargs
#define ZC_FAIL_EXPECT(...) ZC_INDIRECT_EXPAND(ZC_LOG, (ERROR, __VA_ARGS__));
#define ZC_EXPECT(cond, ...)                             \
  if (auto _zcCondition = ::zc::_::MAGIC_ASSERT << cond) \
    ;                                                    \
  else                                                   \
    ZC_INDIRECT_EXPAND(ZC_FAIL_EXPECT, ("failed: expected " #cond, _zcCondition, __VA_ARGS__))
#else
#define ZC_FAIL_EXPECT(...) ZC_LOG(ERROR, ##__VA_ARGS__);
#define ZC_EXPECT(cond, ...)                             \
  if (auto _zcCondition = ::zc::_::MAGIC_ASSERT << cond) \
    ;                                                    \
  else                                                   \
    ZC_FAIL_EXPECT("failed: expected " #cond, _zcCondition, ##__VA_ARGS__)
#endif

// TODO(msvc): cast results to void like non-MSVC versions do
#if _MSC_VER && !defined(__clang__)
#define ZC_EXPECT_THROW_RECOVERABLE(type, code, ...)                                              \
  do {                                                                                            \
    ZC_IF_SOME(e, ::zc::runCatchingExceptions([&]() { code; })) {                                 \
      ZC_INDIRECT_EXPAND(ZC_EXPECT, (e.getType() == ::zc::Exception::Type::type,                  \
                                     "code threw wrong exception type: " #code, e, __VA_ARGS__)); \
    }                                                                                             \
    else { ZC_INDIRECT_EXPAND(ZC_FAIL_EXPECT, ("code did not throw: " #code, __VA_ARGS__)); }     \
  } while (false)

#define ZC_EXPECT_THROW_RECOVERABLE_MESSAGE(message, code, ...)                                    \
  do {                                                                                             \
    ZC_IF_SOME(e, ::zc::runCatchingExceptions([&]() { code; })) {                                  \
      ZC_INDIRECT_EXPAND(                                                                          \
          ZC_EXPECT, (e.getDescription().contains(message),                                        \
                      "exception description didn't contain expected substring", e, __VA_ARGS__)); \
    }                                                                                              \
    else { ZC_INDIRECT_EXPAND(ZC_FAIL_EXPECT, ("code did not throw: " #code, __VA_ARGS__)); }      \
  } while (false)
#else
#define ZC_EXPECT_THROW_RECOVERABLE(type, code, ...)                           \
  do {                                                                         \
    ZC_IF_SOME(e, ::zc::runCatchingExceptions([&]() { (void)({ code; }); })) { \
      ZC_EXPECT(e.getType() == ::zc::Exception::Type::type,                    \
                "code threw wrong exception type: " #code, e, ##__VA_ARGS__);  \
    }                                                                          \
    else { ZC_FAIL_EXPECT("code did not throw: " #code, ##__VA_ARGS__); }      \
  } while (false)

#define ZC_EXPECT_THROW_RECOVERABLE_MESSAGE(message, code, ...)                               \
  do {                                                                                        \
    ZC_IF_SOME(e, ::zc::runCatchingExceptions([&]() { (void)({ code; }); })) {                \
      ZC_EXPECT(e.getDescription().contains(message),                                         \
                "exception description didn't contain expected substring", e, ##__VA_ARGS__); \
    }                                                                                         \
    else { ZC_FAIL_EXPECT("code did not throw: " #code, ##__VA_ARGS__); }                     \
  } while (false)
#endif

#define ZC_EXPECT_THROW ZC_EXPECT_THROW_RECOVERABLE
#define ZC_EXPECT_THROW_MESSAGE ZC_EXPECT_THROW_RECOVERABLE_MESSAGE

#define ZC_EXPECT_EXIT(statusCode, code)                         \
  do {                                                           \
    ZC_EXPECT(::zc::_::expectExit(statusCode, [&]() { code; })); \
  } while (false)
// Forks the code and expects it to exit with a given code.

#define ZC_EXPECT_SIGNAL(signal, code)                         \
  do {                                                         \
    ZC_EXPECT(::zc::_::expectSignal(signal, [&]() { code; })); \
  } while (false)
// Forks the code and expects it to trigger a signal.
// In the child resets all signal handlers as printStackTraceOnCrash sets.

#define ZC_EXPECT_LOG(level, substring) \
  ::zc::_::LogExpectation ZC_UNIQUE_NAME(_zcLogExpectation)(::zc::LogSeverity::level, substring)
// Expects that a log message with the given level and substring text will be
// printed within the current scope. This message will not cause the test to
// fail, even if it is an error.

// =======================================================================================

namespace _ {  // private

bool expectExit(Maybe<int> statusCode, FunctionParam<void()> code) noexcept;
// Expects that the given code will exit with a given statusCode.
// The test will fork() and run in a subprocess. On Windows, where fork() is not
// available, this always returns true.

bool expectSignal(Maybe<int> signal, FunctionParam<void()> code) noexcept;
// Expects that the given code will trigger a signal.
// The test will fork() and run in a subprocess. On Windows, where fork() is not
// available, this always returns true. Resets signal handlers to default prior
// to running the code in the child process.

class LogExpectation : public ExceptionCallback {
public:
  LogExpectation(LogSeverity severity, StringPtr substring);
  ~LogExpectation();

  void logMessage(LogSeverity severity, const char* file, int line, int contextDepth,
                  String&& text) override;

private:
  LogSeverity severity;
  StringPtr substring;
  bool seen;
  UnwindDetector unwindDetector;
};

}  // namespace _
}  // namespace zc

ZC_END_HEADER

#endif  // ZC_ZTEST_ZTEST_H_