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
// See the License for the specific language governing permissions and
// limitations under the License.

// Cover PackageSourceInventory: the pure `from` constructor (sorting and
// duplicate rejection) and `walk`, the reusable directory walk extracted from the
// zomc driver. `walk` collects regular files recursively and — deliberately,
// matching the prior CLI behavior — silently skips a file whose path has a
// non-canonical component rather than failing the whole package. The silent-skip
// case is a regression lock: changing `walk` to fail closed would change the CLI's
// admitted-file set.

#include "compiler/driver/package/source-inventory.h"

#include "compiler/identity/key/package-key.h"
#include "zc/core/filesystem.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::driver::package {
namespace {

identity::CanonicalRelativePath path(zc::StringPtr first, zc::StringPtr second) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  for (const auto text : {first, second}) {
    auto segment = identity::CanonicalPathSegment::fromCanonical(text);
    ZC_IF_SOME(admitted, segment) { segments.add(zc::mv(admitted)); }
  }
  ZC_REQUIRE(segments.size() == 2);
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

// Writes a file at `path` (slash-separated) with trivial content into `dir`.
void writeFile(zc::Directory& dir, zc::StringPtr filePath) {
  zc::Vector<zc::String> parts;
  size_t start = 0;
  for (size_t i = 0; i <= filePath.size(); ++i) {
    if (i == filePath.size() || filePath[i] == '/') {
      parts.add(zc::heapString(filePath.slice(start, i)));
      start = i + 1;
    }
  }
  dir.openFile(zc::Path(parts.releaseAsArray()),
               zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll("x"_zc);
}

// Renders an inventory's regular files as canonical "a/b/c" strings in order.
zc::Vector<zc::String> paths(const PackageSourceInventory& inventory) {
  zc::Vector<zc::String> result;
  for (const auto& file : inventory.regularFiles()) {
    zc::Vector<zc::String> segments;
    for (const auto& segment : file.segments()) { segments.add(zc::heapString(segment.text())); }
    result.add(zc::strArray(segments, "/"));
  }
  return result;
}

}  // namespace

ZC_TEST("SourceInventory.SortsRegularFilesAndRejectsDuplicates") {
  zc::Vector<identity::CanonicalRelativePath> files;
  files.add(path("src"_zc, "long_name.zom"_zc));
  files.add(path("src"_zc, "x.zom"_zc));
  auto inventory = PackageSourceInventory::from(zc::mv(files));
  ZC_REQUIRE(inventory != zc::none);
  ZC_IF_SOME(admitted, inventory) {
    ZC_REQUIRE(admitted.regularFiles().size() == 2);
    ZC_EXPECT(admitted.regularFiles()[0].segments()[1].text() == "x.zom"_zc);
    ZC_EXPECT(admitted.regularFiles()[1].segments()[1].text() == "long_name.zom"_zc);
    ZC_EXPECT(admitted.containsRegularFile(path("src"_zc, "x.zom"_zc)));
    ZC_EXPECT(!admitted.containsRegularFile(path("src"_zc, "missing.zom"_zc)));
  }

  zc::Vector<identity::CanonicalRelativePath> duplicates;
  duplicates.add(path("src"_zc, "x.zom"_zc));
  duplicates.add(path("src"_zc, "x.zom"_zc));
  ZC_EXPECT(PackageSourceInventory::from(zc::mv(duplicates)) == zc::none);
}

ZC_TEST("PackageSourceInventory::walk collects nested files sorted by canonical bytes") {
  auto dir = zc::newInMemoryDirectory(zc::nullClock());
  writeFile(*dir, "src/main.zom"_zc);
  writeFile(*dir, "src/lib.zom"_zc);
  writeFile(*dir, "Zom.toml"_zc);
  writeFile(*dir, "src/util/helper.zom"_zc);

  auto inventory = PackageSourceInventory::walk(*dir);
  ZC_REQUIRE(inventory != zc::none);
  auto names = paths(ZC_ASSERT_NONNULL(inventory));
  ZC_REQUIRE(names.size() == 4);
  // Sorted by canonical encoding: "Zom.toml", then the "src/*" entries in order.
  ZC_EXPECT(names[0] == "Zom.toml"_zc);
  ZC_EXPECT(names[1] == "src/lib.zom"_zc);
  ZC_EXPECT(names[2] == "src/main.zom"_zc);
  ZC_EXPECT(names[3] == "src/util/helper.zom"_zc);
}

ZC_TEST("PackageSourceInventory::walk returns an empty inventory for an empty directory") {
  auto dir = zc::newInMemoryDirectory(zc::nullClock());
  auto inventory = PackageSourceInventory::walk(*dir);
  ZC_REQUIRE(inventory != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(inventory).regularFiles().size() == 0);
}

ZC_TEST("PackageSourceInventory::walk silently skips a file with a non-canonical component") {
  auto dir = zc::newInMemoryDirectory(zc::nullClock());
  writeFile(*dir, "src/main.zom"_zc);
  // A backslash is a legal filesystem component character but not an admissible
  // canonical path segment, so this file has a non-canonical component and is
  // skipped, not rejected.
  dir->openFile(zc::Path({"od\\d.zom"_zc}), zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll("x"_zc);

  auto inventory = PackageSourceInventory::walk(*dir);
  ZC_REQUIRE(inventory != zc::none);
  auto names = paths(ZC_ASSERT_NONNULL(inventory));
  // Only the canonical file survives; the walk did not fail the whole package.
  ZC_REQUIRE(names.size() == 1);
  ZC_EXPECT(names[0] == "src/main.zom"_zc);
}

}  // namespace zomlang::compiler::driver::package
