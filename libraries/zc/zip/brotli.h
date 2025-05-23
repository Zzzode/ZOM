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

#pragma once

#include <brotli/decode.h>
#include <brotli/encode.h>
#include <zc/async/async-io.h>
#include <zc/core/io.h>
#include <zc/core/one-of.h>

ZC_BEGIN_HEADER

namespace zc {

// level 5 should offer a good default tradeoff based on concerns about being slower than zip at
// e.g. level 6 and about compressing worse than zip at lower levels. Note that
// BROTLI_DEFAULT_QUALITY is set to the maximum level of 11 – way too slow for on-the-fly
// compression.
constexpr size_t ZC_BROTLI_DEFAULT_QUALITY = 5;

namespace _ {  // private
// Use a window size of (1 << 19) = 512K by default. Higher values improve compression on longer
// streams but increase memory usage.
constexpr size_t ZC_BROTLI_DEFAULT_WBITS = 19;

// Maximum window size for streams to be decompressed, streams with larger windows are rejected.
// This is currently set to the maximum window size of 16MB, so all RFC 7932-compliant brotli
// streams will be accepted. For applications where memory usage is a concern, using
// BROTLI_DEFAULT_WINDOW (equivalent to 4MB window) is recommended instead as larger window sizes
// are rarely useful in a web context.
constexpr size_t ZC_BROTLI_MAX_DEC_WBITS = BROTLI_MAX_WINDOW_BITS;

// Use an output buffer size of 8K, larger sizes did not seem to significantly improve performance,
// perhaps due to brotli's internal output buffer.
constexpr size_t ZC_BROTLI_BUF_SIZE = 8192;

class BrotliOutputContext final {
public:
  BrotliOutputContext(zc::Maybe<int> compressionLevel, zc::Maybe<int> windowBits = zc::none);
  ~BrotliOutputContext() noexcept(false);
  ZC_DISALLOW_COPY_AND_MOVE(BrotliOutputContext);

  void setInput(const void* in, size_t size);
  zc::Tuple<bool, zc::ArrayPtr<const byte>> pumpOnce(BrotliEncoderOperation flush);
  // Flush the stream. Parameter is ignored for decoding as brotli only uses an operation parameter
  // during encoding.

private:
  int windowBits;
  const byte* nextIn;
  size_t availableIn;
  bool firstInput = true;

  zc::OneOf<BrotliEncoderState*, BrotliDecoderState*> ctx;
  byte buffer[_::ZC_BROTLI_BUF_SIZE];
};

}  // namespace _

class BrotliInputStream final : public InputStream {
public:
  BrotliInputStream(InputStream& inner, zc::Maybe<int> windowBits = zc::none);
  ~BrotliInputStream() noexcept(false);
  ZC_DISALLOW_COPY_AND_MOVE(BrotliInputStream);

  size_t tryRead(ArrayPtr<byte> buffer, size_t minBytes) override;

private:
  InputStream& inner;
  BrotliDecoderState* ctx;
  int windowBits;
  bool atValidEndpoint = false;

  byte buffer[_::ZC_BROTLI_BUF_SIZE];

  const byte* nextIn;
  size_t availableIn;
  bool firstInput = true;

  size_t readImpl(ArrayPtr<byte> buffer, size_t minBytes, size_t alreadyRead);
};

class BrotliOutputStream final : public OutputStream {
public:
  enum { DECOMPRESS };

  // Order of arguments is not ideal, but allows us to specify the window size if needed while
  // remaining compatible with the zip API.
  BrotliOutputStream(OutputStream& inner, int compressionLevel = ZC_BROTLI_DEFAULT_QUALITY,
                     int windowBits = _::ZC_BROTLI_DEFAULT_WBITS);
  BrotliOutputStream(OutputStream& inner, decltype(DECOMPRESS),
                     int windowBits = _::ZC_BROTLI_MAX_DEC_WBITS);
  ~BrotliOutputStream() noexcept(false);
  ZC_DISALLOW_COPY_AND_MOVE(BrotliOutputStream);

  void write(ArrayPtr<const byte> data) override;

  using OutputStream::write;

  inline void flush() {
    // brotli decoder does not use this parameter, but automatically flushes as much as it can.
    pump(BROTLI_OPERATION_FLUSH);
  }

private:
  OutputStream& inner;
  _::BrotliOutputContext ctx;

  void pump(BrotliEncoderOperation flush);
};

class BrotliAsyncInputStream final : public AsyncInputStream {
public:
  BrotliAsyncInputStream(AsyncInputStream& inner, zc::Maybe<int> windowBits = zc::none);
  ~BrotliAsyncInputStream() noexcept(false);
  ZC_DISALLOW_COPY_AND_MOVE(BrotliAsyncInputStream);

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override;

private:
  AsyncInputStream& inner;
  BrotliDecoderState* ctx;
  int windowBits;
  bool atValidEndpoint = false;

  byte buffer[_::ZC_BROTLI_BUF_SIZE];
  const byte* nextIn;
  size_t availableIn;
  bool firstInput = true;

  Promise<size_t> readImpl(byte* buffer, size_t minBytes, size_t maxBytes, size_t alreadyRead);
};

class BrotliAsyncOutputStream final : public AsyncOutputStream {
public:
  enum { DECOMPRESS };

  BrotliAsyncOutputStream(AsyncOutputStream& inner,
                          int compressionLevel = ZC_BROTLI_DEFAULT_QUALITY,
                          int windowBits = _::ZC_BROTLI_DEFAULT_WBITS);
  BrotliAsyncOutputStream(AsyncOutputStream& inner, decltype(DECOMPRESS),
                          int windowBits = _::ZC_BROTLI_MAX_DEC_WBITS);
  ZC_DISALLOW_COPY_AND_MOVE(BrotliAsyncOutputStream);

  Promise<void> write(ArrayPtr<const byte> buffer) override;
  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override;

  Promise<void> whenWriteDisconnected() override { return inner.whenWriteDisconnected(); }

  inline Promise<void> flush() {
    // brotli decoder does not use this parameter, but automatically flushes as much as it can.
    return pump(BROTLI_OPERATION_FLUSH);
  }
  // Call if you need to flush a stream at an arbitrary data point.

  Promise<void> end() { return pump(BROTLI_OPERATION_FINISH); }
  // Must call to flush and finish the stream, since some data may be buffered.
  //
  // TODO(cleanup): This should be a virtual method on AsyncOutputStream.

private:
  AsyncOutputStream& inner;
  _::BrotliOutputContext ctx;

  zc::Promise<void> pump(BrotliEncoderOperation flush);
};

}  // namespace zc

ZC_END_HEADER
