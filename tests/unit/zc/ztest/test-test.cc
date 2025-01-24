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

#include "zc/ztest/test.h"

#include <signal.h>

#include <cstdlib>
#include <stdexcept>

#include "zc/core/common.h"
#include "zc/core/function.h"

#ifndef _WIN32
#include <unistd.h>
#endif

namespace zc {
namespace _ {
namespace {

ZC_TEST("expect exit from exit") {
  ZC_EXPECT_EXIT(42, _exit(42));
  ZC_EXPECT_EXIT(zc::none, _exit(42));
}

ZC_TEST("expect exit from thrown exception") {
  ZC_EXPECT_EXIT(1, throw std::logic_error("test error"));
}

ZC_TEST("expect signal from abort") { ZC_EXPECT_SIGNAL(SIGABRT, abort()); }

ZC_TEST("expect signal from sigint") {
  ZC_EXPECT_SIGNAL(SIGINT, raise(SIGINT));
  ZC_EXPECT_SIGNAL(zc::none, raise(SIGINT));
}

}  // namespace
}  // namespace _
}  // namespace zc
