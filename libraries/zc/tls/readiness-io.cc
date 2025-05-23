// Copyright (c) 2016 Sandstorm Development Group, Inc. and contributors
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

#include "zc/tls/readiness-io.h"

namespace zc {

static size_t copyInto(zc::ArrayPtr<byte> dst, zc::ArrayPtr<const byte>& src) {
  size_t n = zc::min(dst.size(), src.size());
  memcpy(dst.begin(), src.begin(), n);
  src = src.slice(n, src.size());
  return n;
}

// =======================================================================================

ReadyInputStreamWrapper::ReadyInputStreamWrapper(AsyncInputStream& input) : input(input) {}
ReadyInputStreamWrapper::~ReadyInputStreamWrapper() noexcept(false) {}

zc::Maybe<size_t> ReadyInputStreamWrapper::read(zc::ArrayPtr<byte> dst) {
  if (eof || dst.size() == 0) return size_t(0);

  if (content.size() == 0) {
    // No data available. Try to read more.
    if (!isPumping) {
      isPumping = true;
      pumpTask = zc::evalNow([&]() {
                   return input.tryRead(buffer, 1, sizeof(buffer)).then([this](size_t n) {
                     if (n == 0) {
                       eof = true;
                     } else {
                       content = zc::arrayPtr(buffer, n);
                     }
                     isPumping = false;
                   });
                 }).fork();
    }

    return zc::none;
  }

  return copyInto(dst, content);
}

zc::Promise<void> ReadyInputStreamWrapper::whenReady() { return pumpTask.addBranch(); }

// =======================================================================================

ReadyOutputStreamWrapper::ReadyOutputStreamWrapper(AsyncOutputStream& output) : output(output) {}
ReadyOutputStreamWrapper::~ReadyOutputStreamWrapper() noexcept(false) {}

zc::Maybe<size_t> ReadyOutputStreamWrapper::write(zc::ArrayPtr<const byte> data) {
  if (data.size() == 0) return size_t(0);

  if (filled == sizeof(buffer)) {
    // No space.
    return zc::none;
  }

  uint end = start + filled;
  size_t result = 0;
  if (end < sizeof(buffer)) {
    // The filled part of the buffer is somewhere in the middle.

    // Copy into space after filled space.
    result += copyInto(zc::arrayPtr(buffer + end, buffer + sizeof(buffer)), data);

    // Copy into space before filled space.
    result += copyInto(zc::arrayPtr(buffer, buffer + start), data);
  } else {
    // Fill currently loops, so we only have one segment of empty space to copy into.

    // Copy into the space between the fill's end and the fill's start.
    result += copyInto(zc::arrayPtr(buffer + end % sizeof(buffer), buffer + start), data);
  }

  filled += result;

  if (!isPumping && (!corked || filled == sizeof(buffer))) {
    isPumping = true;
    pumpTask = zc::evalNow([&]() { return pump(); }).fork();
  }

  return result;
}

zc::Promise<void> ReadyOutputStreamWrapper::whenReady() { return pumpTask.addBranch(); }

ReadyOutputStreamWrapper::Cork ReadyOutputStreamWrapper::cork() {
  corked = true;
  return Cork(*this);
}

void ReadyOutputStreamWrapper::uncork() {
  corked = false;
  if (!isPumping && filled > 0) {
    isPumping = true;
    pumpTask = zc::evalNow([&]() { return pump(); }).fork();
  }
}

zc::Promise<void> ReadyOutputStreamWrapper::pump() {
  uint oldFilled = filled;
  uint end = start + filled;

  zc::Promise<void> promise = nullptr;
  if (end <= sizeof(buffer)) {
    promise = output.write(zc::arrayPtr(buffer + start, filled));
  } else {
    end = end % sizeof(buffer);
    segments[0] = zc::arrayPtr(buffer + start, buffer + sizeof(buffer));
    segments[1] = zc::arrayPtr(buffer, buffer + end);
    promise = output.write(segments);
  }

  return promise.then([this, oldFilled, end]() -> zc::Promise<void> {
    filled -= oldFilled;
    start = end;

    if (filled > 0) {
      return pump();
    } else {
      isPumping = false;
      // As a small optimization, reset to the start of the buffer when it's empty so we can provide
      // the underlying layer just one contiguous chunk of memory instead of two when possible.
      start = 0;
      return zc::READY_NOW;
    }
  });
}

}  // namespace zc
