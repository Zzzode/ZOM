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

#include "zomlang/compiler/driver/package/dependency-manifest.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/crypto/sha256.h"

namespace zomlang::compiler::driver::package {
namespace {

identity::DependencyAlias alias(zc::StringPtr text) {
  auto value = identity::DependencyAlias::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid dependency alias fixture");
}

identity::PackageName packageName(zc::StringPtr text) {
  auto value = identity::PackageName::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid package name fixture");
}

identity::SortedFeatureSet emptyFeatures() {
  zc::Vector<identity::FeatureName> values;
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(admitted, result) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("empty feature set was rejected");
}

identity::CanonicalWorkspaceRelativePath localPath() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  auto segment = identity::CanonicalPathSegment::fromCanonical("math"_zc);
  ZC_IF_SOME(admitted, segment) { segments.add(zc::mv(admitted)); }
  ZC_REQUIRE(segments.size() == 1);
  return identity::CanonicalWorkspaceRelativePath::from(1, zc::mv(segments));
}

DependencyRequirementWithoutOrigin localRequirement(identity::DependencyDomain domain,
                                                    bool optional = false) {
  auto value = DependencyRequirementWithoutOrigin::from(
      alias("a"_zc), packageName("b"_zc), domain, PackageSourceConstraint::localPath(localPath()),
      zc::none, emptyFeatures(), true, optional);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid local dependency fixture was rejected");
}

}  // namespace

ZC_TEST("DependencyManifest.EnforcesClosedSelectorAndDomainRules") {
  ZC_EXPECT(VcsSelector::tag(""_zc) == zc::none);
  ZC_EXPECT(VcsSelector::branch("bad\nbranch"_zc) == zc::none);
  ZC_EXPECT(DependencyRequirementWithoutOrigin::from(
                alias("a"_zc), packageName("b"_zc), static_cast<identity::DependencyDomain>(0xff),
                PackageSourceConstraint::localPath(localPath()), zc::none, emptyFeatures(), true,
                false) == zc::none);
  ZC_EXPECT(DependencyRequirementWithoutOrigin::from(
                alias("a"_zc), packageName("b"_zc), identity::DependencyDomain::Build,
                PackageSourceConstraint::localPath(localPath()), zc::none, emptyFeatures(), true,
                true) == zc::none);
}

ZC_TEST("DependencyManifest.PassesFixedOriginFreeCodecVector") {
  const uint8_t expected[] = {
      0, 0, 0,   0,   0,   0,   0, 1, 'a', 0, 0, 0, 0, 0, 0, 0, 1, 'b', 1,
      3, 0, 0,   0,   1,   0,   0, 0, 0,   0, 0, 0, 1, 0, 0, 0, 0, 0,   0,
      0, 4, 'm', 'a', 't', 'h', 0, 0, 0,   0, 0, 0, 0, 0, 0, 1, 0,
  };
  auto value = localRequirement(identity::DependencyDomain::Target);
  auto encoded = value.encode();
  ZC_EXPECT(encoded.size() == 55);
  ZC_EXPECT(encoded.asPtr() == zc::arrayPtr(expected));
  auto digest = identity::sha256(encoded.asPtr());
  ZC_REQUIRE(digest != zc::none);
  ZC_IF_SOME(admitted, digest) {
    ZC_EXPECT(zc::encodeHex(admitted.bytes()) ==
              "5ecf54b24b2d56666b5d6f985351ee18e28371413da169935b4ca4f5968c1a07"_zc);
  }
}

}  // namespace zomlang::compiler::driver::package
