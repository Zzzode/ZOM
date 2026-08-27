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

#include "compiler/driver/package/zstd-decoder.h"

#include <cstdint>

#define ZSTD_STATIC_LINKING_ONLY
#include "compiler/driver/package/archive-reader.h"
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
  explicit Impl(SourceAdmissionLimits sourceLimits) : limits(sourceLimits) {}

  SourceAdmissionLimits limits;
};

namespace {

class DecodedArchiveInput final : public ArchiveInput {
public:
  DecodedArchiveInput(ZstdInput& source, SourceAdmissionLimits limits)
      : source(source),
        limits(limits),
        context(ZSTD_createDCtx()),
        inputBuffer(zc::heapArray<zc::byte>(static_cast<size_t>(limits.ioChunkBytes))) {}

  ArchiveInputResult read(zc::ArrayPtr<zc::byte> destination) override {
    if (terminalIssue != zc::none) {
      ZC_IF_SOME(issue, terminalIssue) { return issue; }
    }
    if (finished) { return ArchiveInputEnd{}; }
    if (destination.size() == 0) { return fail(MaterializationIssue::SourceReadFailed); }
    ZC_IF_SOME(issue, initialize()) { return fail(issue); }

    while (true) {
      ZSTD_inBuffer zstdInput{inputBuffer.begin(), inputSize, inputPosition};
      ZSTD_outBuffer zstdOutput{destination.begin(), destination.size(), 0};
      const size_t status = ZSTD_decompressStream(context.get(), &zstdOutput, &zstdInput);
      inputPosition = zstdInput.pos;
      if (ZSTD_isError(status)) { return fail(translateZstdFailure(status)); }

      if (status == 0) {
        if (inputPosition != inputSize) { return fail(MaterializationIssue::TrailingArchiveData); }
        auto trailing =
            readChunk(source, inputBuffer.asPtr(), compressedBytes, limits.compressedArchiveBytes);
        if (trailing.is<MaterializationIssue>()) {
          return fail(trailing.get<MaterializationIssue>());
        }
        if (!trailing.is<ZstdInputEnd>()) {
          return fail(MaterializationIssue::TrailingArchiveData);
        }
        finished = true;
        return zstdOutput.pos == 0 ? ArchiveInputResult(ArchiveInputEnd{})
                                   : ArchiveInputResult(ArchiveInputData{zstdOutput.pos});
      }

      if (zstdOutput.pos != 0) { return ArchiveInputData{zstdOutput.pos}; }
      if (inputPosition == inputSize) {
        auto next =
            readChunk(source, inputBuffer.asPtr(), compressedBytes, limits.compressedArchiveBytes);
        if (next.is<MaterializationIssue>()) { return fail(next.get<MaterializationIssue>()); }
        if (next.is<ZstdInputEnd>()) { return fail(MaterializationIssue::ArchiveDecodeFailed); }
        inputSize = next.get<ZstdInputData>().byteCount;
        inputPosition = 0;
      }
    }
  }

private:
  ArchiveInputResult fail(MaterializationIssue issue) {
    terminalIssue = issue;
    return issue;
  }

  zc::Maybe<MaterializationIssue> initialize() {
    if (initialized) { return zc::none; }
    if (!context) { return MaterializationIssue::DecoderMemoryLimit; }

    ZSTD_FrameHeader frameHeader{};
    while (true) {
      if (inputSize != 0) {
        const size_t status = ZSTD_getFrameHeader(&frameHeader, inputBuffer.begin(), inputSize);
        if (ZSTD_isError(status)) { return translateZstdFailure(status); }
        if (status == 0) { break; }
      }
      if (inputSize == inputBuffer.size()) { return MaterializationIssue::ArchiveDecodeFailed; }
      auto next = readChunk(source, inputBuffer.slice(inputSize, inputBuffer.size()),
                            compressedBytes, limits.compressedArchiveBytes);
      if (next.is<MaterializationIssue>()) { return next.get<MaterializationIssue>(); }
      if (next.is<ZstdInputEnd>()) { return MaterializationIssue::ArchiveDecodeFailed; }
      inputSize += next.get<ZstdInputData>().byteCount;
    }
    if (frameHeader.frameType != ZSTD_frame) {
      return MaterializationIssue::UnsupportedArchiveFormat;
    }
    if (frameHeader.windowSize > limits.zstdWindowBytes) {
      return MaterializationIssue::DecoderWindowLimit;
    }
    const size_t frameWorkingBytes =
        ZSTD_estimateDStreamSize_fromFrame(inputBuffer.begin(), inputSize);
    if (ZSTD_isError(frameWorkingBytes)) { return translateZstdFailure(frameWorkingBytes); }
    if (frameWorkingBytes > limits.decoderWorkingBytes) {
      return MaterializationIssue::DecoderMemoryLimit;
    }
    size_t status = ZSTD_DCtx_reset(context.get(), ZSTD_reset_session_only);
    if (ZSTD_isError(status)) { return translateZstdFailure(status); }
    status = ZSTD_DCtx_setMaxWindowSize(context.get(), static_cast<size_t>(limits.zstdWindowBytes));
    if (ZSTD_isError(status)) { return translateZstdFailure(status); }
    initialized = true;
    return zc::none;
  }

  ZstdInput& source;
  SourceAdmissionLimits limits;
  zc::Own<ZSTD_DCtx, ZstdContextDisposer> context;
  zc::Array<zc::byte> inputBuffer;
  size_t inputSize = 0;
  size_t inputPosition = 0;
  uint64_t compressedBytes = 0;
  bool initialized = false;
  bool finished = false;
  zc::Maybe<MaterializationIssue> terminalIssue;
};

}  // namespace

ZstdDecoder::ZstdDecoder(SourceAdmissionLimits limits) : impl(zc::heap<Impl>(limits)) {}

ZstdDecoder::~ZstdDecoder() noexcept(false) = default;

ZstdDecoder::ZstdDecoder(ZstdDecoder&&) noexcept = default;

ZstdDecoder& ZstdDecoder::operator=(ZstdDecoder&&) noexcept = default;

zc::Maybe<MaterializationIssue> ZstdDecoder::decode(ZstdInput& input, ZstdOutput& output) {
  auto decoded = openDecodedInput(input);
  if (decoded.is<MaterializationIssue>()) { return decoded.get<MaterializationIssue>(); }
  auto stream = zc::mv(decoded.get<zc::Own<ArchiveInput>>());
  auto outputBuffer = zc::heapArray<zc::byte>(static_cast<size_t>(impl->limits.ioChunkBytes));
  while (true) {
    auto result = stream->read(outputBuffer.asPtr());
    if (result.is<MaterializationIssue>()) { return result.get<MaterializationIssue>(); }
    if (result.is<ArchiveInputEnd>()) { return zc::none; }
    const auto count = result.get<ArchiveInputData>().byteCount;
    ZC_IF_SOME(issue, output.write(outputBuffer.first(count))) { return issue; }
  }
}

ZstdDecodedInputResult ZstdDecoder::openDecodedInput(ZstdInput& input) const {
  ZC_IF_SOME(issue, validateLimits(impl->limits)) { return issue; }
  const size_t estimatedWorkingBytes =
      ZSTD_estimateDStreamSize(static_cast<size_t>(impl->limits.zstdWindowBytes));
  if (ZSTD_isError(estimatedWorkingBytes)) { return translateZstdFailure(estimatedWorkingBytes); }
  if (estimatedWorkingBytes > impl->limits.decoderWorkingBytes) {
    return MaterializationIssue::DecoderMemoryLimit;
  }
  return zc::Own<ArchiveInput>(zc::heap<DecodedArchiveInput>(input, impl->limits));
}

}  // namespace zomlang::compiler::driver::package
