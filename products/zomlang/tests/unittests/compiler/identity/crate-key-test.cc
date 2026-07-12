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

#include "zomlang/compiler/identity/crate-key.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::identity {
namespace {

template <typename Scalar>
Scalar requireScalar(zc::StringPtr text) {
  auto value = Scalar::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid canonical scalar test input");
}

ResolvedVersion requireVersion() {
  auto value = ResolvedVersion::fromCanonical("0.0.0"_zc);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid version test input");
}

SortedFeatureSet emptyPackageFeatures() {
  zc::Vector<FeatureName> features;
  auto value = SortedFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("empty package feature set was rejected");
}

SortedTargetFeatureSet emptyTargetFeatures() {
  zc::Vector<TargetFeatureName> features;
  auto value = SortedTargetFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("empty target feature set was rejected");
}

PackageKey localPackage(zc::StringPtr name) {
  zc::Vector<CanonicalPathSegment> segments;
  auto path = CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
  return PackageKey::from(CanonicalPackageSource::localPath(zc::mv(path)),
                          requireScalar<PackageName>(name), requireVersion(),
                          emptyPackageFeatures());
}

CanonicalTargetSpecificationKey targetSpec(uint32_t pointerWidth = 64,
                                           Endianness endianness = Endianness::Little) {
  auto value = CanonicalTargetSpecificationKey::from(
      requireScalar<TargetComponentName>("x"_zc), requireScalar<TargetComponentName>("v"_zc),
      requireScalar<TargetComponentName>("o"_zc), requireScalar<TargetComponentName>("e"_zc),
      requireScalar<TargetComponentName>("a"_zc), pointerWidth, endianness, emptyTargetFeatures());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid target specification test input");
}

Sha256Digest repeatedDigest(uint8_t byte) {
  uint8_t bytes[32];
  for (auto& value : bytes) { value = byte; }
  auto digest = Sha256Digest::fromBytes(zc::arrayPtr(bytes));
  ZC_IF_SOME(value, digest) { return value; }
  ZC_FAIL_REQUIRE("invalid digest test input");
}

CompilationConfigKey targetCompilation() {
  zc::Maybe<BuildScriptOutputKey> output = BuildScriptOutputKey::from(repeatedDigest(0x11));
  auto value = CompilationConfigKey::from(
      CompilationDomain::Target, targetSpec(),
      SemanticCompilerOptionsKey::from(2026, true, false, false), zc::mv(output));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid compilation configuration test input");
}

CrateKey crate(zc::StringPtr packageName) {
  auto value = CrateKey::from(localPackage(packageName), CrateTargetKind::Library,
                              requireScalar<TargetName>("lib"_zc), targetCompilation());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid crate test input");
}

PackageDependencyEdgeKey packageEdge() {
  auto value =
      PackageDependencyEdgeKey::from(localPackage("a"_zc), requireScalar<DependencyAlias>("dep"_zc),
                                     DependencyDomain::Target, localPackage("b"_zc));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid package edge test input");
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

ZC_TEST("CrateKey passes the fixed composite codec vector") {
  auto key = crate("a"_zc);
  auto encoded = key.encode();
  ZC_EXPECT(encoded.size() == 154);
  expectDigest(encoded.asPtr(),
               "136b0e54d7750bc21ab3e1b5f7cd1f6046fa8f5bafab919c391444a869a6c537"_zc);
}

ZC_TEST("CrateDependencyEdgeKey preserves the complete package edge") {
  auto key = CrateDependencyEdgeKey::from(packageEdge(), crate("a"_zc), crate("b"_zc));
  auto encoded = key.encode();
  ZC_EXPECT(encoded.size() == 406);
  expectDigest(encoded.asPtr(),
               "64fcca3d969d5d52c170d40a8a8db32005853856b61087719d003799c2c387a5"_zc);
}

ZC_TEST("Crate target keys reject unknown closed values and invalid pointer widths") {
  auto invalidWidth = CanonicalTargetSpecificationKey::from(
      requireScalar<TargetComponentName>("x"_zc), requireScalar<TargetComponentName>("v"_zc),
      requireScalar<TargetComponentName>("o"_zc), requireScalar<TargetComponentName>("e"_zc),
      requireScalar<TargetComponentName>("a"_zc), 7, Endianness::Little, emptyTargetFeatures());
  ZC_EXPECT(invalidWidth == zc::none);

  auto invalidEndian = CanonicalTargetSpecificationKey::from(
      requireScalar<TargetComponentName>("x"_zc), requireScalar<TargetComponentName>("v"_zc),
      requireScalar<TargetComponentName>("o"_zc), requireScalar<TargetComponentName>("e"_zc),
      requireScalar<TargetComponentName>("a"_zc), 64, static_cast<Endianness>(0xff),
      emptyTargetFeatures());
  ZC_EXPECT(invalidEndian == zc::none);

  zc::Maybe<BuildScriptOutputKey> noOutput;
  auto invalidDomain = CompilationConfigKey::from(
      static_cast<CompilationDomain>(0xff), targetSpec(),
      SemanticCompilerOptionsKey::from(2026, true, false, false), zc::mv(noOutput));
  ZC_EXPECT(invalidDomain == zc::none);

  auto invalidKind = CrateKey::from(localPackage("a"_zc), static_cast<CrateTargetKind>(0xff),
                                    requireScalar<TargetName>("lib"_zc), targetCompilation());
  ZC_EXPECT(invalidKind == zc::none);
}

}  // namespace zomlang::compiler::identity
