// Copyright (c) 2022 Cloudflare, Inc. and contributors
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

#if __GNUC__ && !_WIN32

#include <stdint.h>
#include <zc/core/exception.h>
#include <zc/ztest/gtest.h>

#include <stdexcept>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/debug.h"

namespace zc {

// override weak symbol
String stringifyStackTrace(ArrayPtr<void* const> trace) {
  return zc::str("\n\nTEST_SYMBOLIZER\n\n");
}

namespace {

ZC_TEST("getStackTrace() uses symbolizer override") {
  auto trace = getStackTrace();
  ZC_ASSERT(trace.contains("TEST_SYMBOLIZER"), trace);
}

}  // namespace
}  // namespace zc

#endif
