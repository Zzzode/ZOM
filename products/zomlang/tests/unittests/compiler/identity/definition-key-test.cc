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

#include "zomlang/compiler/identity/definition-key.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/source-snapshot.h"

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

Sha256Digest repeatedDigest(uint8_t byte) {
  uint8_t bytes[32];
  for (auto& value : bytes) { value = byte; }
  auto digest = Sha256Digest::fromBytes(zc::arrayPtr(bytes));
  ZC_IF_SOME(value, digest) { return value; }
  ZC_FAIL_REQUIRE("invalid digest test input");
}

PackageKey package() {
  zc::Vector<CanonicalPathSegment> segments;
  auto path = CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
  return PackageKey::from(CanonicalPackageSource::localPath(zc::mv(path)),
                          requireScalar<PackageName>("a"_zc), requireVersion(),
                          emptyPackageFeatures());
}

CanonicalTargetSpecificationKey targetSpec() {
  auto value = CanonicalTargetSpecificationKey::from(
      requireScalar<TargetComponentName>("x"_zc), requireScalar<TargetComponentName>("v"_zc),
      requireScalar<TargetComponentName>("o"_zc), requireScalar<TargetComponentName>("e"_zc),
      requireScalar<TargetComponentName>("a"_zc), 64, Endianness::Little, emptyTargetFeatures());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid target specification test input");
}

CompilationConfigKey compilation() {
  zc::Maybe<BuildScriptOutputKey> output = BuildScriptOutputKey::from(repeatedDigest(0x11));
  auto value = CompilationConfigKey::from(
      CompilationDomain::Target, targetSpec(),
      SemanticCompilerOptionsKey::from(2026, true, false, false), zc::mv(output));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid compilation configuration test input");
}

CrateKey crate() {
  auto value = CrateKey::from(package(), CrateTargetKind::Library,
                              requireScalar<TargetName>("lib"_zc), compilation());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid crate test input");
}

CanonicalRelativePath logicalPath() {
  zc::Vector<CanonicalPathSegment> segments;
  segments.add(requireScalar<CanonicalPathSegment>("g.zom"_zc));
  return CanonicalRelativePath::from(zc::mv(segments));
}

SourceFileKey source(uint8_t contentByte = 0x22) {
  auto origin = SourceOriginKey::generatedFile(BuildScriptOutputKey::from(repeatedDigest(0x11)),
                                               logicalPath(), repeatedDigest(contentByte));
  return SourceFileKey::from(crate(), zc::mv(origin));
}

ModuleKey module(uint8_t contentByte = 0x22) {
  zc::Vector<ModulePathSegment> path;
  path.add(requireScalar<ModulePathSegment>("m"_zc));
  zc::Maybe<SourceSpan> noAnchor;
  auto value = ModuleKey::from(crate(), zc::mv(path), source(contentByte), zc::mv(noAnchor));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid module test input");
}

SourceSpan span(uint8_t contentByte = 0x22) {
  auto snapshot =
      ImmutableSourceSnapshot::from(source(contentByte), zc::heapArray<uint8_t>(1, uint8_t{0}));
  ZC_IF_SOME(admittedSnapshot, snapshot) {
    auto value = admittedSnapshot.span(0, 1);
    ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  }
  ZC_FAIL_REQUIRE("invalid source span test input");
}

DefinitionNameKey declaredName() {
  return DefinitionNameKey::declared(requireScalar<DeclaredDefinitionName>("f"_zc));
}

DefinitionPathSegment functionSegment(uint8_t contentByte = 0x22) {
  auto value =
      DefinitionPathSegment::from(DefinitionKind::Function, declaredName(), span(contentByte), 0);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid definition segment test input");
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

ZC_TEST("DefinitionKey passes the fixed structural definition codec vector") {
  zc::Vector<DefinitionPathComponent> path;
  path.add(DefinitionPathComponent::definition(functionSegment()));
  auto value = DefinitionKey::from(module(), zc::mv(path));
  bool matched = false;
  ZC_IF_SOME(key, value) {
    auto encoded = key.encode();
    ZC_EXPECT(encoded.size() == 692);
    expectDigest(encoded.asPtr(),
                 "3f9ea55ca0ce091341b59f3cd44b64962e9cf26f4c4e9c19815011a702432ca4"_zc);
    matched = true;
  }
  ZC_EXPECT(matched);
}

ZC_TEST("ImplKey passes the fixed structural implementation codec vector") {
  zc::Vector<DefinitionPathSegment> parentPath;
  auto value = ImplKey::from(module(), zc::mv(parentPath), span(), 0);
  bool matched = false;
  ZC_IF_SOME(key, value) {
    auto encoded = key.encode();
    ZC_EXPECT(encoded.size() == 680);
    expectDigest(encoded.asPtr(),
                 "e71d00f88b11b9ee6bd0a5f2196f9c7506fbe28f341733df1e788cc192d23882"_zc);
    matched = true;
  }
  ZC_EXPECT(matched);
}

ZC_TEST("Definition keys reject malformed kinds paths and source ancestry") {
  ZC_EXPECT(DefinitionPathSegment::from(static_cast<DefinitionKind>(0xff), declaredName(), span(),
                                        0) == zc::none);
  ZC_EXPECT(DefinitionNameKey::anonymous(static_cast<AnonymousDefinitionRole>(0xff)) == zc::none);

  zc::Vector<DefinitionPathComponent> empty;
  ZC_EXPECT(DefinitionKey::from(module(), zc::mv(empty)) == zc::none);

  zc::Vector<DefinitionPathComponent> endsInImpl;
  endsInImpl.add(DefinitionPathComponent::impl(ImplPathSegment::from(span(), 0)));
  ZC_EXPECT(DefinitionKey::from(module(), zc::mv(endsInImpl)) == zc::none);

  zc::Vector<DefinitionPathComponent> wrongSource;
  wrongSource.add(DefinitionPathComponent::definition(functionSegment(0x33)));
  ZC_EXPECT(DefinitionKey::from(module(), zc::mv(wrongSource)) == zc::none);

  zc::Vector<DefinitionPathSegment> wrongImplParent;
  wrongImplParent.add(functionSegment(0x33));
  ZC_EXPECT(ImplKey::from(module(), zc::mv(wrongImplParent), span(), 0) == zc::none);
}

}  // namespace zomlang::compiler::identity
