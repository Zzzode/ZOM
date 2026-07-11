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

#include "zomlang/compiler/identity/source-snapshot.h"
#include "zomlang/compiler/identity/source-manager-identity-resolver.h"

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
  return PackageKey::from(
      CanonicalPackageSource::localPath(zc::mv(path)), requireScalar<PackageName>(name),
      requireVersion(), emptyPackageFeatures());
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

CanonicalRelativePath logicalPath() {
  zc::Vector<CanonicalPathSegment> segments;
  segments.add(requireScalar<CanonicalPathSegment>("g.zom"_zc));
  return CanonicalRelativePath::from(zc::mv(segments));
}

SourceFileKey source(zc::StringPtr packageName = "a"_zc, uint8_t contentByte = 0x22) {
  auto origin = SourceOriginKey::generatedFile(BuildScriptOutputKey::from(repeatedDigest(0x11)),
                                               logicalPath(), repeatedDigest(contentByte));
  return SourceFileKey::from(crate(packageName), zc::mv(origin));
}

SourceFileKey localSource() {
  zc::Vector<CanonicalPathSegment> segments;
  segments.add(requireScalar<CanonicalPathSegment>("main.zom"_zc));
  auto path = CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
  return SourceFileKey::from(crate("a"_zc), SourceOriginKey::localFile(zc::mv(path)));
}

ModuleKey module() {
  zc::Vector<ModulePathSegment> path;
  path.add(requireScalar<ModulePathSegment>("m"_zc));
  zc::Maybe<SourceSpan> noAnchor;
  auto value = ModuleKey::from(crate("a"_zc), zc::mv(path), source(), zc::mv(noAnchor));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid module test input");
}

ImmutableSourceSnapshot snapshot(SourceFileKey&& key, size_t byteCount = 1) {
  auto value = ImmutableSourceSnapshot::from(
      zc::mv(key), zc::heapArray<uint8_t>(byteCount, uint8_t{0}));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid immutable source snapshot was rejected");
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

ZC_TEST("SourceFileKey passes the fixed generated-source codec vector") {
  auto key = source();
  auto encoded = key.encode();
  ZC_EXPECT(encoded.size() == 240);
  expectDigest(encoded.asPtr(),
               "f4198087783111e14911a0f550962f5c010ea2609edfdca47152907d74969102"_zc);
}

ZC_TEST("ModuleKey passes the fixed expanded source codec vector") {
  auto key = module();
  auto encoded = key.encode();
  ZC_EXPECT(encoded.size() == 412);
  expectDigest(encoded.asPtr(),
               "8ef9b8baabd646bf1a4640a8bd70af16e93bbe979229c21342cbebd0c429b91b"_zc);
}

ZC_TEST("SourceSpan and ModuleKey reject malformed ancestry") {
  auto validSnapshot = snapshot(source());
  ZC_EXPECT(validSnapshot.span(2, 1) == zc::none);
  ZC_EXPECT(validSnapshot.span(0, 2) == zc::none);
  ZC_EXPECT(validSnapshot.unbrandedRange(0, 2) == zc::none);
  ZC_EXPECT(validSnapshot.span(0, 1) != zc::none);
  ZC_EXPECT(validSnapshot.unbrandedRange(0, 1) != zc::none);

  zc::Vector<ModulePathSegment> emptyPath;
  zc::Maybe<SourceSpan> noAnchor;
  ZC_EXPECT(ModuleKey::from(crate("a"_zc), zc::mv(emptyPath), source(),
                            zc::mv(noAnchor)) == zc::none);

  zc::Vector<ModulePathSegment> wrongCratePath;
  wrongCratePath.add(requireScalar<ModulePathSegment>("m"_zc));
  zc::Maybe<SourceSpan> noWrongCrateAnchor;
  ZC_EXPECT(ModuleKey::from(crate("b"_zc), zc::mv(wrongCratePath), source(),
                            zc::mv(noWrongCrateAnchor)) == zc::none);

  auto wrongSnapshot = snapshot(source("a"_zc, 0x33));
  auto wrongAnchor = wrongSnapshot.span(0, 1);
  zc::Vector<ModulePathSegment> path;
  path.add(requireScalar<ModulePathSegment>("m"_zc));
  ZC_EXPECT(ModuleKey::from(crate("a"_zc), zc::mv(path), source(),
                            zc::mv(wrongAnchor)) == zc::none);
}

ZC_TEST("Source manager resolver binds only byte-identical immutable snapshots") {
  SemanticContextFactory factory;
  auto created = SemanticIdentityRegistrySet::create(
      factory, ZC_ASSERT_NONNULL(factory.issue()));
  ZC_IF_SOME(registries, created) {
    ZC_REQUIRE(registries.collectPackage(localPackage("a"_zc)) == FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezePackages() == FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectCrate(crate("a"_zc)) == FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeCrates() == FrozenRegistryFailure::None);

    auto sourceKey = localSource();
    uint8_t sourceBytes[] = {'A', 'B'};
    auto admittedSnapshot =
        ImmutableSourceSnapshot::from(localSource(), zc::heapArray(zc::arrayPtr(sourceBytes)));
    ZC_IF_SOME(sourceSnapshot, admittedSnapshot) {
      auto range = sourceSnapshot.unbrandedRange(1, 2);
      ZC_REQUIRE(registries.collectSourceFile(zc::mv(sourceSnapshot)) ==
                 FrozenRegistryFailure::None);
      ZC_REQUIRE(registries.freezeSourceFiles() == FrozenRegistryFailure::None);
      auto sourceId = registries.sourceFiles().find(sourceKey);
      ZC_IF_SOME(id, sourceId) {
        source::SourceManager sourceManager;
        auto wrongBuffer = sourceManager.addMemBufferCopy("AX"_zcb, "wrong.zom"_zc);
        auto buffer = sourceManager.addMemBufferCopy("AB"_zcb, "main.zom"_zc);
        auto resolverValue = SourceManagerIdentityResolver::create(registries, sourceManager);
        ZC_IF_SOME(resolver, resolverValue) {
          ZC_EXPECT(!resolver.bind(id, wrongBuffer));
          ZC_EXPECT(resolver.bind(id, buffer));
          ZC_EXPECT(!resolver.bind(id, buffer));
          ZC_IF_SOME(validRange, range) {
            auto location = resolver.resolve(validRange);
            ZC_IF_SOME(loc, location) {
              ZC_EXPECT(sourceManager.getLocOffsetInBuffer(loc, buffer) == 1);
            }
          }
        }
      }
    }
  }
}

}  // namespace zomlang::compiler::identity
