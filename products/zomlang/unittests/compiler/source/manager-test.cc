// Copyright (c) 2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/source/manager.h"

#include "zc/core/common.h"
#include "zc/core/filesystem.h"
#include "zc/core/string.h"
#include "zc/ztest/test.h"

namespace zomlang {
namespace compiler {

class TestClock final : public zc::Clock {
public:
  void tick() { time += 1 * zc::SECONDS; }

  [[nodiscard]] zc::Date now() const override { return time; }

  void expectChanged(const zc::FsNode& file) {
    ZC_EXPECT(file.stat().lastModified == time);
    time += 1 * zc::SECONDS;
  }
  void expectUnchanged(const zc::FsNode& file) { ZC_EXPECT(file.stat().lastModified != time); }

private:
  zc::Date time = zc::UNIX_EPOCH + 1 * zc::SECONDS;
};

ZC_TEST("SourceManager basic") {}

ZC_TEST("SourceManager LoadDuplicateFiles") {}

ZC_TEST("SourceManager TestModuleIdsUnique") {}

ZC_TEST("SourceManager TestFileContentChange") {}

ZC_TEST("SourceManager TestInvalidPath") {}

ZC_TEST("SourceManager TestSameContentDifferentPaths") {}

}  // namespace compiler
}  // namespace zomlang
