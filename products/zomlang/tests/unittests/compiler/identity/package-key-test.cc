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

#include "zomlang/compiler/identity/package-key.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::identity {
namespace {

PackageName requirePackageName(zc::StringPtr text) {
  auto value = PackageName::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid package-name test input");
}

DependencyAlias requireDependencyAlias(zc::StringPtr text) {
  auto value = DependencyAlias::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid dependency-alias test input");
}

ResolvedVersion requireVersion(zc::StringPtr text) {
  auto value = ResolvedVersion::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid version test input");
}

SortedFeatureSet emptyFeatures() {
  zc::Vector<FeatureName> features;
  auto value = SortedFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("empty feature set was rejected");
}

PackageKey localPackage(zc::StringPtr name) {
  zc::Vector<CanonicalPathSegment> segments;
  auto path = CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
  auto source = CanonicalPackageSource::localPath(zc::mv(path));
  return PackageKey::from(zc::mv(source), requirePackageName(name), requireVersion("0.0.0"_zc),
                          emptyFeatures());
}

void expectDigest(zc::ArrayPtr<const uint8_t> bytes, zc::StringPtr expected) {
  auto digest = sha256(bytes);
  bool matched = false;
  ZC_IF_SOME(value, digest) {
    ZC_EXPECT(zc::encodeHex(value.bytes()) == expected);
    matched = true;
  }
  ZC_EXPECT(matched);
}

}  // namespace

ZC_TEST("PackageKey passes the fixed local package codec vector") {
  const uint8_t expected[] = {
      0x03,
      0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 1, 'a',
      0, 0, 0, 0, 0, 0, 0, 5, '0', '.', '0', '.', '0',
      0, 0, 0, 0, 0, 0, 0, 0,
  };
  auto package = localPackage("a"_zc);
  auto encoded = package.encode();
  ZC_EXPECT(encoded.size() == 43);
  ZC_EXPECT(encoded.asPtr() == zc::arrayPtr(expected));
  expectDigest(encoded.asPtr(),
               "b0c7b4f55c7faf6d4522b3a6f81e979347436c782d29ad2eeaa09985479d40a6"_zc);
}

ZC_TEST("PackageDependencyEdgeKey passes the fixed target edge codec vector") {
  auto admitted = PackageDependencyEdgeKey::from(
      localPackage("a"_zc), requireDependencyAlias("dep"_zc), DependencyDomain::Target,
      localPackage("b"_zc));
  bool matched = false;
  ZC_IF_SOME(edge, admitted) {
    auto encoded = edge.encode();
    ZC_EXPECT(encoded.size() == 98);
    expectDigest(
        encoded.asPtr(),
        "b4a6fdda29af9e3c0b0d6a21b062aa94be3315bc47bde3f432d46e85766b2751"_zc);
    matched = true;
  }
  ZC_EXPECT(matched);

  ZC_EXPECT(PackageDependencyEdgeKey::from(
                localPackage("a"_zc), requireDependencyAlias("dep"_zc),
                static_cast<DependencyDomain>(0xff), localPackage("b"_zc)) == zc::none);
}

ZC_TEST("Canonical package paths preserve strong normalized segments") {
  auto first = CanonicalPathSegment::fromSource("caf\x65\xCC\x81"_zc);
  bool admitted = false;
  ZC_IF_SOME(segment, first) {
    zc::Vector<CanonicalPathSegment> segments;
    segments.add(zc::mv(segment));
    auto path = CanonicalWorkspaceRelativePath::from(2, zc::mv(segments));
    auto duplicate = path.clone();
    ZC_EXPECT(duplicate.leadingParents() == 2);
    ZC_REQUIRE(duplicate.segments().size() == 1);
    ZC_EXPECT(duplicate.segments()[0].text() == "caf\xC3\xA9"_zc);
    admitted = true;
  }
  ZC_EXPECT(admitted);
}

ZC_TEST("VcsRevision enforces closed digest widths") {
  uint8_t sha1Bytes[20] = {};
  uint8_t sha256Bytes[32] = {};
  ZC_EXPECT(VcsRevision::from(VcsRevisionAlgorithm::Sha1, zc::arrayPtr(sha1Bytes)) != zc::none);
  ZC_EXPECT(VcsRevision::from(VcsRevisionAlgorithm::Sha256, zc::arrayPtr(sha256Bytes)) !=
            zc::none);
  ZC_EXPECT(VcsRevision::from(VcsRevisionAlgorithm::Sha1, zc::arrayPtr(sha256Bytes)) == zc::none);
  ZC_EXPECT(VcsRevision::from(VcsRevisionAlgorithm::Sha256, zc::arrayPtr(sha1Bytes)) == zc::none);
  ZC_EXPECT(VcsRevision::from(static_cast<VcsRevisionAlgorithm>(0xff),
                              zc::arrayPtr(sha256Bytes)) == zc::none);
}

}  // namespace zomlang::compiler::identity
