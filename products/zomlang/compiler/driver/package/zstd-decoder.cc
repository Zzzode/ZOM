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
// See the License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/driver/package/zstd-decoder.h"

#include <cstdint>

#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"
#include "zstd_errors.h"

namespace zomlang::compiler::driver::package {
namespace {

struct ZstdContextDisposer final {
  static void dispose(ZSTD_DCtx* context) { ZSTD_freeDCtx(context); }
};

MaterializationIssue translateZstdFailure(size_t status) {
  switch (ZSTD_getErrorCode(status)) {
    case ZSTD_error_frameParameter_windowTooLarge:
      return MaterializationIssue::DecoderWindowLimit;
    case ZSTD_error_memory_allocation:
      return MaterializationIssue::DecoderMemoryLimit;
    default:
      return MaterializationIssue::ArchiveDecodeFailed;
  }
}

zc::Maybe<MaterializationIssue> validateLimits(const SourceAdmissionLimits& limits) {
  if (limits.compressedArchiveBytes == 0 || limits.zstdWindowBytes == 0 ||
      limits.decoderWorkingBytes == 0 || limits.ioChunkBytes == 0 ||
      limits.ioChunkBytes > static_cast<uint64_t>(SIZE_MAX) ||
      limits.zstdWindowBytes > static_cast<uint64_t>(SIZE_MAX)) {
    return MaterializationIssue::LengthOverflow;
  }
  return zc::none;
}

ZstdInputResult readChunk(ZstdInput& input, zc::ArrayPtr<zc::byte> destination,
                          uint64_t& compressedBytes, uint64_t compressedLimit) {
  auto result = input.read(destination);
  if (result.is<MaterializationIssue>()) { return result.get<MaterializationIssue>(); }
  if (result.is<ZstdInputEnd>()) { return ZstdInputEnd{}; }

  const size_t byteCount = result.get<ZstdInputData>().byteCount;
  if (byteCount == 0 || byteCount > destination.size()) {
    return MaterializationIssue::SourceReadFailed;
  }
  if (compressedBytes > UINT64_MAX - byteCount) { return MaterializationIssue::LengthOverflow; }
  compressedBytes += byteCount;
  if (compressedBytes > compressedLimit) { return MaterializationIssue::CompressedSizeLimit; }
  return ZstdInputData{byteCount};
}

}  // namespace

struct ZstdDecoder::Impl final {
  explicit Impl(SourceAdmissionLimits sourceLimits)
      : limits(sourceLimits), context(ZSTD_createDCtx()) {}

  SourceAdmissionLimits limits;
  zc::Own<ZSTD_DCtx, ZstdContextDisposer> context;
};

ZstdDecoder::ZstdDecoder(SourceAdmissionLimits limits) : impl(zc::heap<Impl>(limits)) {}

ZstdDecoder::~ZstdDecoder() noexcept(false) = default;

ZstdDecoder::ZstdDecoder(ZstdDecoder&&) noexcept = default;

ZstdDecoder& ZstdDecoder::operator=(ZstdDecoder&&) noexcept = default;

zc::Maybe<MaterializationIssue> ZstdDecoder::decode(ZstdInput& input, ZstdOutput& output) {
  ZC_IF_SOME(issue, validateLimits(impl->limits)) { return issue; }
  if (!impl->context) { return MaterializationIssue::DecoderMemoryLimit; }

  const size_t maxWindow = static_cast<size_t>(impl->limits.zstdWindowBytes);
  const size_t estimatedWorkingBytes = ZSTD_estimateDStreamSize(maxWindow);
  if (ZSTD_isError(estimatedWorkingBytes)) { return translateZstdFailure(estimatedWorkingBytes); }
  if (estimatedWorkingBytes > impl->limits.decoderWorkingBytes) {
    return MaterializationIssue::DecoderMemoryLimit;
  }

  size_t status = ZSTD_DCtx_reset(impl->context.get(), ZSTD_reset_session_only);
  if (ZSTD_isError(status)) { return translateZstdFailure(status); }
  status = ZSTD_DCtx_setMaxWindowSize(impl->context.get(), maxWindow);
  if (ZSTD_isError(status)) { return translateZstdFailure(status); }

  const size_t chunkSize = static_cast<size_t>(impl->limits.ioChunkBytes);
  auto inputBuffer = zc::heapArray<zc::byte>(chunkSize);
  auto outputBuffer = zc::heapArray<zc::byte>(chunkSize);
  size_t inputSize = 0;
  uint64_t compressedBytes = 0;
  ZSTD_FrameHeader frameHeader{};

  while (true) {
    if (inputSize > 0) {
      status = ZSTD_getFrameHeader(&frameHeader, inputBuffer.begin(), inputSize);
      if (ZSTD_isError(status)) { return translateZstdFailure(status); }
      if (status == 0) { break; }
    }
    if (inputSize == inputBuffer.size()) { return MaterializationIssue::ArchiveDecodeFailed; }
    auto read = readChunk(input, inputBuffer.slice(inputSize, inputBuffer.size()), compressedBytes,
                          impl->limits.compressedArchiveBytes);
    if (read.is<MaterializationIssue>()) { return read.get<MaterializationIssue>(); }
    if (read.is<ZstdInputEnd>()) { return MaterializationIssue::ArchiveDecodeFailed; }
    inputSize += read.get<ZstdInputData>().byteCount;
  }

  if (frameHeader.frameType != ZSTD_frame) {
    return MaterializationIssue::UnsupportedArchiveFormat;
  }
  if (frameHeader.windowSize > impl->limits.zstdWindowBytes) {
    return MaterializationIssue::DecoderWindowLimit;
  }
  const size_t frameWorkingBytes =
      ZSTD_estimateDStreamSize_fromFrame(inputBuffer.begin(), inputSize);
  if (ZSTD_isError(frameWorkingBytes)) { return translateZstdFailure(frameWorkingBytes); }
  if (frameWorkingBytes > impl->limits.decoderWorkingBytes) {
    return MaterializationIssue::DecoderMemoryLimit;
  }

  ZSTD_inBuffer zstdInput{inputBuffer.begin(), inputSize, 0};
  while (true) {
    ZSTD_outBuffer zstdOutput{outputBuffer.begin(), outputBuffer.size(), 0};
    status = ZSTD_decompressStream(impl->context.get(), &zstdOutput, &zstdInput);
    if (ZSTD_isError(status)) { return translateZstdFailure(status); }
    if (zstdOutput.pos > 0) {
      ZC_IF_SOME(issue, output.write(outputBuffer.first(zstdOutput.pos))) { return issue; }
    }

    if (status == 0) {
      if (zstdInput.pos != zstdInput.size) { return MaterializationIssue::TrailingArchiveData; }
      auto trailing = readChunk(input, inputBuffer.asPtr(), compressedBytes,
                                impl->limits.compressedArchiveBytes);
      if (trailing.is<MaterializationIssue>()) { return trailing.get<MaterializationIssue>(); }
      return trailing.is<ZstdInputEnd>()
                 ? zc::Maybe<MaterializationIssue>(zc::none)
                 : zc::Maybe<MaterializationIssue>(MaterializationIssue::TrailingArchiveData);
    }

    if (zstdInput.pos == zstdInput.size) {
      auto next = readChunk(input, inputBuffer.asPtr(), compressedBytes,
                            impl->limits.compressedArchiveBytes);
      if (next.is<MaterializationIssue>()) { return next.get<MaterializationIssue>(); }
      if (next.is<ZstdInputEnd>()) { return MaterializationIssue::ArchiveDecodeFailed; }
      zstdInput.src = inputBuffer.begin();
      zstdInput.size = next.get<ZstdInputData>().byteCount;
      zstdInput.pos = 0;
    }
  }
}

}  // namespace zomlang::compiler::driver::package
