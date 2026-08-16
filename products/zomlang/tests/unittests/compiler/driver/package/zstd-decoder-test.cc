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

#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::driver::package {
namespace {

constexpr zc::byte kHelloFrame[] = {0x28, 0xb5, 0x2f, 0xfd, 0x04, 0x58, 0x51, 0x00,
                                    0x00, 0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x7a,
                                    0x73, 0x74, 0x64, 0xcf, 0xdb, 0x60, 0x9c};
constexpr zc::byte kHelloText[] = {0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x7a, 0x73, 0x74, 0x64};

class MemoryInput final : public ZstdInput {
public:
  explicit MemoryInput(zc::ArrayPtr<const zc::byte> bytes, size_t chunkSize = SIZE_MAX)
      : bytes(bytes), chunkSize(chunkSize) {}

  ZstdInputResult read(zc::ArrayPtr<zc::byte> destination) override {
    ZC_IF_SOME(issue, failure) { return issue; }
    if (position == bytes.size()) { return ZstdInputEnd{}; }
    const size_t byteCount =
        zc::min(zc::min(destination.size(), chunkSize), bytes.size() - position);
    for (size_t index = 0; index < byteCount; ++index) {
      destination[index] = bytes[position + index];
    }
    position += byteCount;
    return ZstdInputData{byteCount};
  }

  void failWith(MaterializationIssue issue) { failure = issue; }

private:
  zc::ArrayPtr<const zc::byte> bytes;
  size_t chunkSize;
  size_t position = 0;
  zc::Maybe<MaterializationIssue> failure;
};

class MemoryOutput final : public ZstdOutput {
public:
  zc::Maybe<MaterializationIssue> write(zc::ArrayPtr<const zc::byte> bytes) override {
    ZC_IF_SOME(issue, failure) { return issue; }
    value.addAll(bytes);
    return zc::none;
  }

  void failWith(MaterializationIssue issue) { failure = issue; }

  zc::Vector<zc::byte> value;

private:
  zc::Maybe<MaterializationIssue> failure;
};

zc::ArrayPtr<const zc::byte> helloFrame() {
  return zc::ArrayPtr<const zc::byte>(kHelloFrame, sizeof(kHelloFrame));
}

bool equals(zc::ArrayPtr<const zc::byte> left, zc::ArrayPtr<const zc::byte> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index] != right[index]) { return false; }
  }
  return true;
}

}  // namespace

ZC_TEST("ZstdDecoder.DecodesOneFragmentedFrame") {
  MemoryInput input(helloFrame(), 1);
  MemoryOutput output;
  ZstdDecoder decoder;

  ZC_EXPECT(decoder.decode(input, output) == zc::none);
  ZC_EXPECT(
      equals(output.value.asPtr(), zc::ArrayPtr<const zc::byte>(kHelloText, sizeof(kHelloText))));
}

ZC_TEST("ZstdDecoder.RejectsTrailingFrame") {
  zc::Vector<zc::byte> frames;
  frames.addAll(helloFrame());
  frames.addAll(helloFrame());
  MemoryInput input(frames.asPtr());
  MemoryOutput output;
  ZstdDecoder decoder;

  ZC_EXPECT(decoder.decode(input, output) == MaterializationIssue::TrailingArchiveData);
}

ZC_TEST("ZstdDecoder.RejectsTruncatedFrame") {
  MemoryInput input(helloFrame().first(10));
  MemoryOutput output;
  ZstdDecoder decoder;

  ZC_EXPECT(decoder.decode(input, output) == MaterializationIssue::ArchiveDecodeFailed);
}

ZC_TEST("ZstdDecoder.EnforcesCompressedByteLimit") {
  SourceAdmissionLimits limits;
  limits.compressedArchiveBytes = 8;
  MemoryInput input(helloFrame());
  MemoryOutput output;
  ZstdDecoder decoder(limits);

  ZC_EXPECT(decoder.decode(input, output) == MaterializationIssue::CompressedSizeLimit);
}

ZC_TEST("ZstdDecoder.EnforcesWorkingMemoryLimit") {
  SourceAdmissionLimits limits;
  limits.decoderWorkingBytes = 1;
  MemoryInput input(helloFrame());
  MemoryOutput output;
  ZstdDecoder decoder(limits);

  ZC_EXPECT(decoder.decode(input, output) == MaterializationIssue::DecoderMemoryLimit);
}

ZC_TEST("ZstdDecoder.ForwardsTypedSourceFailure") {
  MemoryInput input(helloFrame());
  input.failWith(MaterializationIssue::SourceChangedDuringSnapshot);
  MemoryOutput output;
  ZstdDecoder decoder;

  ZC_EXPECT(decoder.decode(input, output) == MaterializationIssue::SourceChangedDuringSnapshot);
}

ZC_TEST("ZstdDecoder.ForwardsTypedSinkFailure") {
  MemoryInput input(helloFrame());
  MemoryOutput output;
  output.failWith(MaterializationIssue::ArchiveMetadataLimit);
  ZstdDecoder decoder;

  ZC_EXPECT(decoder.decode(input, output) == MaterializationIssue::ArchiveMetadataLimit);
}

}  // namespace zomlang::compiler::driver::package
