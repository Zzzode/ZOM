// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/lsp/frame.h"

#include "zc/core/debug.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::lsp {
namespace {

constexpr uint8_t kCr = 0x0d;
constexpr uint8_t kLf = 0x0a;

/// \brief Lowercases one ASCII byte; non-ASCII bytes pass through unchanged.
constexpr uint8_t asciiLower(uint8_t byte) {
  return byte >= 'A' && byte <= 'Z' ? static_cast<uint8_t>(byte - 'A' + 'a') : byte;
}

/// \brief Case-insensitive ASCII comparison against a lowercase literal.
bool matchesHeaderName(zc::ArrayPtr<const uint8_t> name, zc::StringPtr lowercase) {
  if (name.size() != lowercase.size()) { return false; }
  for (size_t index = 0; index < name.size(); ++index) {
    if (asciiLower(name[index]) != static_cast<uint8_t>(lowercase[index])) { return false; }
  }
  return true;
}

/// \brief Trims leading and trailing spaces and tabs.
zc::ArrayPtr<const uint8_t> trim(zc::ArrayPtr<const uint8_t> value) {
  size_t start = 0;
  size_t end = value.size();
  while (start < end && (value[start] == ' ' || value[start] == '\t')) { ++start; }
  while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t')) { --end; }
  return value.slice(start, end);
}

/// \brief Parses a bounded decimal byte count, rejecting empty and overlong input.
///
/// Rejects any digit run longer than 20 characters before multiplying, so the
/// accumulator cannot wrap.
zc::Maybe<uint64_t> parseDecimal(zc::ArrayPtr<const uint8_t> digits, uint64_t maximum) {
  if (digits.size() == 0 || digits.size() > 20) { return zc::none; }
  uint64_t value = 0;
  for (const uint8_t byte : digits) {
    if (byte < '0' || byte > '9') { return zc::none; }
    const uint64_t digit = static_cast<uint64_t>(byte - '0');
    if (value > (maximum - digit) / 10) { return zc::none; }
    value = value * 10 + digit;
  }
  return value;
}

/// \brief Returns the offset of the first CRLF at or after `from`, if any.
zc::Maybe<size_t> findCrLf(zc::ArrayPtr<const uint8_t> bytes, size_t from) {
  for (size_t index = from; index + 1 < bytes.size(); ++index) {
    if (bytes[index] == kCr && bytes[index + 1] == kLf) { return index; }
  }
  return zc::none;
}

}  // namespace

FrameDecodeResult decodeFrame(zc::ArrayPtr<const uint8_t> bytes, const FrameLimits& limits) {
  zc::Maybe<uint64_t> contentLength;
  size_t cursor = 0;

  for (;;) {
    if (cursor > limits.maximumHeaderBytes) { return FrameDecodeFailure::MalformedHeader; }
    auto lineEnd = findCrLf(bytes, cursor);
    if (lineEnd == zc::none) {
      // A header block with no CRLF may simply be truncated, but one that has
      // already outgrown its bound never becomes valid.
      return bytes.size() > limits.maximumHeaderBytes ? FrameDecodeFailure::MalformedHeader
                                                      : FrameDecodeFailure::Incomplete;
    }
    const size_t end = ZC_ASSERT_NONNULL(lineEnd);
    if (end == cursor) {
      // The empty line terminates the header block.
      cursor = end + 2;
      break;
    }

    const auto line = bytes.slice(cursor, end);
    size_t colon = line.size();
    for (size_t index = 0; index < line.size(); ++index) {
      if (line[index] == ':') {
        colon = index;
        break;
      }
    }
    if (colon == line.size() || colon == 0) { return FrameDecodeFailure::MalformedHeader; }

    const auto name = trim(line.slice(0, colon));
    const auto value = trim(line.slice(colon + 1, line.size()));

    if (matchesHeaderName(name, "content-length"_zc)) {
      if (contentLength != zc::none) { return FrameDecodeFailure::InvalidContentLength; }
      auto parsed = parseDecimal(value, limits.maximumPayloadBytes);
      if (parsed == zc::none) {
        // Distinguish a well-formed but oversized length from a malformed one so
        // the caller can report the actual contract that was violated.
        auto unbounded = parseDecimal(value, ~static_cast<uint64_t>(0));
        return unbounded == zc::none ? FrameDecodeFailure::InvalidContentLength
                                     : FrameDecodeFailure::PayloadTooLarge;
      }
      contentLength = ZC_ASSERT_NONNULL(parsed);
    } else if (matchesHeaderName(name, "content-type"_zc)) {
      // The base protocol allows only utf-8. Accept a bare media type, and a
      // charset parameter naming utf-8 in either spelling.
      bool acceptable = true;
      for (size_t index = 0; index + 7 < value.size(); ++index) {
        if (matchesHeaderName(value.slice(index, index + 8), "charset="_zc)) {
          const auto charset = trim(value.slice(index + 8, value.size()));
          acceptable =
              matchesHeaderName(charset, "utf-8"_zc) || matchesHeaderName(charset, "utf8"_zc);
          break;
        }
      }
      if (!acceptable) { return FrameDecodeFailure::UnsupportedContentType; }
    }

    cursor = end + 2;
  }

  ZC_IF_SOME(length, contentLength) {
    const size_t payloadBytes = static_cast<size_t>(length);
    if (bytes.size() - cursor < payloadBytes) { return FrameDecodeFailure::Incomplete; }
    return DecodedFrame{bytes.slice(cursor, cursor + payloadBytes), cursor + payloadBytes};
  }
  return FrameDecodeFailure::InvalidContentLength;
}

zc::Array<uint8_t> encodeFrame(zc::ArrayPtr<const uint8_t> payload) {
  auto header = zc::str("Content-Length: ", payload.size(), "\r\n\r\n");
  auto frame = zc::heapArray<uint8_t>(header.size() + payload.size());
  const auto headerBytes = header.asBytes();
  for (size_t index = 0; index < headerBytes.size(); ++index) { frame[index] = headerBytes[index]; }
  for (size_t index = 0; index < payload.size(); ++index) {
    frame[headerBytes.size() + index] = payload[index];
  }
  return frame;
}

}  // namespace zomlang::compiler::lsp
