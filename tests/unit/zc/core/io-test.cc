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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "src/zc/core/io.h"

#include <src/zc/ztest/gtest.h>

#include "src/zc/core/debug.h"
#include "src/zc/core/miniposix.h"

namespace zc {
namespace {

TEST(Io, WriteVec) {
  // Check that writing an array of arrays works even when some of the arrays are empty.  (This
  // used to not work in some cases.)

  int fds[2]{};
  ZC_SYSCALL(miniposix::pipe(fds));

  FdInputStream in((AutoCloseFd(fds[0])));
  FdOutputStream out((AutoCloseFd(fds[1])));

  ArrayPtr<const byte> pieces[5] = {arrayPtr(implicitCast<const byte*>(nullptr), 0), "foo"_zcb,
                                    arrayPtr(implicitCast<const byte*>(nullptr), 0), "bar"_zcb,
                                    arrayPtr(implicitCast<const byte*>(nullptr), 0)};

  out.write(pieces);

  byte buf[6]{};
  in.read(buf);
  EXPECT_EQ("foobar"_zcb, arrayPtr(buf));
}

ZC_TEST("stringify AutoCloseFd") {
  int fds[2]{};
  ZC_SYSCALL(miniposix::pipe(fds));
  AutoCloseFd in(fds[0]), out(fds[1]);

  ZC_EXPECT(zc::str(in) == zc::str(fds[0]), in, fds[0]);
}

ZC_TEST("VectorOutputStream") {
  VectorOutputStream output(16);
  auto buf = output.getWriteBuffer();
  ZC_ASSERT(buf.size() == 16);

  for (auto i : zc::indices(buf)) { buf[i] = 'a' + i; }

  output.write(buf.first(4));
  ZC_ASSERT(output.getArray().begin() == buf.begin());
  ZC_ASSERT(output.getArray().size() == 4);

  auto buf2 = output.getWriteBuffer();
  ZC_ASSERT(buf2.end() == buf.end());
  ZC_ASSERT(buf2.size() == 12);

  output.write(buf2);
  ZC_ASSERT(output.getArray().begin() == buf.begin());
  ZC_ASSERT(output.getArray().size() == 16);

  auto buf3 = output.getWriteBuffer();
  ZC_ASSERT(buf3.size() == 16);
  ZC_ASSERT(output.getArray().begin() != buf.begin());
  ZC_ASSERT(output.getArray().end() == buf3.begin());
  ZC_ASSERT(zc::str(output.getArray().asChars()) == "abcdefghijklmnop");

  byte junk[24]{};
  for (auto i : zc::indices(junk)) { junk[i] = 'A' + i; }

  output.write(arrayPtr(junk).first(4));
  ZC_ASSERT(output.getArray().begin() != buf.begin());
  ZC_ASSERT(output.getArray().end() == buf3.begin() + 4);
  ZC_ASSERT(zc::str(output.getArray().asChars()) == "abcdefghijklmnopABCD");

  output.write(arrayPtr(junk).slice(4, 24));
  // (We can't assert output.getArray().begin() != buf.begin() because the memory allocator could
  // legitimately have allocated a new array in the same space.)
  ZC_ASSERT(output.getArray().end() != buf3.begin() + 24);
  ZC_ASSERT(zc::str(output.getArray().asChars()) == "abcdefghijklmnopABCDEFGHIJKLMNOPQRSTUVWX");

  ZC_ASSERT(output.getWriteBuffer().size() == 24);
  ZC_ASSERT(output.getWriteBuffer().begin() == output.getArray().begin() + 40);

  output.clear();
  ZC_ASSERT(output.getWriteBuffer().begin() == output.getArray().begin());
  ZC_ASSERT(output.getWriteBuffer().size() == 64);
  ZC_ASSERT(output.getArray().size() == 0);
}

class MockInputStream : public InputStream {
public:
  MockInputStream(zc::ArrayPtr<const byte> bytes, size_t blockSize)
      : bytes(bytes), blockSize(blockSize) {}

  size_t tryRead(ArrayPtr<byte> buffer, size_t minBytes) override {
    // Clamp max read to blockSize.
    size_t n = zc::min(blockSize, buffer.size());

    // Unless that's less than minBytes -- in which case, use minBytes.
    n = zc::max(n, minBytes);

    // But also don't read more data than we have.
    n = zc::min(n, bytes.size());

    memcpy(buffer.begin(), bytes.begin(), n);
    bytes = bytes.slice(n, bytes.size());
    return n;
  }

private:
  zc::ArrayPtr<const byte> bytes;
  size_t blockSize;
};

ZC_TEST("InputStream::readAllText() / readAllBytes()") {
  auto bigText = strArray(zc::repeat("foo bar baz"_zc, 12345), ",");
  size_t inputSizes[] = {0, 1, 256, 4096, 8191, 8192, 8193, 10000, bigText.size()};
  size_t blockSizes[] = {1, 4, 256, 4096, 8192, bigText.size()};
  uint64_t limits[] = {0,
                       1,
                       256,
                       bigText.size() / 2,
                       bigText.size() - 1,
                       bigText.size(),
                       bigText.size() + 1,
                       zc::maxValue};

  for (size_t inputSize : inputSizes) {
    for (size_t blockSize : blockSizes) {
      for (uint64_t limit : limits) {
        ZC_CONTEXT(inputSize, blockSize, limit);
        auto textSlice = bigText.asBytes().first(inputSize);
        auto readAllText = [&]() {
          MockInputStream input(textSlice, blockSize);
          return input.readAllText(limit);
        };
        auto readAllBytes = [&]() {
          MockInputStream input(textSlice, blockSize);
          return input.readAllBytes(limit);
        };
        if (limit > inputSize) {
          ZC_EXPECT(readAllText().asBytes() == textSlice);
          ZC_EXPECT(readAllBytes() == textSlice);
        } else {
          ZC_EXPECT_THROW_MESSAGE("Reached limit before EOF.", readAllText());
          ZC_EXPECT_THROW_MESSAGE("Reached limit before EOF.", readAllBytes());
        }
      }
    }
  }
}

ZC_TEST("ArrayOutputStream::write() does not assume adjacent write buffer is its own") {
  // Previously, if ArrayOutputStream::write(src, size) saw that `src` equaled its fill position, it
  // would assume that the write was already in its buffer. This assumption was buggy if the write
  // buffer was directly adjacent in memory to the ArrayOutputStream's buffer, and the
  // ArrayOutputStream was full (i.e., its fill position was one-past-the-end).
  //
  // VectorOutputStream also suffered a similar bug, but it is much harder to test, since it
  // performs its own allocation.

  zc::byte buffer[10] = {0};

  ArrayOutputStream output(arrayPtr(buffer, buffer + 5));

  // Succeeds and fills the ArrayOutputStream.
  output.write(arrayPtr(buffer).slice(5, 10));

  // Previously this threw an inscrutable "size <= array.end() - fillPos" requirement failure.
  ZC_EXPECT_THROW_MESSAGE("backing array was not large enough for the data written",
                          output.write(arrayPtr(buffer).slice(5, 10)));
}

}  // namespace
}  // namespace zc
