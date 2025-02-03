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
#include "zc/core/string.h"
#include "zc/ztest/gtest.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/source/module.h"

namespace zomlang {
namespace compiler {

class TestClock final : public zc::Clock {
public:
  void tick() { time += 1 * zc::SECONDS; }

  zc::Date now() const override { return time; }

  void expectChanged(const zc::FsNode& file) {
    ZC_EXPECT(file.stat().lastModified == time);
    time += 1 * zc::SECONDS;
  }
  void expectUnchanged(const zc::FsNode& file) { ZC_EXPECT(file.stat().lastModified != time); }

private:
  zc::Date time = zc::UNIX_EPOCH + 1 * zc::SECONDS;
};

ZC_TEST("ModuleLoader basic") {
  TestClock clock;

  auto dir = newInMemoryDirectory(clock);
  clock.expectChanged(*dir);

  zc::Own<const zc::Directory> dir1 = dir->openSubdir(zc::Path("dir1"), zc::WriteMode::CREATE);
  clock.expectChanged(*dir);
  dir1->openFile(zc::Path("mod.zl"), zc::WriteMode::CREATE);
  clock.expectChanged(*dir1);

  zc::Own<const zc::Directory> dir2 = dir->openSubdir(zc::Path("dir2"), zc::WriteMode::CREATE);
  clock.expectChanged(*dir);
  dir2->openFile(zc::Path("mod.zl"), zc::WriteMode::CREATE);
  clock.expectChanged(*dir2);

  ModuleLoader loader;

  // Testing for normal loading
  auto subdir1 = dir->openSubdir(zc::Path("dir1"));
  clock.expectUnchanged(*dir);
  clock.expectUnchanged(*subdir1);
  auto mod1 = loader.loadModule(*subdir1, zc::Path("mod.zl"));
  ZC_EXPECT(mod1 != zc::none);

  // Testing reloading
  auto mod2 = loader.loadModule(*subdir1, zc::Path("mod.zl"));
  ZC_EXPECT(mod2 != zc::none);

  auto maybeModule1 = loader.loadModule(*subdir1, zc::Path("mod.zl"));
  auto maybeModule2 = loader.loadModule(*subdir1, zc::Path("mod.zl"));
  ZC_EXPECT(maybeModule1 != zc::none);
  ZC_EXPECT(maybeModule2 != zc::none);

  ZC_IF_SOME(module1, maybeModule1) {
    ZC_IF_SOME(module2, maybeModule2) { ZC_EXPECT(module1 == module2); }
    // Test files with the same name in different directories
    auto subdir2 = dir->openSubdir(zc::Path("dir2"));
    clock.expectUnchanged(*dir);
    auto maybeModule3 = loader.loadModule(*subdir2, zc::Path("mod.zl"));
    ZC_EXPECT(maybeModule3 != zc::none);
    ZC_IF_SOME(module3, maybeModule3) { ZC_EXPECT(module1 != module3); }
  }
}

ZC_TEST("ModuleLoader LoadDuplicateFiles") {
  TestClock clock;

  auto dir = newInMemoryDirectory(clock);
  clock.expectChanged(*dir);

  auto dir1 = dir->openSubdir(zc::Path("dir1"), zc::WriteMode::CREATE);
  clock.expectChanged(*dir);
  auto file1 = dir1->openFile(zc::Path("test.zl"), zc::WriteMode::CREATE);

  auto dir2 = dir->openSubdir(zc::Path("dir2"), zc::WriteMode::CREATE);
  clock.expectChanged(*dir);
  auto file2 = dir2->openFile(zc::Path("test.zl"), zc::WriteMode::CREATE);

  ModuleLoader loader;
  auto readableDir1 = dir->openSubdir(zc::Path("dir1"));
  clock.expectUnchanged(*dir);
  clock.expectUnchanged(*readableDir1);

  auto maybeModule1 = loader.loadModule(*readableDir1, zc::Path("test.zl"));
  auto maybeModule2 = loader.loadModule(*readableDir1, zc::Path("test.zl"));
  ZC_EXPECT(maybeModule1 != zc::none);
  ZC_EXPECT(maybeModule2 != zc::none);

  ZC_IF_SOME(module1, maybeModule1) {
    ZC_IF_SOME(module2, maybeModule2) { ZC_EXPECT(module1 == module2); }
  }
}

}  // namespace compiler
}  // namespace zomlang