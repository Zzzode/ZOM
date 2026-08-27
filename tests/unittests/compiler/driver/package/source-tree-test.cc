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

#include "compiler/driver/package/source-tree.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::driver::package {
namespace {

void addFile(SourceTreeBuilder& builder, zc::StringPtr path, zc::StringPtr contents) {
  ZC_REQUIRE(builder.beginFile(path, contents.size()) == zc::none);
  ZC_REQUIRE(builder.write(contents.asBytes()) == zc::none);
  ZC_REQUIRE(builder.endFile() == zc::none);
}

SourceTreeRecord buildRecord(bool reverse) {
  SourceTreeBuilder builder;
  if (reverse) {
    addFile(builder, "src/main.zom"_zc, "main"_zc);
    addFile(builder, "Zom.toml"_zc, "manifest"_zc);
  } else {
    addFile(builder, "Zom.toml"_zc, "manifest"_zc);
    addFile(builder, "src/main.zom"_zc, "main"_zc);
  }
  auto result = builder.finish();
  if (result.is<SourceTreeRecord>()) { return zc::mv(result.get<SourceTreeRecord>()); }
  ZC_FAIL_REQUIRE("source tree fixture was rejected");
}

MaterializationIssue secondPathIssue(zc::StringPtr first, zc::StringPtr second) {
  SourceTreeBuilder builder;
  addFile(builder, first, "a"_zc);
  auto issue = builder.beginFile(second, 1);
  ZC_IF_SOME(value, issue) { return value; }
  ZC_FAIL_REQUIRE("colliding source path was admitted");
}

}  // namespace

ZC_TEST("SourceTreeTest.IncrementalSha256MatchesStandardVector") {
  identity::Sha256Hasher hasher;
  ZC_REQUIRE(hasher.update("a"_zc.asBytes()));
  ZC_REQUIRE(hasher.update("b"_zc.asBytes()));
  ZC_REQUIRE(hasher.update("c"_zc.asBytes()));
  auto digest = hasher.finish();
  ZC_IF_SOME(value, digest) {
    ZC_EXPECT(zc::encodeHex(value.bytes()) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"_zc);
  } else {
    ZC_FAIL_EXPECT("incremental SHA-256 did not finish");
  }
  ZC_EXPECT(hasher.finish() == zc::none);
}

ZC_TEST("SourceTreeTest.ClassifiesPathCollisionsByRequiredPriority") {
  ZC_EXPECT(secondPathIssue("same.zom"_zc, "same.zom"_zc) == MaterializationIssue::DuplicatePath);
  ZC_EXPECT(secondPathIssue("\xC3\xA9.zom"_zc, "e\xCC\x81.zom"_zc) ==
            MaterializationIssue::UnicodeCollision);
  ZC_EXPECT(secondPathIssue("A.zom"_zc, "a.zom"_zc) == MaterializationIssue::CaseFoldCollision);
  ZC_EXPECT(secondPathIssue("\xC3\x9F.zom"_zc, "ss.zom"_zc) ==
            MaterializationIssue::CaseFoldCollision);
}

ZC_TEST("SourceTreeTest.SortsInventoryAndProducesPermutationInvariantDigest") {
  auto forward = buildRecord(false);
  auto reverse = buildRecord(true);

  ZC_REQUIRE(forward.files().size() == 2);
  ZC_EXPECT(forward.files()[0].path().segments()[0].text() == "Zom.toml"_zc);
  ZC_EXPECT(forward.files()[1].path().segments()[0].text() == "src"_zc);
  ZC_EXPECT(forward.digest() == reverse.digest());
  ZC_EXPECT(zc::encodeHex(forward.digest().bytes()) ==
            "d9561490cd6762984ced6d62ec14b571808135bf8ac786bac1aeae0b2375e717"_zc);
}

ZC_TEST("SourceTreeTest.RejectsInvalidPathsBeforeReadingContent") {
  SourceTreeBuilder builder;
  ZC_EXPECT(builder.beginFile("/absolute.zom"_zc, 0) == MaterializationIssue::AbsolutePath);

  SourceTreeBuilder parent;
  ZC_EXPECT(parent.beginFile("../outside.zom"_zc, 0) == MaterializationIssue::ParentPath);

  SourceTreeBuilder backslash;
  ZC_EXPECT(backslash.beginFile("src\\main.zom"_zc, 0) == MaterializationIssue::BackslashPath);
}

}  // namespace zomlang::compiler::driver::package
