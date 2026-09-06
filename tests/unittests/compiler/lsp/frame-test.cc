// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "compiler/lsp/frame.h"

#include "zc/core/string.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::lsp {
namespace {

zc::Array<uint8_t> bytesOf(zc::StringPtr text) {
  auto source = text.asBytes();
  auto copy = zc::heapArray<uint8_t>(source.size());
  for (size_t index = 0; index < source.size(); ++index) { copy[index] = source[index]; }
  return copy;
}

zc::String textOf(zc::ArrayPtr<const uint8_t> bytes) {
  return zc::heapString(reinterpret_cast<const char*>(bytes.begin()), bytes.size());
}

FrameDecodeFailure failureOf(zc::StringPtr text, const FrameLimits& limits = FrameLimits()) {
  auto buffer = bytesOf(text);
  auto result = decodeFrame(buffer.asPtr(), limits);
  ZC_REQUIRE(result.is<FrameDecodeFailure>());
  return result.get<FrameDecodeFailure>();
}

}  // namespace

ZC_TEST("Frame decode accepts a minimal well-formed frame") {
  auto buffer = bytesOf("Content-Length: 2\r\n\r\n{}"_zc);
  auto result = decodeFrame(buffer.asPtr());
  ZC_REQUIRE(result.is<DecodedFrame>());
  const auto& frame = result.get<DecodedFrame>();
  ZC_EXPECT(textOf(frame.payload) == "{}");
  ZC_EXPECT(frame.consumed == buffer.size());
}

ZC_TEST("Frame decode reports the offset past the first of two frames") {
  auto buffer = bytesOf("Content-Length: 2\r\n\r\n{}Content-Length: 4\r\n\r\ntrue"_zc);
  auto first = decodeFrame(buffer.asPtr());
  ZC_REQUIRE(first.is<DecodedFrame>());
  const size_t consumed = first.get<DecodedFrame>().consumed;
  ZC_EXPECT(textOf(first.get<DecodedFrame>().payload) == "{}");

  auto second = decodeFrame(buffer.slice(consumed, buffer.size()));
  ZC_REQUIRE(second.is<DecodedFrame>());
  ZC_EXPECT(textOf(second.get<DecodedFrame>().payload) == "true");
}

ZC_TEST("Frame decode ignores unknown headers and is case-insensitive") {
  auto buffer = bytesOf("X-Trace: 1\r\ncOnTeNt-LeNgTh: 3\r\n\r\nabc"_zc);
  auto result = decodeFrame(buffer.asPtr());
  ZC_REQUIRE(result.is<DecodedFrame>());
  ZC_EXPECT(textOf(result.get<DecodedFrame>().payload) == "abc");
}

ZC_TEST("Frame decode accepts an empty payload") {
  auto buffer = bytesOf("Content-Length: 0\r\n\r\n"_zc);
  auto result = decodeFrame(buffer.asPtr());
  ZC_REQUIRE(result.is<DecodedFrame>());
  ZC_EXPECT(result.get<DecodedFrame>().payload.size() == 0);
  ZC_EXPECT(result.get<DecodedFrame>().consumed == buffer.size());
}

ZC_TEST("Frame decode accepts a utf-8 content type in either spelling") {
  for (const zc::StringPtr charset : {"utf-8"_zc, "utf8"_zc, "UTF-8"_zc}) {
    auto buffer =
        bytesOf(zc::str("Content-Length: 1\r\nContent-Type: application/vscode-jsonrpc; "
                        "charset=",
                        charset, "\r\n\r\nx"));
    auto result = decodeFrame(buffer.asPtr());
    ZC_REQUIRE(result.is<DecodedFrame>());
    ZC_EXPECT(textOf(result.get<DecodedFrame>().payload) == "x");
  }
}

ZC_TEST("Frame decode rejects a non-utf-8 charset") {
  ZC_EXPECT(
      failureOf("Content-Length: 1\r\nContent-Type: text/plain; charset=utf-16\r\n\r\nx"_zc) ==
      FrameDecodeFailure::UnsupportedContentType);
}

ZC_TEST("Frame decode treats a truncated header block as incomplete") {
  ZC_EXPECT(failureOf("Content-Length: 2\r\n"_zc) == FrameDecodeFailure::Incomplete);
  ZC_EXPECT(failureOf(""_zc) == FrameDecodeFailure::Incomplete);
}

ZC_TEST("Frame decode treats a truncated payload as incomplete") {
  ZC_EXPECT(failureOf("Content-Length: 8\r\n\r\nshort"_zc) == FrameDecodeFailure::Incomplete);
}

ZC_TEST("Frame decode rejects a missing, repeated, or malformed content length") {
  ZC_EXPECT(failureOf("X-Only: 1\r\n\r\n"_zc) == FrameDecodeFailure::InvalidContentLength);
  ZC_EXPECT(failureOf("Content-Length: 1\r\nContent-Length: 1\r\n\r\nx"_zc) ==
            FrameDecodeFailure::InvalidContentLength);
  ZC_EXPECT(failureOf("Content-Length: \r\n\r\n"_zc) == FrameDecodeFailure::InvalidContentLength);
  ZC_EXPECT(failureOf("Content-Length: 12a\r\n\r\n"_zc) ==
            FrameDecodeFailure::InvalidContentLength);
  ZC_EXPECT(failureOf("Content-Length: -1\r\n\r\n"_zc) == FrameDecodeFailure::InvalidContentLength);
}

ZC_TEST("Frame decode rejects a header line with no colon or an empty name") {
  ZC_EXPECT(failureOf("Content-Length 2\r\n\r\n{}"_zc) == FrameDecodeFailure::MalformedHeader);
  ZC_EXPECT(failureOf(": 2\r\n\r\n{}"_zc) == FrameDecodeFailure::MalformedHeader);
}

ZC_TEST("Frame decode separates an oversized length from a malformed one") {
  FrameLimits limits;
  limits.maximumPayloadBytes = 16;
  ZC_EXPECT(failureOf("Content-Length: 4096\r\n\r\n"_zc, limits) ==
            FrameDecodeFailure::PayloadTooLarge);
  // A digit run long enough to overflow a 64-bit accumulator is malformed, not
  // merely oversized, and must never wrap into an accepted value.
  ZC_EXPECT(failureOf("Content-Length: 999999999999999999999999\r\n\r\n"_zc, limits) ==
            FrameDecodeFailure::InvalidContentLength);
}

ZC_TEST("Frame decode bounds the header block") {
  FrameLimits limits;
  limits.maximumHeaderBytes = 32;
  auto padding = zc::heapString(64);
  for (char& character : padding) { character = 'x'; }
  ZC_EXPECT(failureOf(zc::str("X-Pad: ", padding, "\r\nContent-Length: 1\r\n\r\nx"), limits) ==
            FrameDecodeFailure::MalformedHeader);
}

ZC_TEST("Frame encode round-trips through decode") {
  auto payload = bytesOf("{\"jsonrpc\":\"2.0\",\"id\":1}"_zc);
  auto frame = encodeFrame(payload.asPtr());
  ZC_EXPECT(textOf(frame.slice(0, 20)) == "Content-Length: 24\r\n");

  auto result = decodeFrame(frame.asPtr());
  ZC_REQUIRE(result.is<DecodedFrame>());
  ZC_EXPECT(textOf(result.get<DecodedFrame>().payload) == "{\"jsonrpc\":\"2.0\",\"id\":1}");
  ZC_EXPECT(result.get<DecodedFrame>().consumed == frame.size());
}

ZC_TEST("Frame encode round-trips an empty payload") {
  auto frame = encodeFrame(zc::ArrayPtr<const uint8_t>());
  auto result = decodeFrame(frame.asPtr());
  ZC_REQUIRE(result.is<DecodedFrame>());
  ZC_EXPECT(result.get<DecodedFrame>().payload.size() == 0);
}

}  // namespace zomlang::compiler::lsp
