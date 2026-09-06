// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/one-of.h"

namespace zomlang::compiler::lsp {

/// \brief Closed reasons a base-protocol frame cannot be decoded.
enum class FrameDecodeFailure : uint8_t {
  /// The buffer does not yet hold a complete header block or payload.
  Incomplete = 0x01,
  /// A header line is not `Name: value` or the block is not CRLF-delimited.
  MalformedHeader = 0x02,
  /// `Content-Length` is absent, repeated, empty, non-numeric, or overlong.
  InvalidContentLength = 0x03,
  /// `Content-Length` exceeds the configured maximum payload size.
  PayloadTooLarge = 0x04,
  /// `Content-Type` names a charset other than utf-8.
  UnsupportedContentType = 0x05,
};

/// \brief Bounds applied while decoding one frame.
struct FrameLimits final {
  /// \brief Largest accepted `Content-Length` value, in bytes.
  uint64_t maximumPayloadBytes = 32u * 1024u * 1024u;
  /// \brief Largest accepted header block, in bytes, including the final CRLF.
  uint64_t maximumHeaderBytes = 8u * 1024u;
};

/// \brief One decoded frame and the byte offset just past it.
struct DecodedFrame final {
  /// \brief The payload bytes, excluding the header block.
  zc::ArrayPtr<const uint8_t> payload;
  /// \brief Offset in the input at which the next frame begins.
  size_t consumed;
};

/// \brief Result of one decode attempt.
using FrameDecodeResult = zc::OneOf<DecodedFrame, FrameDecodeFailure>;

/// \brief Decodes the first base-protocol frame in `bytes`.
///
/// Implements the LSP base protocol header block: CRLF-terminated `Name: value`
/// lines followed by an empty CRLF line and exactly `Content-Length` payload
/// bytes. `Content-Length` is mandatory; `Content-Type` is optional and, when
/// present, must name `utf-8` or `utf8`. Unknown headers are ignored, matching
/// the base protocol.
///
/// The returned payload aliases `bytes` and stays valid only while `bytes` does.
///
/// `Incomplete` is not an error: it means the caller should read more bytes and
/// retry with the longer buffer. Every other failure is terminal for the
/// connection, because the stream position of the next frame is unknowable once
/// a header block is malformed.
///
/// \param bytes The buffer to decode from, starting at a frame boundary.
/// \param limits The bounds to enforce while decoding.
/// \return The decoded frame and its total size, or a closed failure.
ZC_NODISCARD FrameDecodeResult decodeFrame(zc::ArrayPtr<const uint8_t> bytes,
                                           const FrameLimits& limits = FrameLimits());

/// \brief Encodes one payload as a base-protocol frame.
///
/// Writes exactly `Content-Length: <n>\r\n\r\n` followed by the payload. No
/// `Content-Type` header is written: utf-8 is the base protocol default, and
/// omitting it keeps the framing deterministic.
///
/// \param payload The payload bytes to frame.
/// \return The complete frame, header block included.
ZC_NODISCARD zc::Array<uint8_t> encodeFrame(zc::ArrayPtr<const uint8_t> payload);

}  // namespace zomlang::compiler::lsp
