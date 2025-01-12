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

#include "src/zc/core/glob-filter.h"

#include "src/zc/core/common.h"
#include "src/zc/ztest/test.h"

namespace zc {
namespace _ {
namespace {

ZC_TEST("GlobFilter") {
  {
    GlobFilter filter("foo");

    ZC_EXPECT(filter.matches("foo"));
    ZC_EXPECT(!filter.matches("bar"));
    ZC_EXPECT(!filter.matches("foob"));
    ZC_EXPECT(!filter.matches("foobbb"));
    ZC_EXPECT(!filter.matches("fobbbb"));
    ZC_EXPECT(!filter.matches("bfoo"));
    ZC_EXPECT(!filter.matches("bbbbbfoo"));
    ZC_EXPECT(filter.matches("bbbbb/foo"));
    ZC_EXPECT(filter.matches("bar/baz/foo"));
  }

  {
    GlobFilter filter("foo*");

    ZC_EXPECT(filter.matches("foo"));
    ZC_EXPECT(!filter.matches("bar"));
    ZC_EXPECT(filter.matches("foob"));
    ZC_EXPECT(filter.matches("foobbb"));
    ZC_EXPECT(!filter.matches("fobbbb"));
    ZC_EXPECT(!filter.matches("bfoo"));
    ZC_EXPECT(!filter.matches("bbbbbfoo"));
    ZC_EXPECT(filter.matches("bbbbb/foo"));
    ZC_EXPECT(filter.matches("bar/baz/foo"));
  }

  {
    GlobFilter filter("foo*bar");

    ZC_EXPECT(filter.matches("foobar"));
    ZC_EXPECT(filter.matches("fooxbar"));
    ZC_EXPECT(filter.matches("fooxxxbar"));
    ZC_EXPECT(!filter.matches("foo/bar"));
    ZC_EXPECT(filter.matches("blah/fooxxxbar"));
    ZC_EXPECT(!filter.matches("blah/xxfooxxxbar"));
  }

  {
    GlobFilter filter("foo?bar");

    ZC_EXPECT(!filter.matches("foobar"));
    ZC_EXPECT(filter.matches("fooxbar"));
    ZC_EXPECT(!filter.matches("fooxxxbar"));
    ZC_EXPECT(!filter.matches("foo/bar"));
    ZC_EXPECT(filter.matches("blah/fooxbar"));
    ZC_EXPECT(!filter.matches("blah/xxfooxbar"));
  }
}

}  // namespace
}  // namespace _
}  // namespace zc
