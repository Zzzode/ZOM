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
#include "zc/ztest/test.h"
#include "zomlang/compiler/source/location.h"

namespace zomlang {
namespace compiler {
namespace source {

ZC_TEST("SourceManager: Basic Memory Buffer Operations") {
  SourceManager manager;

  // Empty Buffer Test
  BufferId emptyId = manager.addMemBufferCopy({}, "empty.zom");
  ZC_EXPECT(manager.getEntireTextForBuffer(emptyId).size() == 0);

  // Common Text Test
  zc::StringPtr content = "Hello\nWorld\n"_zc;
  BufferId bufferId = manager.addMemBufferCopy(content.asBytes(), "test.txt");
  zc::ArrayPtr<const zc::byte> text = manager.getEntireTextForBuffer(bufferId);
  ZC_EXPECT(text.size() == 12);
  ZC_EXPECT(text == content.asBytes());
}

ZC_TEST("SourceManager: File System Operations") {
  SourceManager manager;

  // Test non-existent file
  zc::Maybe<BufferId> nonexistentResult = manager.getFileSystemSourceBufferID("non.txt");
  ZC_EXPECT(nonexistentResult == zc::none);

  // Test duplicate loading
  zc::StringPtr content = "test content"_zc;
  BufferId id1 = manager.addMemBufferCopy(content.asBytes(), "same.txt");
  BufferId id2 = manager.addMemBufferCopy(content.asBytes(), "same.txt");
  // Same path but potentially different content should get different IDs (though content is same
  // here) Or perhaps the intent was: Same path should ideally return the same ID if content is
  // identical and caching is implemented? Keeping the original logic: Expect different IDs as
  // addMemBufferCopy always creates a new buffer.
  ZC_EXPECT(id1 != id2);
}

ZC_TEST("SourceManager: Buffer Identification") {
  SourceManager manager;

  // Test BufferId uniqueness
  BufferId id1 = manager.addMemBufferCopy("content1"_zcb, "file1.txt");
  BufferId id2 = manager.addMemBufferCopy("content2"_zcb, "file2.txt");

  ZC_EXPECT(id1 != id2);
  ZC_EXPECT(manager.getIdentifierForBuffer(id1) != manager.getIdentifierForBuffer(id2));
}

ZC_TEST("SourceManager: Source Location Navigation") {
  SourceManager manager;

  zc::StringPtr content =
      "Line1\n"
      "Line2\n"
      "Line3\n"_zc;

  BufferId bufferId = manager.addMemBufferCopy(content.asBytes(), "nav.txt");

  SourceLoc loc = manager.getLocForBufferStart(bufferId);
  LineAndColumn lineCol = manager.getPresumedLineAndColumnForLoc(loc, bufferId);
  ZC_EXPECT(lineCol.line == 1);
  ZC_EXPECT(lineCol.column == 1);

  SourceLoc loc2 = manager.getLocForOffset(bufferId, 6);
  LineAndColumn lineCol2 = manager.getPresumedLineAndColumnForLoc(loc2, bufferId);
  ZC_EXPECT(lineCol2.line == 2);
  ZC_EXPECT(lineCol2.column == 1);

  SourceLoc loc3 = manager.getLocForOffset(bufferId, 14);
  LineAndColumn lineCol3 = manager.getPresumedLineAndColumnForLoc(loc3, bufferId);
  ZC_EXPECT(lineCol3.line == 3);
  ZC_EXPECT(lineCol3.column == 3);
}

ZC_TEST("SourceManager: Source Ranges") {
  SourceManager manager;

  zc::StringPtr content = "Hello World"_zc;
  BufferId bufferId = manager.addMemBufferCopy(content.asBytes(), "range.txt");

  CharSourceRange range = manager.getRangeForBuffer(bufferId);
  ZC_EXPECT(range.length() == 11);
}

ZC_TEST("SourceManager: Virtual File Layering") {
  SourceManager manager;

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
  SourceManager manager;

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
  SourceManager manager;

  zc::StringPtr content = "L1\nLine2\n\nIndented\n"_zc;
  auto bufferId = manager.addMemBufferCopy(content.asBytes(), "lines.txt");

  ZC_IF_SOME(offset, manager.resolveFromLineCol(bufferId, 2, 1)) {
    auto loc = manager.getLocForOffset(bufferId, offset);
    auto lineCol = manager.getPresumedLineAndColumnForLoc(loc, bufferId);
    ZC_EXPECT(lineCol.line == 2);
    ZC_EXPECT(lineCol.column == 1);
  }
  // Add test for line 4, column 1 (start of "Indented")
  ZC_IF_SOME(offset4_1, manager.resolveFromLineCol(bufferId, 4, 1)) {
    auto loc4_1 = manager.getLocForOffset(bufferId, offset4_1);
    auto lineCol4_1 = manager.getPresumedLineAndColumnForLoc(loc4_1, bufferId);
    ZC_EXPECT(lineCol4_1.line == 4);
    ZC_EXPECT(lineCol4_1.column == 1);
  }
}

ZC_TEST("SourceManager: Content Retrieval") {
  SourceManager manager;

  zc::StringPtr content = "First Line\nSecond Line\n"_zc;
  auto bufferId = manager.addMemBufferCopy(content.asBytes(), "content.txt");

  auto text = manager.getEntireTextForBuffer(bufferId);
  ZC_EXPECT(text.size() == 23);
  ZC_EXPECT(text == content.asBytes());
}

ZC_TEST("SourceManager: Edge Cases and Error Handling") {
  SourceManager manager;

  // Test invalid BufferId
  BufferId invalidId(999999);
  ZC_EXPECT_THROW_MESSAGE("expected impl->idToBuffer.find(bufferId) != nullptr",
                          manager.getEntireTextForBuffer(invalidId));

  // Test invalid location
  SourceLoc invalidLoc;
  ZC_EXPECT_THROW_MESSAGE("Invalid source location",
                          manager.getPresumedLineAndColumnForLoc(invalidLoc, invalidId));
}

ZC_TEST("SourceManager: Performance") {
  SourceManager manager;

  // Create a large file for performance testing
  zc::Array<zc::byte> largeContent = zc::heapArray<zc::byte>(1024 * 1024, 'x');
  auto bufferId = manager.addNewSourceBuffer(zc::mv(largeContent), "large.txt");

  // Test fast location lookup
  auto loc = manager.getLocForOffset(bufferId, 1024 * 512);
  auto lineCol = manager.getPresumedLineAndColumnForLoc(loc, bufferId);
  ZC_EXPECT(lineCol.line == 1);
  ZC_EXPECT(lineCol.column == 524289);  // 1024 * 512 + 1
}

ZC_TEST("SourceManager: Special Characters") {
  SourceManager manager;

  // Includes more special characters: Unicode, Tab, Newline, Space, Null character, CRLF
  zc::StringPtr content =
      "Unicode: 你好世界\tTab\nSpace  \0Null\r\n"_zc;  // Keeping Chinese here as it's part of the
                                                       // test data
  auto bufferId = manager.addMemBufferCopy(content.asBytes(), "special.txt");

  auto text = manager.getEntireTextForBuffer(bufferId);

  // Verify basic properties
  ZC_EXPECT(text.size() == content.size());
  ZC_EXPECT(text == content.asBytes());

  // Verify position and content of special characters
  ZC_IF_SOME(offset, manager.resolveFromLineCol(
                         bufferId, 1, 9)) {  // Position of the first Chinese character '你'
    auto loc = manager.getLocForOffset(bufferId, offset);
    auto lineCol = manager.getPresumedLineAndColumnForLoc(loc, bufferId);
    ZC_EXPECT(lineCol.line == 1);
    ZC_EXPECT(lineCol.column == 9);
  }
  // Add test for Tab position
  ZC_IF_SOME(offsetTab,
             manager.resolveFromLineCol(
                 bufferId, 1,
                 16)) {  // Position after "你好世界" (assuming 6 bytes for Chinese chars) + \t
    auto locTab = manager.getLocForOffset(bufferId, offsetTab);
    auto lineColTab = manager.getPresumedLineAndColumnForLoc(locTab, bufferId);
    ZC_EXPECT(lineColTab.line == 1);
    ZC_EXPECT(lineColTab.column == 16);  // Column after Tab
  }
  // Add test for Null character position
  ZC_IF_SOME(offsetNull, manager.resolveFromLineCol(bufferId, 2, 8)) {  // Position of Null char
    auto locNull = manager.getLocForOffset(bufferId, offsetNull);
    auto lineColNull = manager.getPresumedLineAndColumnForLoc(locNull, bufferId);
    ZC_EXPECT(lineColNull.line == 2);
    ZC_EXPECT(lineColNull.column == 8);
  }
}

ZC_TEST("SourceManager: Content Comparison") {
  SourceManager manager;

  zc::StringPtr content1 = "Hello World"_zc;
  zc::StringPtr content2 = "Hello World"_zc;

  auto id1 = manager.addMemBufferCopy(content1.asBytes(), "file1.txt");
  auto id2 = manager.addMemBufferCopy(content2.asBytes(), "file2.txt");

  // Use zc::ArrayPtr for content comparison
  auto text1 = manager.getEntireTextForBuffer(id1);
  auto text2 = manager.getEntireTextForBuffer(id2);

  ZC_EXPECT(text1 == text2);  // ArrayPtr direct comparison
  ZC_EXPECT(text1.size() == content1.size());
}

}  // namespace source
}  // namespace compiler
}  // namespace zomlang
