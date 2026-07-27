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

#include "zomlang/compiler/identity/semantic-identity-registry-set.h"

#include "zc/ztest/test.h"

namespace zomlang::compiler::identity {
namespace {

template <typename Scalar>
Scalar requireScalar(zc::StringPtr text) {
  auto value = Scalar::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid canonical scalar test input");
}

SemanticContextBrand requireContext(SemanticContextFactory& factory) {
  auto issued = factory.issue();
  ZC_IF_SOME(context, issued) { return context; }
  ZC_FAIL_REQUIRE("semantic context brand space exhausted during registry-set test");
}

SemanticIdentityRegistrySet registrySet(SemanticContextFactory& factory) {
  auto value = SemanticIdentityRegistrySet::create(factory, requireContext(factory));
  ZC_IF_SOME(registries, value) { return zc::mv(registries); }
  ZC_FAIL_REQUIRE("semantic identity registry set was rejected during registry-set test");
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

SortedTargetFeatureSet targetFeatures() {
  zc::Vector<TargetFeatureName> values;
  auto value = SortedTargetFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("empty target feature set was rejected");
}

PackageKey package(zc::StringPtr name) {
  zc::Vector<CanonicalPathSegment> segments;
  auto path = CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
  return PackageKey::from(CanonicalPackageSource::localPath(zc::mv(path)),
                          requireScalar<PackageName>(name), version(), features());
}

CompilationUnitIdentity userUnit(zc::StringPtr name) {
  return CompilationUnitIdentity::userPackage(package(name));
}

CompilationUnitIdentity coreUnit() {
  return CompilationUnitIdentity::toolchain(ToolchainUnitKey::core());
}

CanonicalTargetSpecificationKey target() {
  auto value = CanonicalTargetSpecificationKey::from(
      requireScalar<TargetComponentName>("x"_zc), requireScalar<TargetComponentName>("v"_zc),
      requireScalar<TargetComponentName>("o"_zc), requireScalar<TargetComponentName>("e"_zc),
      requireScalar<TargetComponentName>("a"_zc), 64, Endianness::Little, targetFeatures());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid target specification test input");
}

CompilationConfigKey compilation() {
  zc::Maybe<BuildScriptProducerKey> noBuildScript;
  auto value = CompilationConfigKey::from(
      CompilationDomain::Target, target(),
      SemanticCompilerOptionsKey::from(2026, true, false, false), zc::mv(noBuildScript));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid compilation configuration test input");
}

CrateKey coreCrate() {
  auto value = CrateKey::from(coreUnit(), CrateTargetKind::Library,
                              requireScalar<TargetName>("core"_zc), compilation());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid core crate test input");
}

}  // namespace

ZC_TEST("Semantic identity registry set orders mixed compilation units canonically") {
  SemanticContextFactory factory;
  auto registries = registrySet(factory);
  ZC_REQUIRE(registries.collectCompilationUnit(coreUnit()) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectCompilationUnit(userUnit("b"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectCompilationUnit(userUnit("a"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeCompilationUnits() == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.compilationUnits().size() == 3);

  ZC_IF_SOME(first, registries.compilationUnits().keyAt(0)) {
    ZC_EXPECT(first.kind() == CompilationUnitKind::UserPackage);
    ZC_EXPECT(first.userPackage().name() == "a"_zc);
  }
  ZC_IF_SOME(second, registries.compilationUnits().keyAt(1)) {
    ZC_EXPECT(second.kind() == CompilationUnitKind::UserPackage);
    ZC_EXPECT(second.userPackage().name() == "b"_zc);
  }
  ZC_IF_SOME(third, registries.compilationUnits().keyAt(2)) {
    ZC_EXPECT(third.kind() == CompilationUnitKind::Toolchain);
    ZC_EXPECT(third.toolchain().component() == ToolchainComponent::Core);
  }
}

ZC_TEST("Semantic identity registry set rejects a crate from the wrong unit branch") {
  SemanticContextFactory factory;
  auto registries = registrySet(factory);
  ZC_REQUIRE(registries.collectCompilationUnit(userUnit("a"_zc)) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeCompilationUnits() == FrozenRegistryFailure::None);

  ZC_EXPECT(registries.collectCrate(coreCrate(), 17) == FrozenRegistryFailure::AncestorMismatch);
  ZC_EXPECT(registries.crates().size() == 0);
  registries.sortIdentityInvariants();
  ZC_REQUIRE(registries.identityInvariants().size() == 1);
  const auto& invariant = registries.identityInvariants()[0];
  ZC_EXPECT(invariant.kind() == IdentityInvariantKind::AncestorMismatch);
  ZC_EXPECT(invariant.phase() == IdentityAllocationPhase::Crate);
  ZC_EXPECT(invariant.apiSite() == IdentityApiSite::RegistryMutation);
  ZC_EXPECT(invariant.inputTraversalOrdinal() == 17);
  ZC_EXPECT(invariant.structuralInputKey() != zc::none);
}

ZC_TEST("Semantic identity registry set admits the core unit and its crate") {
  SemanticContextFactory factory;
  auto registries = registrySet(factory);
  ZC_REQUIRE(registries.collectCompilationUnit(coreUnit()) == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeCompilationUnits() == FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectCrate(coreCrate()) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezeCrates() == FrozenRegistryFailure::None);
}

}  // namespace zomlang::compiler::identity
