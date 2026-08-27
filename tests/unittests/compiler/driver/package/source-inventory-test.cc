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

#include "compiler/driver/package/source-inventory.h"

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

}  // namespace zomlang::compiler::driver::package
