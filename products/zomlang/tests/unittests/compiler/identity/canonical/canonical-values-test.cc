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

#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"
#include "zomlang/compiler/identity/semantic/semantic-version.h"
#include "zomlang/compiler/identity/sorted-feature-set.h"

namespace zomlang::compiler::identity {
namespace {

void expectVersion(zc::StringPtr input) {
  auto admitted = ResolvedVersion::fromCanonical(input);
  bool matched = false;
  ZC_IF_SOME(value, admitted) {
    ZC_EXPECT(value.text() == input);
    auto duplicate = value.clone();
    ZC_EXPECT(duplicate == value);
    matched = true;
  }
  ZC_EXPECT(matched);
}

void expectInvalidVersion(zc::StringPtr input) {
  ZC_EXPECT(ResolvedVersion::fromCanonical(input) == zc::none);
}

FeatureName requireFeature(zc::StringPtr input) {
  auto admitted = FeatureName::fromCanonical(input);
  ZC_IF_SOME(value, admitted) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid feature in canonical set test");
}

}  // namespace

ZC_TEST("ResolvedVersion accepts the complete SemVer 2.0.0 grammar") {
  expectVersion("0.0.0"_zc);
  expectVersion("1.2.3"_zc);
  expectVersion("1.0.0-alpha"_zc);
  expectVersion("1.0.0-alpha.1-rc+Build.01-abc"_zc);
  expectVersion("999999999999999999999.0.1+001"_zc);
}

ZC_TEST("ResolvedVersion rejects non-canonical and malformed version text") {
  expectInvalidVersion(""_zc);
  expectInvalidVersion("v1.2.3"_zc);
  expectInvalidVersion("1.2"_zc);
  expectInvalidVersion("1.2.3.4"_zc);
  expectInvalidVersion("01.2.3"_zc);
  expectInvalidVersion("1.02.3"_zc);
  expectInvalidVersion("1.2.03"_zc);
  expectInvalidVersion("1.2.3-"_zc);
  expectInvalidVersion("1.2.3-alpha..1"_zc);
  expectInvalidVersion("1.2.3-01"_zc);
  expectInvalidVersion("1.2.3+"_zc);
  expectInvalidVersion("1.2.3+build+again"_zc);
  expectInvalidVersion("1.2.3 alpha"_zc);
  expectInvalidVersion("1.2.3-\xC3\xA9"_zc);
}

ZC_TEST("SortedFeatureSet orders encoded keys and rejects duplicates") {
  zc::Vector<FeatureName> input;
  input.add(requireFeature("aaa"_zc));
  input.add(requireFeature("zz"_zc));
  input.add(requireFeature("b"_zc));
  input.add(requireFeature("a"_zc));

  auto admitted = SortedFeatureSet::from(zc::mv(input));
  bool matched = false;
  ZC_IF_SOME(value, admitted) {
    const auto values = value.values();
    ZC_REQUIRE(values.size() == 4);
    ZC_EXPECT(values[0].text() == "a"_zc);
    ZC_EXPECT(values[1].text() == "b"_zc);
    ZC_EXPECT(values[2].text() == "zz"_zc);
    ZC_EXPECT(values[3].text() == "aaa"_zc);

    auto duplicate = value.clone();
    ZC_EXPECT(duplicate.values().size() == values.size());
    for (size_t index = 0; index < values.size(); ++index) {
      ZC_EXPECT(duplicate.values()[index] == values[index]);
    }
    matched = true;
  }
  ZC_EXPECT(matched);

  zc::Vector<FeatureName> duplicates;
  duplicates.add(requireFeature("simd"_zc));
  duplicates.add(requireFeature("simd"_zc));
  ZC_EXPECT(SortedFeatureSet::from(zc::mv(duplicates)) == zc::none);
}

ZC_TEST("SortedFeatureSet encodes its canonical sequence") {
  zc::Vector<FeatureName> input;
  input.add(requireFeature("bb"_zc));
  input.add(requireFeature("a"_zc));
  auto admitted = SortedFeatureSet::from(zc::mv(input));

  bool encoded = false;
  ZC_IF_SOME(value, admitted) {
    const uint8_t expected[] = {
        0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 1, 'a', 0, 0, 0, 0, 0, 0, 0, 2, 'b', 'b',
    };
    CanonicalEncoder encoder;
    value.encode(encoder);
    auto bytes = encoder.finish();
    ZC_EXPECT(bytes.asPtr() == zc::arrayPtr(expected));
    encoded = true;
  }
  ZC_EXPECT(encoded);
}

}  // namespace zomlang::compiler::identity
