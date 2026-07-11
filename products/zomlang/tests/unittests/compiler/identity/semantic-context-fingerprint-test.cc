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

#include "zomlang/compiler/identity/semantic-context-fingerprint.h"

#include "zc/core/encoding.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::identity {
namespace {

PackageName requirePackageName(zc::StringPtr text) {
  auto value = PackageName::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid package-name test input");
}

template <typename Scalar>
Scalar requireScalar(zc::StringPtr text) {
  auto value = Scalar::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid canonical scalar test input");
}

DependencyAlias requireDependencyAlias(zc::StringPtr text) {
  auto value = DependencyAlias::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid dependency-alias test input");
}

ResolvedVersion requireVersion() {
  auto value = ResolvedVersion::fromCanonical("0.0.0"_zc);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid semantic-version test input");
}

SortedFeatureSet emptyFeatures() {
  zc::Vector<FeatureName> features;
  auto value = SortedFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("empty feature set was rejected");
}

SortedTargetFeatureSet emptyTargetFeatures() {
  zc::Vector<TargetFeatureName> features;
  auto value = SortedTargetFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("empty target feature set was rejected");
}

Sha256Digest repeatedDigest(uint8_t byte) {
  uint8_t bytes[32];
  for (auto& value : bytes) { value = byte; }
  auto digest = Sha256Digest::fromBytes(zc::arrayPtr(bytes));
  ZC_IF_SOME(value, digest) { return value; }
  ZC_FAIL_REQUIRE("invalid digest test input");
}

PackageKey localPackage(zc::StringPtr name) {
  zc::Vector<CanonicalPathSegment> segments;
  auto path = CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
  return PackageKey::from(
      CanonicalPackageSource::localPath(zc::mv(path)), requirePackageName(name), requireVersion(),
      emptyFeatures());
}

PackageDependencyEdgeKey packageEdge() {
  auto value = PackageDependencyEdgeKey::from(
      localPackage("a"_zc), requireDependencyAlias("dep"_zc), DependencyDomain::Target,
      localPackage("b"_zc));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid package edge was rejected");
}

SemanticContextBrand requireContext(SemanticContextFactory& factory) {
  auto issued = factory.issue();
  ZC_IF_SOME(context, issued) { return context; }
  ZC_FAIL_REQUIRE("semantic context brand space exhausted during fingerprint test");
}

SemanticIdentityRegistrySet requireRegistrySet(SemanticContextFactory& factory,
                                               SemanticContextBrand context) {
  auto created = SemanticIdentityRegistrySet::create(factory, context);
  ZC_IF_SOME(registries, created) { return zc::mv(registries); }
  ZC_FAIL_REQUIRE("semantic context registry set was already claimed during fingerprint test");
}

CanonicalTargetSpecificationKey targetSpec() {
  auto value = CanonicalTargetSpecificationKey::from(
      requireScalar<TargetComponentName>("x"_zc),
      requireScalar<TargetComponentName>("v"_zc),
      requireScalar<TargetComponentName>("o"_zc),
      requireScalar<TargetComponentName>("e"_zc),
      requireScalar<TargetComponentName>("a"_zc), 64, Endianness::Little,
      emptyTargetFeatures());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid target specification test input");
}

CompilationConfigKey targetCompilation() {
  zc::Maybe<BuildScriptOutputKey> output = BuildScriptOutputKey::from(repeatedDigest(0x11));
  auto value = CompilationConfigKey::from(
      CompilationDomain::Target, targetSpec(),
      SemanticCompilerOptionsKey::from(2026, true, false, false), zc::mv(output));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid compilation configuration test input");
}

CrateKey crate() {
  auto value = CrateKey::from(localPackage("a"_zc), CrateTargetKind::Library,
                              requireScalar<TargetName>("lib"_zc), targetCompilation());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid crate test input");
}

SourceFileKey source() {
  zc::Vector<CanonicalPathSegment> segments;
  segments.add(requireScalar<CanonicalPathSegment>("g.zom"_zc));
  auto path = CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
  auto origin = SourceOriginKey::localFile(zc::mv(path));
  return SourceFileKey::from(crate(), zc::mv(origin));
}

ImmutableSourceSnapshot sourceSnapshot(uint8_t contentByte) {
  auto value = ImmutableSourceSnapshot::from(source(), zc::heapArray<uint8_t>(1, contentByte));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid immutable source snapshot was rejected");
}

zc::Maybe<SemanticContextFingerprint> fingerprint(
    zc::Vector<PackageKey>& packages, zc::Vector<PackageDependencyEdgeKey>& packageEdges) {
  zc::Vector<CrateKey> crates;
  zc::Vector<CrateDependencyEdgeKey> crateEdges;
  zc::Vector<SourceContentIdentity> sourceContents;
  zc::Vector<ModuleKey> modules;
  return SemanticContextFingerprint::compute(packages.asPtr(), packageEdges.asPtr(),
                                             crates.asPtr(), crateEdges.asPtr(),
                                             sourceContents.asPtr(), modules.asPtr());
}

zc::Maybe<SemanticContextFingerprint> fingerprintWithSources(
    zc::Vector<SourceContentIdentity>& sourceContents) {
  zc::Vector<PackageKey> packages;
  zc::Vector<PackageDependencyEdgeKey> packageEdges;
  zc::Vector<CrateKey> crates;
  zc::Vector<CrateDependencyEdgeKey> crateEdges;
  zc::Vector<ModuleKey> modules;
  return SemanticContextFingerprint::compute(packages.asPtr(), packageEdges.asPtr(),
                                             crates.asPtr(), crateEdges.asPtr(),
                                             sourceContents.asPtr(), modules.asPtr());
}

void expectFingerprint(zc::Maybe<SemanticContextFingerprint>& result, zc::StringPtr expected) {
  bool matched = false;
  ZC_IF_SOME(fingerprintValue, result) {
    ZC_EXPECT(zc::encodeHex(fingerprintValue.digest().bytes()) == expected);
    matched = true;
  }
  ZC_EXPECT(matched);
}

}  // namespace

ZC_TEST("Semantic context fingerprint passes the empty codec fixture") {
  zc::Vector<PackageKey> packages;
  zc::Vector<PackageDependencyEdgeKey> packageEdges;
  auto result = fingerprint(packages, packageEdges);
  expectFingerprint(result,
                    "aa36edfdf536f061cd028efd3cfe5003474aee9aa3ab39f294d3b42a95eaae5e"_zc);
}

ZC_TEST("Semantic context fingerprint passes the sorted package graph fixture") {
  zc::Vector<PackageKey> packages;
  packages.add(localPackage("b"_zc));
  packages.add(localPackage("a"_zc));
  zc::Vector<PackageDependencyEdgeKey> packageEdges;
  packageEdges.add(packageEdge());
  auto result = fingerprint(packages, packageEdges);
  expectFingerprint(result,
                    "20d2a8ab26a6a17066de900f472dab2e6222c949c6b01da507753822bc116eac"_zc);

  zc::Vector<PackageKey> permutedPackages;
  permutedPackages.add(localPackage("a"_zc));
  permutedPackages.add(localPackage("b"_zc));
  zc::Vector<PackageDependencyEdgeKey> permutedEdges;
  permutedEdges.add(packageEdge());
  auto permuted = fingerprint(permutedPackages, permutedEdges);
  expectFingerprint(permuted,
                    "20d2a8ab26a6a17066de900f472dab2e6222c949c6b01da507753822bc116eac"_zc);
}

ZC_TEST("Semantic context fingerprint rejects duplicate canonical inputs") {
  zc::Vector<PackageKey> packages;
  packages.add(localPackage("a"_zc));
  packages.add(localPackage("a"_zc));
  zc::Vector<PackageDependencyEdgeKey> packageEdges;
  ZC_EXPECT(fingerprint(packages, packageEdges) == zc::none);

  auto firstSnapshot = sourceSnapshot(0x33);
  auto secondSnapshot = sourceSnapshot(0x44);
  zc::Vector<SourceContentIdentity> sourceContents;
  sourceContents.add(SourceContentIdentity::from(firstSnapshot));
  sourceContents.add(SourceContentIdentity::from(secondSnapshot));
  ZC_EXPECT(fingerprintWithSources(sourceContents) == zc::none);
}

ZC_TEST("Semantic context fingerprint consumes only frozen context registries") {
  SemanticContextFactory factory;
  auto registries = requireRegistrySet(factory, requireContext(factory));
  zc::Vector<PackageDependencyEdgeKey> packageEdges;
  packageEdges.add(packageEdge());
  zc::Vector<CrateDependencyEdgeKey> crateEdges;
  ZC_EXPECT(SemanticContextFingerprint::compute(registries, packageEdges.asPtr(),
                                                crateEdges.asPtr()) == zc::none);

  ZC_EXPECT(registries.collectPackage(localPackage("b"_zc)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.collectPackage(localPackage("a"_zc)) == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezePackages() == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezeCrates() == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezeSourceFiles() == FrozenRegistryFailure::None);
  ZC_EXPECT(registries.freezeModules() == FrozenRegistryFailure::None);
  auto result =
      SemanticContextFingerprint::compute(registries, packageEdges.asPtr(), crateEdges.asPtr());
  expectFingerprint(result,
                    "20d2a8ab26a6a17066de900f472dab2e6222c949c6b01da507753822bc116eac"_zc);
}

}  // namespace zomlang::compiler::identity
