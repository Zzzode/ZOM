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

#include "zomlang/compiler/identity/semantic/context-fingerprint.h"

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
  return PackageKey::from(CanonicalPackageSource::localPath(zc::mv(path)), requirePackageName(name),
                          requireVersion(), emptyFeatures());
}

CompilationUnitIdentity userCompilationUnit(zc::StringPtr name) {
  return CompilationUnitIdentity::userPackage(localPackage(name));
}

CompilationUnitIdentity coreCompilationUnit() {
  return CompilationUnitIdentity::toolchain(ToolchainUnitKey::core());
}

ToolchainSemanticContextInput coreContextInput(uint8_t distributionByte = 0x21,
                                               uint8_t policyByte = 0x31) {
  return ToolchainSemanticContextInput::from(
      ToolchainUnitKey::core(), repeatedDigest(distributionByte), repeatedDigest(policyByte));
}

PackageDependencyEdgeKey packageEdge() {
  auto value =
      PackageDependencyEdgeKey::from(localPackage("a"_zc), requireDependencyAlias("dep"_zc),
                                     DependencyDomain::Target, localPackage("b"_zc));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid package edge was rejected");
}

CanonicalTargetSpecificationKey targetSpec() {
  auto value = CanonicalTargetSpecificationKey::from(
      requireScalar<TargetComponentName>("x"_zc), requireScalar<TargetComponentName>("v"_zc),
      requireScalar<TargetComponentName>("o"_zc), requireScalar<TargetComponentName>("e"_zc),
      requireScalar<TargetComponentName>("a"_zc), 64, Endianness::Little, emptyTargetFeatures());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid target specification test input");
}

CompilationConfigKey targetCompilation() {
  zc::Maybe<BuildScriptProducerKey> output = BuildScriptProducerKey::from(repeatedDigest(0x11));
  auto value = CompilationConfigKey::from(
      CompilationDomain::Target, targetSpec(),
      SemanticCompilerOptionsKey::from(2026, true, false, false), zc::mv(output));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid compilation configuration test input");
}

CrateKey crate() {
  auto value = CrateKey::from(userCompilationUnit("a"_zc), CrateTargetKind::Library,
                              requireScalar<TargetName>("lib"_zc), targetCompilation());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid crate test input");
}

CrateKey coreCrate() {
  auto projected = projectToolchainCoreCrate(crate());
  ZC_IF_SOME(value, projected) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("valid core projection was rejected");
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

zc::Maybe<ContextFingerprint> fingerprint(
    zc::Vector<CompilationUnitIdentity>& compilationUnits,
    zc::Vector<ToolchainSemanticContextInput>& toolchainInputs,
    zc::Vector<PackageDependencyEdgeKey>& packageEdges) {
  zc::Vector<CrateKey> crates;
  zc::Vector<CrateDependencyEdgeKey> crateEdges;
  zc::Vector<SourceContentIdentity> sourceContents;
  zc::Vector<ModuleKey> modules;
  return ContextFingerprint::compute(
      compilationUnits.asPtr(), toolchainInputs.asPtr(), packageEdges.asPtr(), crates.asPtr(),
      crateEdges.asPtr(), sourceContents.asPtr(), modules.asPtr());
}

zc::Maybe<ContextFingerprint> fingerprintWithSources(
    zc::Vector<SourceContentIdentity>& sourceContents) {
  zc::Vector<CompilationUnitIdentity> compilationUnits;
  zc::Vector<ToolchainSemanticContextInput> toolchainInputs;
  zc::Vector<PackageDependencyEdgeKey> packageEdges;
  zc::Vector<CrateKey> crates;
  zc::Vector<CrateDependencyEdgeKey> crateEdges;
  zc::Vector<ModuleKey> modules;
  return ContextFingerprint::compute(
      compilationUnits.asPtr(), toolchainInputs.asPtr(), packageEdges.asPtr(), crates.asPtr(),
      crateEdges.asPtr(), sourceContents.asPtr(), modules.asPtr());
}

void expectFingerprint(zc::Maybe<ContextFingerprint>& result, zc::StringPtr expected) {
  bool matched = false;
  ZC_IF_SOME(fingerprintValue, result) {
    auto actual = zc::encodeHex(fingerprintValue.digest().bytes());
    ZC_EXPECT(actual == expected, actual, expected);
    matched = true;
  }
  ZC_EXPECT(matched);
}

bool sameFingerprint(const zc::Maybe<ContextFingerprint>& left,
                     const zc::Maybe<ContextFingerprint>& right) {
  ZC_IF_SOME(leftValue, left) {
    ZC_IF_SOME(rightValue, right) {
      return leftValue.digest().bytes() == rightValue.digest().bytes();
    }
  }
  return false;
}

}  // namespace

ZC_TEST("Semantic context fingerprint passes the empty codec fixture") {
  zc::Vector<CompilationUnitIdentity> compilationUnits;
  zc::Vector<ToolchainSemanticContextInput> toolchainInputs;
  zc::Vector<PackageDependencyEdgeKey> packageEdges;
  auto result = fingerprint(compilationUnits, toolchainInputs, packageEdges);
  expectFingerprint(result, "9edf7ccadf4fb6b61b8a8e87c665571bcaed3d4cc4fb1a0554b0cdabbc8dc61b"_zc);
}

ZC_TEST("Semantic context fingerprint canonicalizes user compilation-unit order") {
  zc::Vector<CompilationUnitIdentity> compilationUnits;
  compilationUnits.add(userCompilationUnit("b"_zc));
  compilationUnits.add(userCompilationUnit("a"_zc));
  zc::Vector<ToolchainSemanticContextInput> toolchainInputs;
  zc::Vector<PackageDependencyEdgeKey> packageEdges;
  packageEdges.add(packageEdge());
  auto result = fingerprint(compilationUnits, toolchainInputs, packageEdges);
  expectFingerprint(result, "9bd03b6d1c0ac441a09e942e67cbbc526d36555f1c6c24e72e9fe9dc37e325ae"_zc);

  zc::Vector<CompilationUnitIdentity> permutedCompilationUnits;
  permutedCompilationUnits.add(userCompilationUnit("a"_zc));
  permutedCompilationUnits.add(userCompilationUnit("b"_zc));
  zc::Vector<ToolchainSemanticContextInput> permutedToolchainInputs;
  zc::Vector<PackageDependencyEdgeKey> permutedEdges;
  permutedEdges.add(packageEdge());
  auto permuted = fingerprint(permutedCompilationUnits, permutedToolchainInputs, permutedEdges);
  ZC_EXPECT(sameFingerprint(result, permuted));
}

ZC_TEST("Semantic context fingerprint canonicalizes mixed user and core order") {
  zc::Vector<CompilationUnitIdentity> compilationUnits;
  compilationUnits.add(coreCompilationUnit());
  compilationUnits.add(userCompilationUnit("a"_zc));
  zc::Vector<ToolchainSemanticContextInput> toolchainInputs;
  toolchainInputs.add(coreContextInput());
  zc::Vector<PackageDependencyEdgeKey> packageEdges;
  auto result = fingerprint(compilationUnits, toolchainInputs, packageEdges);
  expectFingerprint(result, "0ca67b056331e421600aeda686029b084cd7e1c0e1475f1b87193fdc141f07e3"_zc);

  zc::Vector<CompilationUnitIdentity> permutedCompilationUnits;
  permutedCompilationUnits.add(userCompilationUnit("a"_zc));
  permutedCompilationUnits.add(coreCompilationUnit());
  zc::Vector<ToolchainSemanticContextInput> permutedToolchainInputs;
  permutedToolchainInputs.add(coreContextInput());
  zc::Vector<PackageDependencyEdgeKey> permutedEdges;
  auto permuted = fingerprint(permutedCompilationUnits, permutedToolchainInputs, permutedEdges);
  ZC_EXPECT(sameFingerprint(result, permuted));
}

ZC_TEST("Semantic context fingerprint passes the core-only codec fixture") {
  zc::Vector<CompilationUnitIdentity> compilationUnits;
  compilationUnits.add(coreCompilationUnit());
  zc::Vector<ToolchainSemanticContextInput> toolchainInputs;
  toolchainInputs.add(coreContextInput());
  zc::Vector<PackageDependencyEdgeKey> packageEdges;
  auto result = fingerprint(compilationUnits, toolchainInputs, packageEdges);
  expectFingerprint(result, "f3f4e83d154a5b4b31f9f71a8ba377e899ce4fc21350935505ed3d48f8dcc74a"_zc);
}

ZC_TEST("Core semantic context fingerprint binds only the exact projected core crate") {
  auto core = coreCrate();
  auto result = CoreSemanticContextFingerprint::compute(core);
  ZC_REQUIRE(result != zc::none);

  zc::Vector<uint8_t> preimage;
  preimage.addAll("zom.core-semantic-context"_zc.asBytes());
  preimage.add(0);
  preimage.addAll(core.encode().asPtr());
  auto independent = sha256(preimage.asPtr());
  ZC_REQUIRE(independent != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(result).digest() == ZC_REQUIRE_NONNULL(independent));
  ZC_EXPECT(CoreSemanticContextFingerprint::compute(crate()) == zc::none);
}

ZC_TEST("Semantic context fingerprint rejects duplicate canonical inputs") {
  zc::Vector<CompilationUnitIdentity> compilationUnits;
  compilationUnits.add(coreCompilationUnit());
  compilationUnits.add(coreCompilationUnit());
  zc::Vector<ToolchainSemanticContextInput> toolchainInputs;
  toolchainInputs.add(coreContextInput());
  zc::Vector<PackageDependencyEdgeKey> packageEdges;
  ZC_EXPECT(fingerprint(compilationUnits, toolchainInputs, packageEdges) == zc::none);

  auto firstSnapshot = sourceSnapshot(0x33);
  auto secondSnapshot = sourceSnapshot(0x44);
  zc::Vector<SourceContentIdentity> sourceContents;
  sourceContents.add(SourceContentIdentity::from(firstSnapshot));
  sourceContents.add(SourceContentIdentity::from(secondSnapshot));
  ZC_EXPECT(fingerprintWithSources(sourceContents) == zc::none);
}

ZC_TEST("Semantic context fingerprint requires exactly one input per toolchain unit") {
  zc::Vector<CompilationUnitIdentity> coreUnits;
  coreUnits.add(coreCompilationUnit());
  zc::Vector<PackageDependencyEdgeKey> packageEdges;

  zc::Vector<ToolchainSemanticContextInput> missingInputs;
  ZC_EXPECT(fingerprint(coreUnits, missingInputs, packageEdges) == zc::none);

  zc::Vector<ToolchainSemanticContextInput> duplicateInputs;
  duplicateInputs.add(coreContextInput());
  duplicateInputs.add(coreContextInput());
  ZC_EXPECT(fingerprint(coreUnits, duplicateInputs, packageEdges) == zc::none);

  zc::Vector<CompilationUnitIdentity> userUnits;
  userUnits.add(userCompilationUnit("a"_zc));
  zc::Vector<ToolchainSemanticContextInput> extraInputs;
  extraInputs.add(coreContextInput());
  ZC_EXPECT(fingerprint(userUnits, extraInputs, packageEdges) == zc::none);
}

ZC_TEST("Semantic context fingerprint changes with core distribution and policy lineage") {
  zc::Vector<CompilationUnitIdentity> baselineUnits;
  baselineUnits.add(coreCompilationUnit());
  zc::Vector<ToolchainSemanticContextInput> baselineInputs;
  baselineInputs.add(coreContextInput());
  zc::Vector<PackageDependencyEdgeKey> baselineEdges;
  auto baseline = fingerprint(baselineUnits, baselineInputs, baselineEdges);

  zc::Vector<CompilationUnitIdentity> distributionUnits;
  distributionUnits.add(coreCompilationUnit());
  zc::Vector<ToolchainSemanticContextInput> distributionInputs;
  distributionInputs.add(coreContextInput(0x22, 0x31));
  zc::Vector<PackageDependencyEdgeKey> distributionEdges;
  auto distributionMutation = fingerprint(distributionUnits, distributionInputs, distributionEdges);
  ZC_EXPECT(!sameFingerprint(baseline, distributionMutation));

  zc::Vector<CompilationUnitIdentity> policyUnits;
  policyUnits.add(coreCompilationUnit());
  zc::Vector<ToolchainSemanticContextInput> policyInputs;
  policyInputs.add(coreContextInput(0x21, 0x32));
  zc::Vector<PackageDependencyEdgeKey> policyEdges;
  auto policyMutation = fingerprint(policyUnits, policyInputs, policyEdges);
  ZC_EXPECT(!sameFingerprint(baseline, policyMutation));

  auto stableToolchain = ToolchainUnitKey::core().encode();
  auto mutatedDistributionToolchain = coreContextInput(0x22, 0x31).toolchain().encode();
  auto mutatedPolicyToolchain = coreContextInput(0x21, 0x32).toolchain().encode();
  ZC_EXPECT(stableToolchain.asPtr() == mutatedDistributionToolchain.asPtr());
  ZC_EXPECT(stableToolchain.asPtr() == mutatedPolicyToolchain.asPtr());
}

}  // namespace zomlang::compiler::identity
