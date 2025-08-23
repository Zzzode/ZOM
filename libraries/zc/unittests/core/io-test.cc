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

#include "zc/core/io.h"

#include "zc/core/debug.h"
#include "zc/core/miniposix.h"
#include "zc/ztest/gtest.h"
#include "zc/ztest/test.h"

namespace zc {
namespace {

TEST(Io, WriteVec) {
  // Check that writing an array of arrays works even when some of the arrays are empty.  (This
  // used to not work in some cases.)

  int fds[2]{};
  ZC_SYSCALL(miniposix::pipe(fds));

  FdInputStream in((OwnFd(fds[0])));
  FdOutputStream out((OwnFd(fds[1])));

  ArrayPtr<const byte> pieces[5] = {arrayPtr(implicitCast<const byte*>(nullptr), 0), "foo"_zcb,
                                    arrayPtr(implicitCast<const byte*>(nullptr), 0), "bar"_zcb,
                                    arrayPtr(implicitCast<const byte*>(nullptr), 0)};

  out.write(pieces);

  byte buf[6]{};
  in.read(buf);
  EXPECT_EQ("foobar"_zcb, arrayPtr(buf));
}

ZC_TEST("stringify OwnFd") {
  int fds[2]{};
  ZC_SYSCALL(miniposix::pipe(fds));
  OwnFd in(fds[0]), out(fds[1]);

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

ZC_TEST("BufferedInputStreamWrapper basic buffering and skip operations") {
  // Verify BufferedInputStreamWrapper provides correct buffering behavior with default buffer size
  auto data = "Hello World"_zcb;
  ArrayInputStream arrayInput(data);

  // Test with default buffer
  BufferedInputStreamWrapper buffered(arrayInput);

  // Test getReadBuffer
  auto buffer = buffered.tryGetReadBuffer();
  ZC_EXPECT(buffer.size() > 0);
  ZC_EXPECT(buffer.size() == data.size());

  // Test skip
  buffered.skip(6);  // Skip "Hello "
  buffer = buffered.tryGetReadBuffer();
  ZC_EXPECT(buffer.size() == 5);  // "World" remaining
}

ZC_TEST("BufferedInputStreamWrapper respects custom buffer size limits") {
  // Ensure BufferedInputStreamWrapper correctly uses provided custom buffer without exceeding its
  // capacity
  auto data = "Test data for custom buffer"_zcb;
  ArrayInputStream arrayInput(data);

  byte customBuffer[16];
  BufferedInputStreamWrapper buffered(arrayInput, customBuffer);

  auto buffer = buffered.tryGetReadBuffer();
  ZC_EXPECT(buffer.size() <= 16);  // Should not exceed custom buffer size
  ZC_EXPECT(buffer.asChars().startsWith(zc::StringPtr("Test data")));
}

ZC_TEST("BufferedOutputStreamWrapper buffering and flush behavior") {
  // Verify BufferedOutputStreamWrapper correctly buffers writes and flushes data to underlying
  // stream
  VectorOutputStream vectorOutput;
  BufferedOutputStreamWrapper buffered(vectorOutput);

  // Test getWriteBuffer
  auto buffer = buffered.getWriteBuffer();
  ZC_EXPECT(buffer.size() > 0);

  // Write some data
  buffered.write("Hello, "_zcb);
  buffered.write("World!"_zcb);

  // Data might still be in buffer, so flush
  buffered.flush();

  auto result = vectorOutput.getArray();
  ZC_EXPECT(zc::str(result.asChars()) == "Hello, World!");
}

ZC_TEST("BufferedOutputStreamWrapper handles data exceeding custom buffer size") {
  // Test automatic flushing when write data exceeds the provided custom buffer capacity
  VectorOutputStream vectorOutput;
  byte customBuffer[8];
  BufferedOutputStreamWrapper buffered(vectorOutput, customBuffer);

  auto buffer = buffered.getWriteBuffer();
  ZC_EXPECT(buffer.size() <= 8);  // Should not exceed custom buffer size

  // Write data that exceeds buffer size to test flushing
  buffered.write("This is a long string that exceeds buffer"_zcb);
  buffered.flush();

  auto result = vectorOutput.getArray();
  ZC_EXPECT(zc::str(result.asChars()) == "This is a long string that exceeds buffer");
}

ZC_TEST("ArrayInputStream read operations and EOF behavior") {
  // Verify ArrayInputStream correctly provides read buffer access and handles end-of-file
  // conditions
  auto data = "ABC"_zcb;
  ArrayInputStream input(data);

  // Test tryGetReadBuffer
  auto buffer = input.tryGetReadBuffer();
  ZC_EXPECT(buffer == data);

  // Test tryRead - read all data at once
  byte readBuffer[4] = {0};  // Initialize to zero
  size_t bytesRead = input.tryRead(readBuffer, 3);
  ZC_EXPECT(bytesRead == 3);
  readBuffer[3] = '\0';  // Ensure NUL termination
  ZC_EXPECT(zc::StringPtr(reinterpret_cast<char*>(readBuffer), 3) == "ABC");

  // Test reading at EOF
  bytesRead = input.tryRead(readBuffer, 1);
  ZC_EXPECT(bytesRead == 0);  // Should be 0 at EOF
}

ZC_TEST("InputStream skip method preserves remaining data integrity") {
  // Verify that InputStream::skip() correctly advances position without affecting subsequent reads
  auto data = "ABCDEF"_zcb;  // 6 chars
  ArrayInputStream input(data);

  // Test skip
  input.skip(2);  // Skip "AB"

  // Test reading after skip - should read remaining data
  byte buffer[5] = {0};  // Initialize and make room for null terminator
  size_t bytesRead = input.tryRead(buffer, 4);
  ZC_EXPECT(bytesRead == 4);
  buffer[4] = '\0';  // Ensure null termination
  ZC_EXPECT(zc::StringPtr(reinterpret_cast<char*>(buffer), 4) == "CDEF");

  // Test reading at EOF
  bytesRead = input.tryRead(buffer, 1);
  ZC_EXPECT(bytesRead == 0);
}

ZC_TEST("InputStream tryRead respects minBytes parameter constraints") {
  // Ensure tryRead() returns at least minBytes when sufficient data is available
  auto data = "Hello World"_zcb;
  ArrayInputStream input(data);

  byte buffer[20];

  // Read with minBytes less than buffer size
  size_t bytesRead = input.tryRead(buffer, 5);
  ZC_EXPECT(bytesRead >= 5);
  buffer[5] = '\0';  // Ensure null termination
  ZC_EXPECT(zc::StringPtr(reinterpret_cast<char*>(buffer), 5) == "Hello");

  // Test exact read
  ArrayInputStream input2("Test"_zcb);
  size_t bytesRead2 = input2.tryRead(arrayPtr(buffer).first(4), 4);
  ZC_EXPECT(bytesRead2 == 4);
  buffer[4] = '\0';  // Ensure null termination
  ZC_EXPECT(zc::StringPtr(reinterpret_cast<char*>(buffer), 4) == "Test");
}

ZC_TEST("InputStream read throws on premature EOF when insufficient data available") {
  // Verify that read() throws appropriate exception when requested bytes exceed available data
  auto data = "Hi"_zcb;
  ArrayInputStream input(data);

  byte buffer[10];

  // Try to read more than available - should throw
  ZC_EXPECT_THROW_MESSAGE("Premature EOF", input.read(buffer, 5));
}

ZC_TEST("OwnFd RAII semantics and ownership transfer operations") {
  // Verify OwnFd properly manages file descriptor lifecycle with move semantics and release
  OwnFd fd1;  // Default construction
  ZC_EXPECT(fd1 == nullptr);
  ZC_EXPECT(fd1.get() == -1);

  OwnFd fd2(nullptr);  // Null construction
  ZC_EXPECT(fd2 == nullptr);

  // Test with valid fd (using a pipe)
  int fds[2];
  ZC_SYSCALL(miniposix::pipe(fds));

  OwnFd ownedFd(fds[0]);
  ZC_EXPECT(ownedFd.get() == fds[0]);
  ZC_EXPECT(static_cast<int>(ownedFd) == fds[0]);

  // Test move construction
  OwnFd movedFd = zc::mv(ownedFd);
  ZC_EXPECT(movedFd.get() == fds[0]);
  ZC_EXPECT(ownedFd.get() == -1);

  // Test release
  int releasedFd = movedFd.release();
  ZC_EXPECT(releasedFd == fds[0]);
  ZC_EXPECT(movedFd.get() == -1);

  // Clean up
  ZC_SYSCALL(miniposix::close(releasedFd));
  ZC_SYSCALL(miniposix::close(fds[1]));
}

ZC_TEST("VectorOutputStream handles capacity growth and clear operations correctly") {
  // Test VectorOutputStream behavior with small initial capacity, automatic growth, and state reset
  VectorOutputStream output(8);  // Small initial capacity

  // Test initial state
  ZC_EXPECT(output.getArray().size() == 0);
  auto buffer = output.getWriteBuffer();
  ZC_EXPECT(buffer.size() >= 8);

  // Write data that causes growth
  auto longData =
      "This is a very long string that will definitely exceed the initial capacity of 8 bytes"_zcb;
  output.write(longData);

  ZC_EXPECT(output.getArray().size() == longData.size());
  ZC_EXPECT(output.getArray() == longData);

  // Test clear
  output.clear();
  ZC_EXPECT(output.getArray().size() == 0);

  // Write again after clear
  output.write("New data"_zcb);
  ZC_EXPECT(zc::str(output.getArray().asChars()) == "New data");
}

}  // namespace
}  // namespace zc
