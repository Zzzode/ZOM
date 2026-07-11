// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/identity/identity-dump.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::identity {
namespace {

SemanticContextBrand requireContext(SemanticContextFactory& factory) {
  auto issued = factory.issue();
  ZC_IF_SOME(context, issued) { return context; }
  ZC_FAIL_REQUIRE("semantic context brand space exhausted during dump test");
}

SemanticIdentityRegistrySet registrySet(SemanticContextFactory& factory) {
  auto value = SemanticIdentityRegistrySet::create(factory, requireContext(factory));
  ZC_IF_SOME(registries, value) { return zc::mv(registries); }
  ZC_FAIL_REQUIRE("semantic identity registry set was rejected during dump test");
}

PackageName packageName(zc::StringPtr text) {
  auto value = PackageName::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid package name test input");
}

ResolvedVersion version() {
  auto value = ResolvedVersion::fromCanonical("0.0.0"_zc);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid semantic version test input");
}

SortedFeatureSet features() {
  zc::Vector<FeatureName> values;
  auto value = SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("empty feature set was rejected");
}

PackageKey package(zc::StringPtr name) {
  zc::Vector<CanonicalPathSegment> segments;
  auto path = CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
  return PackageKey::from(CanonicalPackageSource::localPath(zc::mv(path)), packageName(name),
                          version(), features());
}

void freezeRemainingEmptyRegistries(SemanticIdentityRegistrySet& registries) {
  ZC_EXPECT(registries.freezeCrates() == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezeSourceFiles() == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezeModules() == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezeDefinitions() == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezeImpls() == FrozenRegistryFailure::None);
}

}  // namespace

ZC_TEST("Identity dump preserves exact empty sections and final LF") {
  SemanticContextFactory factory;
  auto registries = registrySet(factory);
  ZC_EXPECT(dumpIdentityRegistries(registries) == zc::none);
  ZC_EXPECT(registries.freezePackages() == FrozenRegistryFailure::None);
  freezeRemainingEmptyRegistries(registries);
  auto dump = dumpIdentityRegistries(registries);
  ZC_IF_SOME(text, dump) {
    ZC_EXPECT(text == "zom.identity.v0\n[packages]\n[crates]\n[sources]\n[modules]\n"
                      "[definitions]\n[impls]\n"_zc);
  }
}

ZC_TEST("Identity dump uses canonical key order without slots or brands") {
  SemanticContextFactory factory;
  auto registries = registrySet(factory);
  auto first = package("a"_zc).encode();
  auto second = package("b"_zc).encode();
  ZC_EXPECT(registries.collectPackage(package("b"_zc)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.collectPackage(package("a"_zc)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezePackages() == FrozenRegistryFailure::None);
  freezeRemainingEmptyRegistries(registries);

  auto expected = zc::str("zom.identity.v0\n[packages]\npackage ", zc::encodeHex(first.asPtr()),
                          "\npackage ", zc::encodeHex(second.asPtr()),
                          "\n[crates]\n[sources]\n[modules]\n[definitions]\n[impls]\n");
  auto dump = dumpIdentityRegistries(registries);
  ZC_IF_SOME(text, dump) { ZC_EXPECT(text == expected); }
}

}  // namespace zomlang::compiler::identity
