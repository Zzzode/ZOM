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

#include "zomlang/compiler/driver/package/archive-reader.h"

#include <cstdint>

#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::driver::package {
namespace {

constexpr size_t kTarBlockSize = 512;

class MemoryArchiveInput final : public ArchiveInput {
public:
  explicit MemoryArchiveInput(zc::ArrayPtr<const zc::byte> bytes, size_t chunkSize = SIZE_MAX)
      : bytes(bytes), chunkSize(chunkSize) {}

  ArchiveInputResult read(zc::ArrayPtr<zc::byte> destination) override {
    ZC_IF_SOME(issue, failure) { return issue; }
    if (position == bytes.size()) { return ArchiveInputEnd{}; }
    const size_t count = zc::min(zc::min(destination.size(), chunkSize), bytes.size() - position);
    for (size_t index = 0; index < count; ++index) { destination[index] = bytes[position + index]; }
    position += count;
    return ArchiveInputData{count};
  }

  void failWith(MaterializationIssue issue) { failure = issue; }

private:
  zc::ArrayPtr<const zc::byte> bytes;
  size_t chunkSize;
  size_t position = 0;
  zc::Maybe<MaterializationIssue> failure;
};

class MemoryArchiveOutput final : public ArchiveOutput {
public:
  zc::Maybe<MaterializationIssue> beginFile(zc::StringPtr path, uint64_t byteLength) override {
    ZC_IF_SOME(issue, failure) { return issue; }
    ZC_REQUIRE(!active);
    active = true;
    currentPath = zc::heapString(path);
    expectedSize = byteLength;
    return zc::none;
  }

  zc::Maybe<MaterializationIssue> write(zc::ArrayPtr<const zc::byte> bytes) override {
    ZC_IF_SOME(issue, failure) { return issue; }
    ZC_REQUIRE(active);
    value.addAll(bytes);
    return zc::none;
  }

  zc::Maybe<MaterializationIssue> endFile() override {
    ZC_IF_SOME(issue, failure) { return issue; }
    ZC_REQUIRE(active);
    ZC_EXPECT(value.size() == expectedSize);
    active = false;
    ++fileCount;
    return zc::none;
  }

  void failWith(MaterializationIssue issue) { failure = issue; }

  zc::String currentPath;
  zc::Vector<zc::byte> value;
  size_t fileCount = 0;

private:
  uint64_t expectedSize = 0;
  bool active = false;
  zc::Maybe<MaterializationIssue> failure;
};

void appendZeroBlock(zc::Vector<zc::byte>& bytes) {
  for (size_t index = 0; index < kTarBlockSize; ++index) { bytes.add(0); }
}

void writeText(zc::ArrayPtr<zc::byte> block, size_t offset, zc::StringPtr text) {
  ZC_REQUIRE(offset + text.size() <= block.size());
  for (size_t index = 0; index < text.size(); ++index) {
    block[offset + index] = static_cast<zc::byte>(text[index]);
  }
}

void writeOctal(zc::ArrayPtr<zc::byte> block, size_t offset, size_t width, uint64_t value) {
  ZC_REQUIRE(width >= 2);
  block[offset + width - 1] = 0;
  for (size_t index = width - 1; index > 0; --index) {
    block[offset + index - 1] = static_cast<zc::byte>('0' + (value & 7));
    value >>= 3;
  }
  ZC_REQUIRE(value == 0);
}

zc::Vector<zc::byte> makeUstar(zc::StringPtr path, zc::ArrayPtr<const zc::byte> data,
                               char type = '0', zc::StringPtr link = zc::StringPtr()) {
  zc::Vector<zc::byte> bytes;
  appendZeroBlock(bytes);
  auto header = bytes.asPtr().first(kTarBlockSize);
  writeText(header, 0, path);
  writeOctal(header, 100, 8, 0644);
  writeOctal(header, 108, 8, 0);
  writeOctal(header, 116, 8, 0);
  writeOctal(header, 124, 12, data.size());
  writeOctal(header, 136, 12, 0);
  for (size_t index = 148; index < 156; ++index) { header[index] = ' '; }
  header[156] = static_cast<zc::byte>(type);
  writeText(header, 157, link);
  writeText(header, 257, zc::StringPtr("ustar"));
  header[262] = 0;
  writeText(header, 263, zc::StringPtr("00"));

  uint64_t checksum = 0;
  for (zc::byte value : header) { checksum += value; }
  writeOctal(header, 148, 7, checksum);
  header[155] = ' ';

  bytes.addAll(data);
  while (bytes.size() % kTarBlockSize != 0) { bytes.add(0); }
  appendZeroBlock(bytes);
  appendZeroBlock(bytes);
  return bytes;
}

constexpr zc::byte kPayload[] = {'h', 'e', 'l', 'l', 'o'};

}  // namespace

ZC_TEST("ArchiveReader.ReadsFragmentedPosixUstar") {
  auto archive = makeUstar("src/lib.zom"_zc, zc::arrayPtr(kPayload));
  MemoryArchiveInput input(archive.asPtr(), 37);
  MemoryArchiveOutput output;
  ArchiveReader reader;

  ZC_EXPECT(reader.read(input, output) == zc::none);
  ZC_EXPECT(output.fileCount == 1);
  ZC_EXPECT(output.currentPath == "src/lib.zom"_zc);
  ZC_EXPECT(output.value.size() == sizeof(kPayload));
}

ZC_TEST("ArchiveReader.RejectsSymlinksAndHardLinks") {
  auto symlink = makeUstar("src/link.zom"_zc, zc::ArrayPtr<const zc::byte>(), '2', "lib.zom"_zc);
  MemoryArchiveInput symlinkInput(symlink.asPtr());
  MemoryArchiveOutput symlinkOutput;
  ArchiveReader reader;
  ZC_EXPECT(reader.read(symlinkInput, symlinkOutput) == MaterializationIssue::Symlink);

  auto hardLink = makeUstar("src/hard.zom"_zc, zc::ArrayPtr<const zc::byte>(), '1', "lib.zom"_zc);
  MemoryArchiveInput hardLinkInput(hardLink.asPtr());
  MemoryArchiveOutput hardLinkOutput;
  ZC_EXPECT(reader.read(hardLinkInput, hardLinkOutput) == MaterializationIssue::HardLink);

  auto directory = makeUstar("src"_zc, zc::ArrayPtr<const zc::byte>(), '5');
  MemoryArchiveInput directoryInput(directory.asPtr());
  MemoryArchiveOutput directoryOutput;
  ZC_EXPECT(reader.read(directoryInput, directoryOutput) == MaterializationIssue::SpecialFile);
}

ZC_TEST("ArchiveReader.EnforcesHeaderAndFileLimits") {
  auto archive = makeUstar("src/lib.zom"_zc, zc::arrayPtr(kPayload));
  SourceAdmissionLimits headerLimits;
  headerLimits.archiveHeaderCount = 0;
  MemoryArchiveInput headerInput(archive.asPtr());
  MemoryArchiveOutput headerOutput;
  ArchiveReader headerReader(headerLimits);
  ZC_EXPECT(headerReader.read(headerInput, headerOutput) ==
            MaterializationIssue::ArchiveHeaderLimit);

  SourceAdmissionLimits fileLimits;
  fileLimits.singleFileBytes = 4;
  MemoryArchiveInput fileInput(archive.asPtr());
  MemoryArchiveOutput fileOutput;
  ArchiveReader fileReader(fileLimits);
  ZC_EXPECT(fileReader.read(fileInput, fileOutput) == MaterializationIssue::FileTooLarge);

  SourceAdmissionLimits metadataLimits;
  metadataLimits.archiveMetadataBytes = 512 + zc::StringPtr("src/lib.zom").size() + 506;
  MemoryArchiveInput metadataInput(archive.asPtr());
  MemoryArchiveOutput metadataOutput;
  ArchiveReader metadataReader(metadataLimits);
  ZC_EXPECT(metadataReader.read(metadataInput, metadataOutput) ==
            MaterializationIssue::ArchiveMetadataLimit);
}

ZC_TEST("ArchiveReader.RejectsTrailingArchiveData") {
  auto archive = makeUstar("src/lib.zom"_zc, zc::arrayPtr(kPayload));
  archive.add(0x7f);
  MemoryArchiveInput input(archive.asPtr());
  MemoryArchiveOutput output;
  ArchiveReader reader;

  ZC_EXPECT(reader.read(input, output) == MaterializationIssue::TrailingArchiveData);
}

ZC_TEST("ArchiveReader.ForwardsTypedInputAndOutputFailures") {
  auto archive = makeUstar("src/lib.zom"_zc, zc::arrayPtr(kPayload));
  MemoryArchiveInput input(archive.asPtr());
  input.failWith(MaterializationIssue::SourceChangedDuringSnapshot);
  MemoryArchiveOutput output;
  ArchiveReader reader;
  ZC_EXPECT(reader.read(input, output) == MaterializationIssue::SourceChangedDuringSnapshot);

  MemoryArchiveInput outputInput(archive.asPtr());
  MemoryArchiveOutput failedOutput;
  failedOutput.failWith(MaterializationIssue::DestinationWriteFailed);
  ZC_EXPECT(reader.read(outputInput, failedOutput) == MaterializationIssue::DestinationWriteFailed);
}

}  // namespace zomlang::compiler::driver::package
