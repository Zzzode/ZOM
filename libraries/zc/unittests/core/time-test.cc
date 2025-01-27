// Copyright (c) 2019 Cloudflare, Inc. and contributors
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

#if _WIN32
#include "zc/core/win32-api-version.h"
#endif

#include <zc/core/time.h>
#include <zc/ztest/test.h>

#include "zc/core/debug.h"
#include "zc/core/time.h"

#if _WIN32
#include <windows.h>

#include "zc/core/windows-sanity.h"
#else
#include <unistd.h>
#endif

namespace zc {
namespace {

ZC_TEST("stringify times") {
  ZC_EXPECT(zc::str(50 * zc::SECONDS) == "50s");
  ZC_EXPECT(zc::str(5 * zc::SECONDS + 2 * zc::MILLISECONDS) == "5.002s");
  ZC_EXPECT(zc::str(256 * zc::MILLISECONDS) == "256ms");
  ZC_EXPECT(zc::str(5 * zc::MILLISECONDS + 2 * zc::NANOSECONDS) == "5.000002ms");
  ZC_EXPECT(zc::str(50 * zc::MICROSECONDS) == "50μs");
  ZC_EXPECT(zc::str(5 * zc::MICROSECONDS + 300 * zc::NANOSECONDS) == "5.3μs");
  ZC_EXPECT(zc::str(50 * zc::NANOSECONDS) == "50ns");
  ZC_EXPECT(zc::str(-256 * zc::MILLISECONDS) == "-256ms");
  ZC_EXPECT(zc::str(-50 * zc::NANOSECONDS) == "-50ns");
  ZC_EXPECT(zc::str((int64_t)zc::maxValue * zc::NANOSECONDS) == "9223372036.854775807s");
  ZC_EXPECT(zc::str((int64_t)zc::minValue * zc::NANOSECONDS) == "-9223372036.854775808s");
}

#if _WIN32
void delay(zc::Duration d) { Sleep(d / zc::MILLISECONDS); }
#else
void delay(zc::Duration d) { usleep(d / zc::MICROSECONDS); }
#endif

ZC_TEST("calendar clocks matches unix time") {
  // Check that the times returned by the calendar clock are within 1s of what time() returns.

  auto& coarse = systemCoarseCalendarClock();
  auto& precise = systemPreciseCalendarClock();

  Date p = precise.now();
  Date c = coarse.now();
  time_t t = time(nullptr);

  int64_t pi = (p - UNIX_EPOCH) / zc::SECONDS;
  int64_t ci = (c - UNIX_EPOCH) / zc::SECONDS;

  ZC_EXPECT(pi >= t - 1);
  ZC_EXPECT(pi <= t + 1);
  ZC_EXPECT(ci >= t - 1);
  ZC_EXPECT(ci <= t + 1);
}

ZC_TEST("monotonic clocks match each other") {
  // Check that the monotonic clocks return comparable times.

  auto& coarse = systemCoarseMonotonicClock();
  auto& precise = systemPreciseMonotonicClock();

  TimePoint p = precise.now();
  TimePoint c = coarse.now();

  // 40ms tolerance due to Windows timeslices being quite long, especially on GitHub Actions where
  // Windows is drunk and has completely lost track of time.
  ZC_EXPECT(p < c + 40 * zc::MILLISECONDS, p - c);
  ZC_EXPECT(p > c - 40 * zc::MILLISECONDS, c - p);
}

ZC_TEST("all clocks advance in real time") {
  Duration coarseCalDiff;
  Duration preciseCalDiff;
  Duration coarseMonoDiff;
  Duration preciseMonoDiff;

  for (uint retryCount ZC_UNUSED : zc::zeroTo(20)) {
    auto& coarseCal = systemCoarseCalendarClock();
    auto& preciseCal = systemPreciseCalendarClock();
    auto& coarseMono = systemCoarseMonotonicClock();
    auto& preciseMono = systemPreciseMonotonicClock();

    Date coarseCalBefore = coarseCal.now();
    Date preciseCalBefore = preciseCal.now();
    TimePoint coarseMonoBefore = coarseMono.now();
    TimePoint preciseMonoBefore = preciseMono.now();

    Duration delayTime = 150 * zc::MILLISECONDS;
    delay(delayTime);

    Date coarseCalAfter = coarseCal.now();
    Date preciseCalAfter = preciseCal.now();
    TimePoint coarseMonoAfter = coarseMono.now();
    TimePoint preciseMonoAfter = preciseMono.now();

    coarseCalDiff = coarseCalAfter - coarseCalBefore;
    preciseCalDiff = preciseCalAfter - preciseCalBefore;
    coarseMonoDiff = coarseMonoAfter - coarseMonoBefore;
    preciseMonoDiff = preciseMonoAfter - preciseMonoBefore;

    // 20ms tolerance due to Windows timeslices being quite long (and Windows sleeps being only
    // accurate to the timeslice).
    if (coarseCalDiff > delayTime - 20 * zc::MILLISECONDS &&
        coarseCalDiff < delayTime + 20 * zc::MILLISECONDS &&
        preciseCalDiff > delayTime - 20 * zc::MILLISECONDS &&
        preciseCalDiff < delayTime + 20 * zc::MILLISECONDS &&
        coarseMonoDiff > delayTime - 20 * zc::MILLISECONDS &&
        coarseMonoDiff < delayTime + 20 * zc::MILLISECONDS &&
        preciseMonoDiff > delayTime - 20 * zc::MILLISECONDS &&
        preciseMonoDiff < delayTime + 20 * zc::MILLISECONDS) {
      // success
      return;
    }
  }

  ZC_FAIL_EXPECT("clocks seem inaccurate even after 20 tries", coarseCalDiff / zc::MICROSECONDS,
                 preciseCalDiff / zc::MICROSECONDS, coarseMonoDiff / zc::MICROSECONDS,
                 preciseMonoDiff / zc::MICROSECONDS);
}

}  // namespace
}  // namespace zc
