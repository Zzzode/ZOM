// Copyright (c) 2017 Cloudflare, Inc. and contributors
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

#if ZC_HAS_ZLIB

#include "src/zc/zip/gzip.h"

#include <src/zc/core/debug.h>
#include <src/zc/ztest/test.h>
#include <stdlib.h>

namespace zc {
namespace {

static const byte FOOBAR_GZIP[] = {
    0x1F, 0x8B, 0x08, 0x00, 0xF9, 0x05, 0xB7, 0x59, 0x00, 0x03, 0x4B, 0xCB, 0xCF,
    0x4F, 0x4A, 0x2C, 0x02, 0x00, 0x95, 0x1F, 0xF6, 0x9E, 0x06, 0x00, 0x00, 0x00,
};

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

class MockAsyncInputStream : public AsyncInputStream {
public:
  MockAsyncInputStream(zc::ArrayPtr<const byte> bytes, size_t blockSize)
      : bytes(bytes), blockSize(blockSize) {}

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    // Clamp max read to blockSize.
    size_t n = zc::min(blockSize, maxBytes);

    // Unless that's less than minBytes -- in which case, use minBytes.
    n = zc::max(n, minBytes);

    // But also don't read more data than we have.
    n = zc::min(n, bytes.size());

    memcpy(buffer, bytes.begin(), n);
    bytes = bytes.slice(n, bytes.size());
    return n;
  }

private:
  zc::ArrayPtr<const byte> bytes;
  size_t blockSize;
};

class MockOutputStream : public OutputStream {
public:
  zc::Vector<byte> bytes;

  zc::String decompress() {
    MockInputStream rawInput(bytes, zc::maxValue);
    GzipInputStream gzip(rawInput);
    return gzip.readAllText();
  }

  void write(ArrayPtr<const byte> data) override { bytes.addAll(data); }
};

class MockAsyncOutputStream : public AsyncOutputStream {
public:
  zc::Vector<byte> bytes;

  zc::String decompress(WaitScope& ws) {
    MockAsyncInputStream rawInput(bytes, zc::maxValue);
    GzipAsyncInputStream gzip(rawInput);
    return gzip.readAllText().wait(ws);
  }

  Promise<void> write(ArrayPtr<const byte> buffer) override {
    bytes.addAll(buffer);
    return zc::READY_NOW;
  }
  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    for (auto& piece : pieces) { bytes.addAll(piece); }
    return zc::READY_NOW;
  }

  Promise<void> whenWriteDisconnected() override { ZC_UNIMPLEMENTED("not used"); }
};

ZC_TEST("zip decompression") {
  // Normal read.
  {
    MockInputStream rawInput(FOOBAR_GZIP, zc::maxValue);
    GzipInputStream gzip(rawInput);
    ZC_EXPECT(gzip.readAllText() == "foobar");
  }

  // Force read one byte at a time.
  {
    MockInputStream rawInput(FOOBAR_GZIP, 1);
    GzipInputStream gzip(rawInput);
    ZC_EXPECT(gzip.readAllText() == "foobar");
  }

  // Read truncated input.
  {
    MockInputStream rawInput(zc::arrayPtr(FOOBAR_GZIP, sizeof(FOOBAR_GZIP) / 2), zc::maxValue);
    GzipInputStream gzip(rawInput);

    byte text[16]{};
    auto amount = gzip.tryRead(text, 1);
    ZC_EXPECT(arrayPtr(text).first(amount) == "fo"_zcb);

    ZC_EXPECT_THROW_MESSAGE("zip compressed stream ended prematurely", gzip.tryRead(text, 1));
  }

  // Read concatenated input.
  {
    Vector<byte> bytes;
    bytes.addAll(ArrayPtr<const byte>(FOOBAR_GZIP));
    bytes.addAll(ArrayPtr<const byte>(FOOBAR_GZIP));
    MockInputStream rawInput(bytes, zc::maxValue);
    GzipInputStream gzip(rawInput);

    ZC_EXPECT(gzip.readAllText() == "foobarfoobar");
  }
}

ZC_TEST("async zip decompression") {
  auto io = setupAsyncIo();

  // Normal read.
  {
    MockAsyncInputStream rawInput(FOOBAR_GZIP, zc::maxValue);
    GzipAsyncInputStream gzip(rawInput);
    ZC_EXPECT(gzip.readAllText().wait(io.waitScope) == "foobar");
  }

  // Force read one byte at a time.
  {
    MockAsyncInputStream rawInput(FOOBAR_GZIP, 1);
    GzipAsyncInputStream gzip(rawInput);
    ZC_EXPECT(gzip.readAllText().wait(io.waitScope) == "foobar");
  }

  // Read truncated input.
  {
    MockAsyncInputStream rawInput(zc::arrayPtr(FOOBAR_GZIP, sizeof(FOOBAR_GZIP) / 2), zc::maxValue);
    GzipAsyncInputStream gzip(rawInput);

    char text[16]{};
    size_t n = gzip.tryRead(text, 1, sizeof(text)).wait(io.waitScope);
    text[n] = '\0';
    ZC_EXPECT(StringPtr(text, n) == "fo");

    ZC_EXPECT_THROW_MESSAGE("zip compressed stream ended prematurely",
                            gzip.tryRead(text, 1, sizeof(text)).wait(io.waitScope));
  }

  // Read concatenated input.
  {
    Vector<byte> bytes;
    bytes.addAll(ArrayPtr<const byte>(FOOBAR_GZIP));
    bytes.addAll(ArrayPtr<const byte>(FOOBAR_GZIP));
    MockAsyncInputStream rawInput(bytes, zc::maxValue);
    GzipAsyncInputStream gzip(rawInput);

    ZC_EXPECT(gzip.readAllText().wait(io.waitScope) == "foobarfoobar");
  }

  // Decompress using an output stream.
  {
    MockAsyncOutputStream rawOutput;
    GzipAsyncOutputStream gzip(rawOutput, GzipAsyncOutputStream::DECOMPRESS);

    auto mid = sizeof(FOOBAR_GZIP) / 2;
    gzip.write(arrayPtr(FOOBAR_GZIP).first(mid)).wait(io.waitScope);
    auto str1 = zc::heapString(rawOutput.bytes.asPtr().asChars());
    ZC_EXPECT(str1 == "fo", str1);

    gzip.write(arrayPtr(FOOBAR_GZIP).slice(mid)).wait(io.waitScope);
    auto str2 = zc::heapString(rawOutput.bytes.asPtr().asChars());
    ZC_EXPECT(str2 == "foobar", str2);

    gzip.end().wait(io.waitScope);
  }
}

ZC_TEST("zip compression") {
  // Normal write.
  {
    MockOutputStream rawOutput;
    {
      GzipOutputStream gzip(rawOutput);
      gzip.write("foobar"_zcb);
    }

    ZC_EXPECT(rawOutput.decompress() == "foobar");
  }

  // Multi-part write.
  {
    MockOutputStream rawOutput;
    {
      GzipOutputStream gzip(rawOutput);
      gzip.write("foo"_zcb);
      gzip.write("bar"_zcb);
    }

    ZC_EXPECT(rawOutput.decompress() == "foobar");
  }

  // Array-of-arrays write.
  {
    MockOutputStream rawOutput;

    {
      GzipOutputStream gzip(rawOutput);
      ArrayPtr<const byte> pieces[] = {
          "foo"_zcb,
          "bar"_zcb,
      };
      gzip.write(pieces);
    }

    ZC_EXPECT(rawOutput.decompress() == "foobar");
  }
}

ZC_TEST("zip huge round trip") {
  auto bytes = heapArray<byte>(65536);
  for (auto& b : bytes) { b = rand(); }

  MockOutputStream rawOutput;
  {
    GzipOutputStream gzipOut(rawOutput);
    gzipOut.write(bytes);
  }

  MockInputStream rawInput(rawOutput.bytes, zc::maxValue);
  GzipInputStream gzipIn(rawInput);
  auto decompressed = gzipIn.readAllBytes();

  ZC_ASSERT(bytes == decompressed);
}

ZC_TEST("async zip compression") {
  auto io = setupAsyncIo();

  // Normal write.
  {
    MockAsyncOutputStream rawOutput;
    GzipAsyncOutputStream gzip(rawOutput);
    gzip.write("foobar"_zcb).wait(io.waitScope);
    gzip.end().wait(io.waitScope);

    ZC_EXPECT(rawOutput.decompress(io.waitScope) == "foobar");
  }

  // Multi-part write.
  {
    MockAsyncOutputStream rawOutput;
    GzipAsyncOutputStream gzip(rawOutput);

    gzip.write("foo"_zcb).wait(io.waitScope);
    auto prevSize = rawOutput.bytes.size();

    gzip.write("bar"_zcb).wait(io.waitScope);
    auto curSize = rawOutput.bytes.size();
    ZC_EXPECT(prevSize == curSize, prevSize, curSize);

    gzip.flush().wait(io.waitScope);
    curSize = rawOutput.bytes.size();
    ZC_EXPECT(prevSize < curSize, prevSize, curSize);

    gzip.end().wait(io.waitScope);

    ZC_EXPECT(rawOutput.decompress(io.waitScope) == "foobar");
  }

  // Array-of-arrays write.
  {
    MockAsyncOutputStream rawOutput;
    GzipAsyncOutputStream gzip(rawOutput);

    ArrayPtr<const byte> pieces[] = {
        zc::StringPtr("foo").asBytes(),
        zc::StringPtr("bar").asBytes(),
    };
    gzip.write(pieces).wait(io.waitScope);
    gzip.end().wait(io.waitScope);

    ZC_EXPECT(rawOutput.decompress(io.waitScope) == "foobar");
  }
}

ZC_TEST("async zip huge round trip") {
  auto io = setupAsyncIo();

  auto bytes = heapArray<byte>(65536);
  for (auto& b : bytes) { b = rand(); }

  MockAsyncOutputStream rawOutput;
  GzipAsyncOutputStream gzipOut(rawOutput);
  gzipOut.write(bytes).wait(io.waitScope);
  gzipOut.end().wait(io.waitScope);

  MockAsyncInputStream rawInput(rawOutput.bytes, zc::maxValue);
  GzipAsyncInputStream gzipIn(rawInput);
  auto decompressed = gzipIn.readAllBytes().wait(io.waitScope);

  ZC_ASSERT(bytes == decompressed);
}

}  // namespace
}  // namespace zc

#endif  // ZC_HAS_ZLIB
