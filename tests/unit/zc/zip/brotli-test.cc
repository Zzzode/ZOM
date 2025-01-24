// Copyright (c) 2023 Cloudflare, Inc. and contributors
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

#if ZC_HAS_BROTLI

#include "zc/zip/brotli.h"

#include <stdlib.h>
#include <zc/core/debug.h>
#include <zc/ztest/test.h>

namespace zc {
namespace {

static const byte FOOBAR_BR[] = {
    0x83, 0x02, 0x80, 0x66, 0x6f, 0x6f, 0x62, 0x61, 0x72, 0x03,
};

// brotli stream with 24 window bits, i.e. the max window size. If ZC_BROTLI_MAX_DEC_WBITS is less
// than 24, the stream will be rejected by default. This approach should be acceptable in a web
// context, where few files benefit from larger windows and memory usage matters for
// concurrent transfers.
static const byte FOOBAR_BR_LARGE_WIN[] = {
    0x8f, 0x02, 0x80, 0x66, 0x6f, 0x6f, 0x62, 0x61, 0x72, 0x03,
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
    BrotliInputStream brotli(rawInput);
    return brotli.readAllText();
  }

  void write(ArrayPtr<const byte> data) override { bytes.addAll(data); }
};

class MockAsyncOutputStream : public AsyncOutputStream {
public:
  zc::Vector<byte> bytes;

  zc::String decompress(WaitScope& ws) {
    MockAsyncInputStream rawInput(bytes, zc::maxValue);
    BrotliAsyncInputStream brotli(rawInput);
    return brotli.readAllText().wait(ws);
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

ZC_TEST("brotli decompression") {
  // Normal read.
  {
    MockInputStream rawInput(FOOBAR_BR, zc::maxValue);
    BrotliInputStream brotli(rawInput);
    ZC_EXPECT(brotli.readAllText() == "foobar");
  }

  // Force read one byte at a time.
  {
    MockInputStream rawInput(FOOBAR_BR, 1);
    BrotliInputStream brotli(rawInput);
    ZC_EXPECT(brotli.readAllText() == "foobar");
  }

  // Read truncated input.
  {
    MockInputStream rawInput(zc::arrayPtr(FOOBAR_BR, sizeof(FOOBAR_BR) / 2), zc::maxValue);
    BrotliInputStream brotli(rawInput);

    byte text[16]{};
    auto amount = brotli.tryRead(text, 1);
    ZC_EXPECT(arrayPtr(text).first(amount) == "fo"_zcb);

    ZC_EXPECT_THROW_MESSAGE("brotli compressed stream ended prematurely", brotli.tryRead(text, 1));
  }

  // Check that stream with high window size is rejected. Conversely, check that it is accepted if
  // configured to accept the full window size.
  {
    MockInputStream rawInput(FOOBAR_BR_LARGE_WIN, zc::maxValue);
    BrotliInputStream brotli(rawInput, BROTLI_DEFAULT_WINDOW);
    ZC_EXPECT_THROW_MESSAGE("brotli window size too big", brotli.readAllText());
  }

  {
    MockInputStream rawInput(FOOBAR_BR_LARGE_WIN, zc::maxValue);
    BrotliInputStream brotli(rawInput, BROTLI_MAX_WINDOW_BITS);
    ZC_EXPECT(brotli.readAllText() == "foobar");
  }

  // Check that invalid stream is rejected.
  {
    MockInputStream rawInput(zc::arrayPtr(FOOBAR_BR + 3, sizeof(FOOBAR_BR) - 3), zc::maxValue);
    BrotliInputStream brotli(rawInput);
    ZC_EXPECT_THROW_MESSAGE("brotli decompression failed", brotli.readAllText());
  }

  // Read concatenated input.
  {
    Vector<byte> bytes;
    bytes.addAll(ArrayPtr<const byte>(FOOBAR_BR));
    bytes.addAll(ArrayPtr<const byte>(FOOBAR_BR));
    MockInputStream rawInput(bytes, zc::maxValue);
    BrotliInputStream brotli(rawInput);

    ZC_EXPECT(brotli.readAllText() == "foobarfoobar");
  }
}

ZC_TEST("async brotli decompression") {
  auto io = setupAsyncIo();

  // Normal read.
  {
    MockAsyncInputStream rawInput(FOOBAR_BR, zc::maxValue);
    BrotliAsyncInputStream brotli(rawInput);
    ZC_EXPECT(brotli.readAllText().wait(io.waitScope) == "foobar");
  }

  // Force read one byte at a time.
  {
    MockAsyncInputStream rawInput(FOOBAR_BR, 1);
    BrotliAsyncInputStream brotli(rawInput);
    ZC_EXPECT(brotli.readAllText().wait(io.waitScope) == "foobar");
  }

  // Read truncated input.
  {
    MockAsyncInputStream rawInput(zc::arrayPtr(FOOBAR_BR, sizeof(FOOBAR_BR) / 2), zc::maxValue);
    BrotliAsyncInputStream brotli(rawInput);

    char text[16]{};
    size_t n = brotli.tryRead(text, 1, sizeof(text)).wait(io.waitScope);
    text[n] = '\0';
    ZC_EXPECT(StringPtr(text, n) == "fo");

    ZC_EXPECT_THROW_MESSAGE("brotli compressed stream ended prematurely",
                            brotli.tryRead(text, 1, sizeof(text)).wait(io.waitScope));
  }

  // Check that stream with high window size is rejected. Conversely, check that it is accepted if
  // configured to accept the full window size.
  {
    MockAsyncInputStream rawInput(FOOBAR_BR_LARGE_WIN, zc::maxValue);
    BrotliAsyncInputStream brotli(rawInput, BROTLI_DEFAULT_WINDOW);
    ZC_EXPECT_THROW_MESSAGE("brotli window size too big", brotli.readAllText().wait(io.waitScope));
  }

  {
    MockAsyncInputStream rawInput(FOOBAR_BR_LARGE_WIN, zc::maxValue);
    BrotliAsyncInputStream brotli(rawInput, BROTLI_MAX_WINDOW_BITS);
    ZC_EXPECT(brotli.readAllText().wait(io.waitScope) == "foobar");
  }

  // Read concatenated input.
  {
    Vector<byte> bytes;
    bytes.addAll(ArrayPtr<const byte>(FOOBAR_BR));
    bytes.addAll(ArrayPtr<const byte>(FOOBAR_BR));
    MockAsyncInputStream rawInput(bytes, zc::maxValue);
    BrotliAsyncInputStream brotli(rawInput);

    ZC_EXPECT(brotli.readAllText().wait(io.waitScope) == "foobarfoobar");
  }

  // Decompress using an output stream.
  {
    MockAsyncOutputStream rawOutput;
    BrotliAsyncOutputStream brotli(rawOutput, BrotliAsyncOutputStream::DECOMPRESS);

    auto mid = sizeof(FOOBAR_BR) / 2;
    brotli.write(arrayPtr(FOOBAR_BR).first(mid)).wait(io.waitScope);
    auto str1 = zc::heapString(rawOutput.bytes.asPtr().asChars());
    ZC_EXPECT(str1 == "fo", str1);

    brotli.write(arrayPtr(FOOBAR_BR).slice(mid)).wait(io.waitScope);
    auto str2 = zc::heapString(rawOutput.bytes.asPtr().asChars());
    ZC_EXPECT(str2 == "foobar", str2);

    brotli.end().wait(io.waitScope);
  }
}

ZC_TEST("brotli compression") {
  // Normal write.
  {
    MockOutputStream rawOutput;
    {
      BrotliOutputStream brotli(rawOutput);
      brotli.write("foobar"_zcb);
    }

    ZC_EXPECT(rawOutput.decompress() == "foobar");
  }

  // Multi-part write.
  {
    MockOutputStream rawOutput;
    {
      BrotliOutputStream brotli(rawOutput);
      brotli.write("foo"_zcb);
      brotli.write("bar"_zcb);
    }

    ZC_EXPECT(rawOutput.decompress() == "foobar");
  }

  // Array-of-arrays write.
  {
    MockOutputStream rawOutput;

    {
      BrotliOutputStream brotli(rawOutput);

      ArrayPtr<const byte> pieces[] = {
          "foo"_zcb,
          "bar"_zcb,
      };
      brotli.write(pieces);
    }

    ZC_EXPECT(rawOutput.decompress() == "foobar");
  }
}

ZC_TEST("brotli huge round trip") {
  auto bytes = heapArray<byte>(96 * 1024);
  for (auto& b : bytes) { b = rand(); }

  MockOutputStream rawOutput;
  {
    BrotliOutputStream brotliOut(rawOutput);
    brotliOut.write(bytes);
  }

  MockInputStream rawInput(rawOutput.bytes, zc::maxValue);
  BrotliInputStream brotliIn(rawInput);
  auto decompressed = brotliIn.readAllBytes();

  ZC_ASSERT(bytes == decompressed);
}

ZC_TEST("async brotli compression") {
  auto io = setupAsyncIo();
  // Normal write.
  {
    MockAsyncOutputStream rawOutput;
    BrotliAsyncOutputStream brotli(rawOutput);
    brotli.write("foobar"_zcb).wait(io.waitScope);
    brotli.end().wait(io.waitScope);

    ZC_EXPECT(rawOutput.decompress(io.waitScope) == "foobar");
  }

  // Multi-part write.
  {
    MockAsyncOutputStream rawOutput;
    BrotliAsyncOutputStream brotli(rawOutput);

    brotli.write("foo"_zcb).wait(io.waitScope);
    auto prevSize = rawOutput.bytes.size();

    brotli.write("bar"_zcb).wait(io.waitScope);
    auto curSize = rawOutput.bytes.size();
    ZC_EXPECT(prevSize == curSize, prevSize, curSize);

    brotli.flush().wait(io.waitScope);
    curSize = rawOutput.bytes.size();
    ZC_EXPECT(prevSize < curSize, prevSize, curSize);

    brotli.end().wait(io.waitScope);

    ZC_EXPECT(rawOutput.decompress(io.waitScope) == "foobar");
  }

  // Array-of-arrays write.
  {
    MockAsyncOutputStream rawOutput;
    BrotliAsyncOutputStream brotli(rawOutput);

    ArrayPtr<const byte> pieces[] = {
        zc::StringPtr("foo").asBytes(),
        zc::StringPtr("bar").asBytes(),
    };
    brotli.write(pieces).wait(io.waitScope);
    brotli.end().wait(io.waitScope);

    ZC_EXPECT(rawOutput.decompress(io.waitScope) == "foobar");
  }
}

ZC_TEST("async brotli huge round trip") {
  auto io = setupAsyncIo();

  auto bytes = heapArray<byte>(65536);
  for (auto& b : bytes) { b = rand(); }

  MockAsyncOutputStream rawOutput;
  BrotliAsyncOutputStream brotliOut(rawOutput);
  brotliOut.write(bytes).wait(io.waitScope);
  brotliOut.end().wait(io.waitScope);

  MockAsyncInputStream rawInput(rawOutput.bytes, zc::maxValue);
  BrotliAsyncInputStream brotliIn(rawInput);
  auto decompressed = brotliIn.readAllBytes().wait(io.waitScope);

  ZC_ASSERT(bytes == decompressed);
}

}  // namespace
}  // namespace zc

#endif  // ZC_HAS_BROTLI
