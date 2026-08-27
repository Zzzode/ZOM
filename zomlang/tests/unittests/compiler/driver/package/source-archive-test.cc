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

#include "zomlang/compiler/driver/package/source-archive.h"

#include "source-archive-test-data.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::driver::package {
namespace {

class MemoryZstdInput final : public ZstdInput {
public:
  MemoryZstdInput(zc::ArrayPtr<const zc::byte> bytes, size_t chunkSize)
      : bytes(bytes), chunkSize(chunkSize) {}

  ZstdInputResult read(zc::ArrayPtr<zc::byte> destination) override {
    if (position == bytes.size()) { return ZstdInputEnd{}; }
    const size_t count = zc::min(zc::min(destination.size(), chunkSize), bytes.size() - position);
    for (size_t index = 0; index < count; ++index) { destination[index] = bytes[position + index]; }
    position += count;
    return ZstdInputData{count};
  }

private:
  zc::ArrayPtr<const zc::byte> bytes;
  size_t chunkSize;
  size_t position = 0;
};

}  // namespace

ZC_TEST("SourceArchiveTest.StreamsZstandardIntoUstarAdmission") {
  MemoryZstdInput input(zc::arrayPtr(test::kCompressedUstar), 13);
  SourceArchiveAdmission admission;

  auto result = admission.admit(input);
  ZC_REQUIRE(result.is<SourceTreeRecord>());
  const auto& record = result.get<SourceTreeRecord>();
  ZC_REQUIRE(record.files().size() == 1);
  ZC_EXPECT(record.files()[0].path().segments()[0].text() == "src"_zc);
  ZC_EXPECT(record.files()[0].path().segments()[1].text() == "lib.zom"_zc);
  ZC_EXPECT(record.files()[0].byteLength() == 7);
}

}  // namespace zomlang::compiler::driver::package
