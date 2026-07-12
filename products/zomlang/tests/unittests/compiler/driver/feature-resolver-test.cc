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

#include "zomlang/compiler/driver/package/feature-resolver.h"

#include "zc/ztest/test.h"

namespace zomlang::compiler::driver::package {
namespace {

identity::CanonicalWorkspaceRelativePath manifestPath() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  auto segment = identity::CanonicalPathSegment::fromCanonical("Zom.toml"_zc);
  ZC_IF_SOME(value, segment) { segments.add(zc::mv(value)); }
  return identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
}

NormalizedManifest manifest() {
  zc::Vector<identity::CanonicalRelativePath> files;
  auto inventory = PackageSourceInventory::from(zc::mv(files));
  ZC_IF_SOME(inventoryValue, inventory) {
    ManifestParser parser;
    auto result = parser.parseWorkspaceManifest(manifestPath(), R"toml([package]
name = "app"
version = "1.0.0"
edition = "2026"

[dependencies]
math = { path = "../math", optional = true }

[features]
default = ["fast", "dep:math"]
fast = ["simd", "math/fast"]
simd = []
)toml"_zc,
                                                inventoryValue);
    if (result.is<NormalizedManifest>()) { return zc::mv(result.get<NormalizedManifest>()); }
  }
  ZC_FAIL_REQUIRE("valid feature resolver manifest fixture was rejected");
}

identity::FeatureName feature(zc::StringPtr name) {
  auto value = identity::FeatureName::fromCanonical(name);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid feature fixture");
}

}  // namespace

ZC_TEST("FeatureResolverTest.ExpandsDefaultLocalAndDependencyEdges") {
  auto input = manifest();
  zc::Vector<identity::FeatureName> requested;
  auto result = FeatureResolver::expand(input, FeatureActivationDomain::Target, requested, true);
  ZC_REQUIRE(result.is<ExpandedFeatureActivation>());
  const auto& activation = result.get<ExpandedFeatureActivation>();
  ZC_REQUIRE(activation.activeFeatures().size() == 3);
  ZC_EXPECT(activation.activeFeatures()[0].text() == "fast"_zc);
  ZC_EXPECT(activation.activeFeatures()[1].text() == "simd"_zc);
  ZC_EXPECT(activation.activeFeatures()[2].text() == "default"_zc);
  ZC_REQUIRE(activation.activatedDependencies().size() == 1);
  ZC_EXPECT(activation.activatedDependencies()[0].alias() == "math"_zc);
  ZC_REQUIRE(activation.activatedDependencies()[0].requestedFeatures().size() == 1);
  ZC_EXPECT(activation.activatedDependencies()[0].requestedFeatures()[0].text() == "fast"_zc);
}

ZC_TEST("FeatureResolverTest.IsPermutationInvariantAndSeparatesDomains") {
  auto input = manifest();
  zc::Vector<identity::FeatureName> first;
  first.add(feature("simd"_zc));
  first.add(feature("fast"_zc));
  zc::Vector<identity::FeatureName> second;
  second.add(feature("fast"_zc));
  second.add(feature("simd"_zc));
  auto left = FeatureResolver::expand(input, FeatureActivationDomain::Target, first, false);
  auto right = FeatureResolver::expand(input, FeatureActivationDomain::Target, second, false);
  ZC_REQUIRE(left.is<ExpandedFeatureActivation>());
  ZC_REQUIRE(right.is<ExpandedFeatureActivation>());
  ZC_EXPECT(left.get<ExpandedFeatureActivation>().encode().asPtr() ==
            right.get<ExpandedFeatureActivation>().encode().asPtr());

  auto build = FeatureResolver::expand(input, FeatureActivationDomain::Build, second, false);
  ZC_REQUIRE(build.is<ExpandedFeatureActivation>());
  ZC_EXPECT(build.get<ExpandedFeatureActivation>().encode().asPtr() !=
            right.get<ExpandedFeatureActivation>().encode().asPtr());
}

ZC_TEST("FeatureResolverTest.RejectsMissingRequestedFeature") {
  auto input = manifest();
  zc::Vector<identity::FeatureName> requested;
  requested.add(feature("missing"_zc));
  auto result = FeatureResolver::expand(input, FeatureActivationDomain::Target, requested, false);
  ZC_REQUIRE(result.is<FeatureIssue>());
  ZC_EXPECT(result.get<FeatureIssue>() == FeatureIssue::RequestedFeatureMissing);
}

}  // namespace zomlang::compiler::driver::package
