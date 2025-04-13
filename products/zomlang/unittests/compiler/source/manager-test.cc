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
#include "zomlang/compiler/source/location.h"

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

ZC_TEST("SourceManager: Basic Memory Buffer Operations") {
  source::SourceManager manager;

  // Empty Buffer Test
  source::BufferId emptyId = manager.addMemBufferCopy({}, "empty.zom");
  ZC_EXPECT(manager.getEntireTextForBuffer(emptyId).size() == 0);

  // Common Text Test
  zc::StringPtr content = "Hello\nWorld\n"_zc;
  source::BufferId bufferId = manager.addMemBufferCopy(content.asBytes(), "test.txt");
  zc::ArrayPtr<const zc::byte> text = manager.getEntireTextForBuffer(bufferId);
  ZC_EXPECT(text.size() == 12);
  ZC_EXPECT(text == content);
}

ZC_TEST("SourceManager: File System Operations") {
  source::SourceManager manager;

  // 测试不存在的文件
  zc::Maybe<source::BufferId> nonexistentResult = manager.getFileSystemSourceBufferID("non.txt");
  ZC_EXPECT(nonexistentResult == zc::none);

  // 测试重复加载
  zc::StringPtr content = "test content"_zc;
  source::BufferId id1 = manager.addMemBufferCopy(content.asBytes(), "same.txt");
  source::BufferId id2 = manager.addMemBufferCopy(content.asBytes(), "same.txt");
  ZC_EXPECT(id1 != id2);  // 相同路径但是不同内容应该得到不同的ID
}

ZC_TEST("SourceManager: Buffer Identification") {
  source::SourceManager manager;

  // 测试BufferId唯一性
  source::BufferId id1 = manager.addMemBufferCopy("content1"_zcb, "file1.txt");
  source::BufferId id2 = manager.addMemBufferCopy("content2"_zcb, "file2.txt");

  ZC_EXPECT(id1 != id2);
  ZC_EXPECT(manager.getIdentifierForBuffer(id1) != manager.getIdentifierForBuffer(id2));
}

ZC_TEST("SourceManager: Source Location Navigation") {
  source::SourceManager manager;

  zc::StringPtr content =
      "Line1\n"
      "Line2\n"
      "Line3\n"_zc;

  source::BufferId bufferId = manager.addMemBufferCopy(content.asBytes(), "nav.txt");

  source::SourceLoc loc = manager.getLocForBufferStart(bufferId);
  source::LineAndColumn lineCol = manager.getPresumedLineAndColumnForLoc(loc);
  ZC_EXPECT(lineCol.line == 1);
  ZC_EXPECT(lineCol.column == 1);

  source::SourceLoc loc2 = manager.getLocForOffset(bufferId, 6);
  source::LineAndColumn lineCol2 = manager.getPresumedLineAndColumnForLoc(loc2);
  ZC_EXPECT(lineCol2.line == 2);
  ZC_EXPECT(lineCol2.column == 1);
}

ZC_TEST("SourceManager: Source Ranges") {
  source::SourceManager manager;

  zc::StringPtr content = "Hello World"_zc;
  source::BufferId bufferId = manager.addMemBufferCopy(content.asBytes(), "range.txt");

  source::CharSourceRange range = manager.getRangeForBuffer(bufferId);
  ZC_EXPECT(range.length() == 11);
}

ZC_TEST("SourceManager: Virtual File Layering") {
  source::SourceManager manager;

  zc::StringPtr content = "base content\nfor testing\n"_zc;
  auto bufferId = manager.addMemBufferCopy(content.asBytes(), "base.txt");

  auto loc = manager.getLocForBufferStart(bufferId);
  manager.createVirtualFile(loc, "virtual.txt", 10, 12);

  ZC_IF_SOME(vf, manager.getVirtualFile(loc)) {
    ZC_EXPECT(vf.name == "virtual.txt"_zc);
    ZC_EXPECT(vf.lineOffset == 10);
  }
}

ZC_TEST("SourceManager: Virtual File Interactions") {
  source::SourceManager manager;

  zc::StringPtr content = "content\nfor\nvirtual\nfile\n"_zc;
  auto bufferId = manager.addMemBufferCopy(content.asBytes(), "base.txt");

  auto loc1 = manager.getLocForBufferStart(bufferId);
  auto loc2 = manager.getLocForOffset(bufferId, 8);

  manager.createVirtualFile(loc1, "v1.txt", 5, 7);
  manager.createVirtualFile(loc2, "v2.txt", 10, 3);

  ZC_IF_SOME(vf1, manager.getVirtualFile(loc1)) { ZC_EXPECT(vf1.name == "v1.txt"); }
  ZC_IF_SOME(vf2, manager.getVirtualFile(loc2)) { ZC_EXPECT(vf2.name == "v2.txt"); }
}

ZC_TEST("SourceManager: Line Column Operations") {
  source::SourceManager manager;

  zc::StringPtr content = "L1\nLine2\n\nIndented\n"_zc;
  auto bufferId = manager.addMemBufferCopy(content.asBytes(), "lines.txt");

  ZC_IF_SOME(offset, manager.resolveFromLineCol(bufferId, 2, 1)) {
    auto loc = manager.getLocForOffset(bufferId, offset);
    auto lineCol = manager.getPresumedLineAndColumnForLoc(loc);
    ZC_EXPECT(lineCol.line == 2);
    ZC_EXPECT(lineCol.column == 1);
  }
}

ZC_TEST("SourceManager: Content Retrieval") {
  source::SourceManager manager;

  zc::StringPtr content = "First Line\nSecond Line\n"_zc;
  auto bufferId = manager.addMemBufferCopy(content.asBytes(), "content.txt");

  auto text = manager.getEntireTextForBuffer(bufferId);
  ZC_EXPECT(text.size() == 22);
  ZC_EXPECT(text == content);
}

ZC_TEST("SourceManager: Edge Cases and Error Handling") {
  source::SourceManager manager;

  // 测试无效的BufferId
  source::BufferId invalidId(999999);
  ZC_EXPECT_THROW(FAILED, manager.getEntireTextForBuffer(invalidId));

  // 测试无效的位置
  source::SourceLoc invalidLoc;
  auto lineCol = manager.getPresumedLineAndColumnForLoc(invalidLoc);
  ZC_EXPECT(lineCol.line == 0);
  ZC_EXPECT(lineCol.column == 0);
}

ZC_TEST("SourceManager: Performance") {
  source::SourceManager manager;

  // 创建一个大文件进行性能测试
  zc::Array<zc::byte> largeContent = zc::heapArray<zc::byte>(1024 * 1024, 'x');
  auto bufferId = manager.addNewSourceBuffer(zc::mv(largeContent), "large.txt");

  // 测试快速定位
  auto loc = manager.getLocForOffset(bufferId, 1024 * 512);
  auto lineCol = manager.getPresumedLineAndColumnForLoc(loc);
  ZC_EXPECT(lineCol.line == 1);
}

ZC_TEST("SourceManager: Special Characters") {
  source::SourceManager manager;

  // 包含更多特殊字符：Unicode、制表符、换行符、空格、null字符
  zc::StringPtr content = "Unicode: 你好世界\tTab\nSpace  \0Null\r\n"_zc;
  auto bufferId = manager.addMemBufferCopy(content.asBytes(), "special.txt");

  auto text = manager.getEntireTextForBuffer(bufferId);

  // 验证基本属性
  ZC_EXPECT(text.size() == content.size());
  ZC_EXPECT(text == content);

  // 验证特殊字符的位置和内容
  ZC_IF_SOME(offset, manager.resolveFromLineCol(bufferId, 1, 9)) {
    auto loc = manager.getLocForOffset(bufferId, offset);
    auto lineCol = manager.getPresumedLineAndColumnForLoc(loc);
    ZC_EXPECT(lineCol.line == 1);
    ZC_EXPECT(lineCol.column == 9);  // "你" 的位置
  }
}

ZC_TEST("SourceManager: Content Comparison") {
  source::SourceManager manager;

  zc::StringPtr content1 = "Hello World"_zc;
  zc::StringPtr content2 = "Hello World"_zc;

  auto id1 = manager.addMemBufferCopy(content1.asBytes(), "file1.txt");
  auto id2 = manager.addMemBufferCopy(content2.asBytes(), "file2.txt");

  // 使用 zc::ArrayPtr 进行内容比较
  auto text1 = manager.getEntireTextForBuffer(id1);
  auto text2 = manager.getEntireTextForBuffer(id2);

  ZC_EXPECT(text1 == text2);  // ArrayPtr 直接比较
  ZC_EXPECT(text1.size() == content1.size());
}

}  // namespace compiler
}  // namespace zomlang
