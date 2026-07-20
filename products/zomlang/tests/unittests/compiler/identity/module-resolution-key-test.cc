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

#include "zomlang/compiler/identity/module-resolution-key.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

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

PackageKey localPackage() {
  zc::Vector<CanonicalPathSegment> segments;
  auto path = CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
  return PackageKey::from(CanonicalPackageSource::localPath(zc::mv(path)),
                          requireScalar<PackageName>("test"_zc), requireVersion(),
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

CompilationConfigKey targetCompilation() {
  zc::Maybe<BuildScriptProducerKey> noOutput;
  auto value = CompilationConfigKey::from(
      CompilationDomain::Target, targetSpec(),
      SemanticCompilerOptionsKey::from(2026, true, false, false), zc::mv(noOutput));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid compilation configuration test input");
}

CrateKey crate() {
  auto value = CrateKey::from(localPackage(), CrateTargetKind::Library,
                              requireScalar<TargetName>("lib"_zc), targetCompilation());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid crate test input");
}

ModuleKey requester() {
  zc::Vector<ModulePathSegment> path;
  path.add(requireScalar<ModulePathSegment>("app"_zc));
  auto value = ModuleKey::from(crate(), zc::mv(path));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid requester module test input");
}

ModuleResolutionPolicyKey policy() {
  auto value = ModuleResolutionPolicyKey::from(
      UnicodeNormalizationPolicy::Nfc, CaseComparisonPolicy::CaseSensitive,
      SymlinkHandlingPolicy::ResolveThenConfine, ModuleContainmentPolicy::DeclaredRootsOnly,
      LocalModuleLookupPolicy::RequesterAncestryAndCrateRoot,
      DependencyAliasLookupPolicy::ExactFirstSegment, PreludeLookupPolicy::ConfiguredCratePrelude,
      ModuleCandidateSelectionPolicy::AllDistinctMatchesNoPrecedence);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid module-resolution policy was rejected");
}

zc::Vector<ModulePathSegment> canonicalPath(zc::StringPtr first, zc::StringPtr second = nullptr,
                                            zc::StringPtr third = nullptr) {
  zc::Vector<ModulePathSegment> segments;
  if (first != nullptr) { segments.add(requireScalar<ModulePathSegment>(first)); }
  if (second != nullptr) { segments.add(requireScalar<ModulePathSegment>(second)); }
  if (third != nullptr) { segments.add(requireScalar<ModulePathSegment>(third)); }
  return segments;
}

ModuleKey module(zc::StringPtr first, zc::StringPtr second = nullptr,
                 zc::StringPtr third = nullptr) {
  auto value = ModuleKey::from(crate(), canonicalPath(first, second, third));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid module-key test input");
}

ModuleCatalogPathBucketKey bucketKey(zc::StringPtr first, zc::StringPtr second = nullptr) {
  auto value = ModuleCatalogPathBucketKey::from(crate(), canonicalPath(first, second));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid module-catalog bucket-key test input");
}

zc::Maybe<zc::Vector<ModulePathSegment>> path(zc::StringPtr first, zc::StringPtr second = nullptr) {
  return canonicalPath(first, second);
}

zc::Maybe<DependencyAlias> alias(zc::StringPtr text) {
  if (text == nullptr) { return zc::none; }
  return requireScalar<DependencyAlias>(text);
}

zc::Maybe<ModuleResolutionKey> request(ModuleDependencyKind kind,
                                       zc::Maybe<zc::Vector<ModulePathSegment>>&& normalizedPath,
                                       zc::Maybe<DependencyAlias>&& dependencyAlias) {
  return ModuleResolutionKey::from(requester(), kind, zc::mv(normalizedPath),
                                   zc::mv(dependencyAlias), policy());
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

ZC_TEST("ModuleCatalogPathBucketKey passes the fixed canonical codec vector") {
  auto value = ModuleCatalogPathBucketKey::from(crate(), canonicalPath("dep"_zc, "util"_zc));
  ZC_REQUIRE(value != zc::none);
  ZC_IF_SOME(key, value) {
    auto encoded = key.encode();
    ZC_EXPECT(encoded.size() == 190);
    expectDigest(encoded.asPtr(),
                 "960b37b6e951a701f73b194f98122460910d0c8a3498669993097fe903046c1f"_zc);
    ZC_EXPECT(key.clone().encode().asPtr() == encoded.asPtr());
    ZC_EXPECT(key.crate().encode().asPtr() == crate().encode().asPtr());
    ZC_REQUIRE(key.path().size() == 2);
    ZC_EXPECT(key.path()[0].text() == "dep"_zc);
    ZC_EXPECT(key.path()[1].text() == "util"_zc);
  }
}

ZC_TEST("ModuleCatalogPathBucketKey rejects an empty canonical path") {
  zc::Vector<ModulePathSegment> empty;
  ZC_EXPECT(ModuleCatalogPathBucketKey::from(crate(), zc::mv(empty)) == zc::none);
}

ZC_TEST("ModuleCatalogPathBucketKey decoder is exact bounded and domain separated") {
  auto original = bucketKey("dep"_zc, "util"_zc);
  auto encoded = original.encode();
  auto decoded = ModuleCatalogPathBucketKey::decodeCanonical(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_IF_SOME(value, decoded) { ZC_EXPECT(value.encode().asPtr() == encoded.asPtr()); }

  auto truncated = zc::heapArray(encoded.asPtr().slice(0, encoded.size() - 1));
  ZC_EXPECT(ModuleCatalogPathBucketKey::decodeCanonical(truncated.asPtr()) == zc::none);
  auto wrongDomain = zc::heapArray(encoded.asPtr());
  wrongDomain[0] ^= 0x01;
  ZC_EXPECT(ModuleCatalogPathBucketKey::decodeCanonical(wrongDomain.asPtr()) == zc::none);
  zc::Vector<uint8_t> trailing(encoded.size() + 1);
  trailing.addAll(encoded.asPtr());
  trailing.add(0x00);
  ZC_EXPECT(ModuleCatalogPathBucketKey::decodeCanonical(trailing.asPtr()) == zc::none);
}

ZC_TEST("RequesterModuleAncestry admits the exact requester-first lexical chain") {
  zc::Vector<ModuleKey> chain;
  chain.add(module("app"_zc, "inner"_zc));
  chain.add(module("app"_zc));
  auto value = RequesterModuleAncestry::from(module("app"_zc, "inner"_zc), zc::mv(chain));
  ZC_REQUIRE(value != zc::none);
  ZC_IF_SOME(ancestry, value) {
    const auto encoded = ancestry.encode();
    const auto digest = sha256(encoded.asPtr());
    ZC_EXPECT(encoded.size() == 309);
    ZC_REQUIRE(digest != zc::none);
    ZC_IF_SOME(actual, digest) {
      const auto hex = zc::encodeHex(actual.bytes());
      ZC_EXPECT(hex == "8e3fc6f925d1072411d2ce7a064f8616ac060d875089244c562a86da41ea3cc6"_zc);
    }
    ZC_EXPECT(ancestry.requester().encode().asPtr() ==
              module("app"_zc, "inner"_zc).encode().asPtr());
    ZC_REQUIRE(ancestry.ancestry().size() == 2);
    ZC_EXPECT(ancestry.clone().encode().asPtr() == encoded.asPtr());
  }
}

ZC_TEST("RequesterModuleAncestry rejects empty mismatched and skipped chains") {
  zc::Vector<ModuleKey> empty;
  ZC_EXPECT(RequesterModuleAncestry::from(module("app"_zc), zc::mv(empty)) == zc::none);

  zc::Vector<ModuleKey> wrongFirst;
  wrongFirst.add(module("other"_zc));
  ZC_EXPECT(RequesterModuleAncestry::from(module("app"_zc), zc::mv(wrongFirst)) == zc::none);

  zc::Vector<ModuleKey> skipped;
  skipped.add(module("app"_zc, "inner"_zc, "deep"_zc));
  skipped.add(module("app"_zc));
  ZC_EXPECT(RequesterModuleAncestry::from(module("app"_zc, "inner"_zc, "deep"_zc),
                                          zc::mv(skipped)) == zc::none);
}

ZC_TEST("RequesterModuleAncestry decoder rejects truncation trailing data and invalid chains") {
  zc::Vector<ModuleKey> chain;
  chain.add(module("app"_zc, "inner"_zc));
  chain.add(module("app"_zc));
  auto original = RequesterModuleAncestry::from(module("app"_zc, "inner"_zc), zc::mv(chain));
  ZC_REQUIRE(original != zc::none);
  ZC_IF_SOME(value, original) {
    auto encoded = value.encode();
    auto decoded = RequesterModuleAncestry::decodeCanonical(encoded.asPtr());
    ZC_REQUIRE(decoded != zc::none);
    ZC_IF_SOME(roundTrip, decoded) { ZC_EXPECT(roundTrip.encode().asPtr() == encoded.asPtr()); }
    auto truncated = zc::heapArray(encoded.asPtr().slice(0, encoded.size() - 1));
    ZC_EXPECT(RequesterModuleAncestry::decodeCanonical(truncated.asPtr()) == zc::none);
    zc::Vector<uint8_t> trailing(encoded.size() + 1);
    trailing.addAll(encoded.asPtr());
    trailing.add(0x00);
    ZC_EXPECT(RequesterModuleAncestry::decodeCanonical(trailing.asPtr()) == zc::none);
  }

  CanonicalEncoder invalid;
  invalid.encodeSequenceSize(2);
  module("app"_zc, "inner"_zc, "deep"_zc).encode(invalid);
  module("app"_zc).encode(invalid);
  auto invalidBytes = invalid.finish();
  ZC_EXPECT(RequesterModuleAncestry::decodeCanonical(invalidBytes.asPtr()) == zc::none);
}

ZC_TEST("ModuleCatalogPathBucket admits exact present and absent values") {
  auto absent = ModuleCatalogPathBucket::absent(bucketKey("dep"_zc, "util"_zc));
  ZC_EXPECT(zc::encodeHex(absent.encode().asPtr()) == "00"_zc);
  ZC_EXPECT(absent.module() == zc::none);
  ZC_EXPECT(absent.clone().encode().asPtr() == absent.encode().asPtr());

  auto present =
      ModuleCatalogPathBucket::present(bucketKey("dep"_zc, "util"_zc), module("dep"_zc, "util"_zc));
  ZC_REQUIRE(present != zc::none);
  ZC_IF_SOME(bucket, present) {
    const auto encoded = bucket.encode();
    const auto digest = sha256(encoded.asPtr());
    ZC_EXPECT(encoded.size() == 157);
    ZC_REQUIRE(digest != zc::none);
    ZC_IF_SOME(actual, digest) {
      const auto hex = zc::encodeHex(actual.bytes());
      ZC_EXPECT(hex == "fe5c087376553d8ceaec44339f203f73f1f39debd3a569b1052625c879e8442a"_zc);
    }
    ZC_REQUIRE(bucket.module() != zc::none);
    ZC_IF_SOME(value, bucket.module()) {
      ZC_EXPECT(value.encode().asPtr() == module("dep"_zc, "util"_zc).encode().asPtr());
    }
    ZC_EXPECT(bucket.clone().encode().asPtr() == encoded.asPtr());
  }

  ZC_EXPECT(ModuleCatalogPathBucket::present(bucketKey("dep"_zc, "util"_zc),
                                             module("dep"_zc, "other"_zc)) == zc::none);
}

ZC_TEST("ModuleCatalogPathBucket decoder validates its external key") {
  auto present =
      ModuleCatalogPathBucket::present(bucketKey("dep"_zc, "util"_zc), module("dep"_zc, "util"_zc));
  ZC_REQUIRE(present != zc::none);
  ZC_IF_SOME(value, present) {
    auto encoded = value.encode();
    auto decoded =
        ModuleCatalogPathBucket::decodeCanonical(bucketKey("dep"_zc, "util"_zc), encoded.asPtr());
    ZC_REQUIRE(decoded != zc::none);
    ZC_IF_SOME(roundTrip, decoded) { ZC_EXPECT(roundTrip.encode().asPtr() == encoded.asPtr()); }
    ZC_EXPECT(ModuleCatalogPathBucket::decodeCanonical(bucketKey("dep"_zc, "other"_zc),
                                                       encoded.asPtr()) == zc::none);
    zc::Vector<uint8_t> trailing(encoded.size() + 1);
    trailing.addAll(encoded.asPtr());
    trailing.add(0x00);
    ZC_EXPECT(ModuleCatalogPathBucket::decodeCanonical(bucketKey("dep"_zc, "util"_zc),
                                                       trailing.asPtr()) == zc::none);
  }
  auto absent = ModuleCatalogPathBucket::decodeCanonical(
      bucketKey("dep"_zc, "util"_zc),
      zc::arrayPtr(static_cast<const uint8_t*>(nullptr), size_t{0}));
  ZC_EXPECT(absent == zc::none);
}

ZC_TEST("ModuleResolutionPolicyKey passes the exact fixed byte vector") {
  auto encoded = policy().encode();
  ZC_EXPECT(zc::encodeHex(encoded.asPtr()) ==
            "7a6f6d2e6d6f64756c652d7265736f6c7574696f6e2d706f6c6963792e7630000101010101010101"_zc);
}

ZC_TEST("ModuleResolutionPolicyKey rejects every unknown field tag") {
  constexpr auto invalidUnicode = static_cast<UnicodeNormalizationPolicy>(0xff);
  constexpr auto invalidCase = static_cast<CaseComparisonPolicy>(0xff);
  constexpr auto invalidSymlink = static_cast<SymlinkHandlingPolicy>(0xff);
  constexpr auto invalidContainment = static_cast<ModuleContainmentPolicy>(0xff);
  constexpr auto invalidLocal = static_cast<LocalModuleLookupPolicy>(0xff);
  constexpr auto invalidAlias = static_cast<DependencyAliasLookupPolicy>(0xff);
  constexpr auto invalidPrelude = static_cast<PreludeLookupPolicy>(0xff);
  constexpr auto invalidSelection = static_cast<ModuleCandidateSelectionPolicy>(0xff);

  ZC_EXPECT(ModuleResolutionPolicyKey::from(
                invalidUnicode, CaseComparisonPolicy::CaseSensitive,
                SymlinkHandlingPolicy::ResolveThenConfine,
                ModuleContainmentPolicy::DeclaredRootsOnly,
                LocalModuleLookupPolicy::RequesterAncestryAndCrateRoot,
                DependencyAliasLookupPolicy::ExactFirstSegment,
                PreludeLookupPolicy::ConfiguredCratePrelude,
                ModuleCandidateSelectionPolicy::AllDistinctMatchesNoPrecedence) == zc::none);
  ZC_EXPECT(ModuleResolutionPolicyKey::from(
                UnicodeNormalizationPolicy::Nfc, invalidCase,
                SymlinkHandlingPolicy::ResolveThenConfine,
                ModuleContainmentPolicy::DeclaredRootsOnly,
                LocalModuleLookupPolicy::RequesterAncestryAndCrateRoot,
                DependencyAliasLookupPolicy::ExactFirstSegment,
                PreludeLookupPolicy::ConfiguredCratePrelude,
                ModuleCandidateSelectionPolicy::AllDistinctMatchesNoPrecedence) == zc::none);
  ZC_EXPECT(ModuleResolutionPolicyKey::from(
                UnicodeNormalizationPolicy::Nfc, CaseComparisonPolicy::CaseSensitive,
                invalidSymlink, ModuleContainmentPolicy::DeclaredRootsOnly,
                LocalModuleLookupPolicy::RequesterAncestryAndCrateRoot,
                DependencyAliasLookupPolicy::ExactFirstSegment,
                PreludeLookupPolicy::ConfiguredCratePrelude,
                ModuleCandidateSelectionPolicy::AllDistinctMatchesNoPrecedence) == zc::none);
  ZC_EXPECT(ModuleResolutionPolicyKey::from(
                UnicodeNormalizationPolicy::Nfc, CaseComparisonPolicy::CaseSensitive,
                SymlinkHandlingPolicy::ResolveThenConfine, invalidContainment,
                LocalModuleLookupPolicy::RequesterAncestryAndCrateRoot,
                DependencyAliasLookupPolicy::ExactFirstSegment,
                PreludeLookupPolicy::ConfiguredCratePrelude,
                ModuleCandidateSelectionPolicy::AllDistinctMatchesNoPrecedence) == zc::none);
  ZC_EXPECT(ModuleResolutionPolicyKey::from(
                UnicodeNormalizationPolicy::Nfc, CaseComparisonPolicy::CaseSensitive,
                SymlinkHandlingPolicy::ResolveThenConfine,
                ModuleContainmentPolicy::DeclaredRootsOnly, invalidLocal,
                DependencyAliasLookupPolicy::ExactFirstSegment,
                PreludeLookupPolicy::ConfiguredCratePrelude,
                ModuleCandidateSelectionPolicy::AllDistinctMatchesNoPrecedence) == zc::none);
  ZC_EXPECT(ModuleResolutionPolicyKey::from(
                UnicodeNormalizationPolicy::Nfc, CaseComparisonPolicy::CaseSensitive,
                SymlinkHandlingPolicy::ResolveThenConfine,
                ModuleContainmentPolicy::DeclaredRootsOnly,
                LocalModuleLookupPolicy::RequesterAncestryAndCrateRoot, invalidAlias,
                PreludeLookupPolicy::ConfiguredCratePrelude,
                ModuleCandidateSelectionPolicy::AllDistinctMatchesNoPrecedence) == zc::none);
  ZC_EXPECT(ModuleResolutionPolicyKey::from(
                UnicodeNormalizationPolicy::Nfc, CaseComparisonPolicy::CaseSensitive,
                SymlinkHandlingPolicy::ResolveThenConfine,
                ModuleContainmentPolicy::DeclaredRootsOnly,
                LocalModuleLookupPolicy::RequesterAncestryAndCrateRoot,
                DependencyAliasLookupPolicy::ExactFirstSegment, invalidPrelude,
                ModuleCandidateSelectionPolicy::AllDistinctMatchesNoPrecedence) == zc::none);
  ZC_EXPECT(ModuleResolutionPolicyKey::from(
                UnicodeNormalizationPolicy::Nfc, CaseComparisonPolicy::CaseSensitive,
                SymlinkHandlingPolicy::ResolveThenConfine,
                ModuleContainmentPolicy::DeclaredRootsOnly,
                LocalModuleLookupPolicy::RequesterAncestryAndCrateRoot,
                DependencyAliasLookupPolicy::ExactFirstSegment,
                PreludeLookupPolicy::ConfiguredCratePrelude, invalidSelection) == zc::none);
}

ZC_TEST("ModuleResolutionPolicyKey decoder closes the domain and record") {
  auto encoded = policy().encode();
  auto decoded = ModuleResolutionPolicyKey::decodeCanonical(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_IF_SOME(value, decoded) { ZC_EXPECT(value.encode().asPtr() == encoded.asPtr()); }

  auto unknown = zc::heapArray(encoded.asPtr());
  unknown[unknown.size() - 1] = 0xff;
  ZC_EXPECT(ModuleResolutionPolicyKey::decodeCanonical(unknown.asPtr()) == zc::none);
  zc::Vector<uint8_t> trailing(encoded.size() + 1);
  trailing.addAll(encoded.asPtr());
  trailing.add(0x00);
  ZC_EXPECT(ModuleResolutionPolicyKey::decodeCanonical(trailing.asPtr()) == zc::none);
}

ZC_TEST("ModuleResolutionKey passes fixed import and prelude vectors") {
  auto importValue =
      request(ModuleDependencyKind::Import, path("dep"_zc, "util"_zc), alias("dep"_zc));
  ZC_REQUIRE(importValue != zc::none);
  ZC_IF_SOME(importKey, importValue) {
    auto encoded = importKey.encode();
    ZC_EXPECT(encoded.size() == 262);
    expectDigest(encoded.asPtr(),
                 "d687937e945b71e166af344e903016bece3d2a60cdcfb321896282766a6c5159"_zc);
    ZC_EXPECT(importKey.clone().encode().asPtr() == encoded.asPtr());
  }

  auto preludeValue = request(ModuleDependencyKind::Prelude, zc::none, zc::none);
  ZC_REQUIRE(preludeValue != zc::none);
  ZC_IF_SOME(preludeKey, preludeValue) {
    auto encoded = preludeKey.encode();
    ZC_EXPECT(encoded.size() == 220);
    expectDigest(encoded.asPtr(),
                 "91734bcd549f9a0c70d410de768a90a0d51842d013635fd0d0ad1e064fcc2f1c"_zc);
  }
}

ZC_TEST("ModuleResolutionKey encodes all dependency-kind tags") {
  constexpr auto domainSize = "zom.module-resolution.v0"_zc.size() + 1;
  auto requesterBytes = requester().encode();
  const size_t kindOffset = domainSize + requesterBytes.size();

  auto importValue = request(ModuleDependencyKind::Import, path("dep"_zc), alias("dep"_zc));
  auto reexportValue =
      request(ModuleDependencyKind::ForeignReexport, path("dep"_zc), alias("dep"_zc));
  auto moduleAliasValue =
      request(ModuleDependencyKind::ModuleAlias, path("dep"_zc), alias("dep"_zc));
  auto preludeValue = request(ModuleDependencyKind::Prelude, zc::none, zc::none);
  ZC_REQUIRE(importValue != zc::none);
  ZC_REQUIRE(reexportValue != zc::none);
  ZC_REQUIRE(moduleAliasValue != zc::none);
  ZC_REQUIRE(preludeValue != zc::none);
  ZC_IF_SOME(value, importValue) { ZC_EXPECT(value.encode()[kindOffset] == 0x01); }
  ZC_IF_SOME(value, reexportValue) { ZC_EXPECT(value.encode()[kindOffset] == 0x02); }
  ZC_IF_SOME(value, moduleAliasValue) { ZC_EXPECT(value.encode()[kindOffset] == 0x03); }
  ZC_IF_SOME(value, preludeValue) { ZC_EXPECT(value.encode()[kindOffset] == 0x04); }
}

ZC_TEST("ModuleResolutionKey rejects malformed enums and records") {
  ZC_EXPECT(request(static_cast<ModuleDependencyKind>(0xff), path("dep"_zc), alias("dep"_zc)) ==
            zc::none);
  ZC_EXPECT(request(ModuleDependencyKind::Import, zc::none, zc::none) == zc::none);
  ZC_EXPECT(request(ModuleDependencyKind::Import, path(nullptr), zc::none) == zc::none);
  ZC_EXPECT(request(ModuleDependencyKind::Import, path("dep"_zc), alias("other"_zc)) == zc::none);
  ZC_EXPECT(request(ModuleDependencyKind::Prelude, path("prelude"_zc), zc::none) == zc::none);
  ZC_EXPECT(request(ModuleDependencyKind::Prelude, zc::none, alias("prelude"_zc)) == zc::none);
}

ZC_TEST("ModuleResolutionKey decoder is exact and rejects closed-tag mutations") {
  auto original = request(ModuleDependencyKind::Import, path("dep"_zc, "util"_zc), alias("dep"_zc));
  ZC_REQUIRE(original != zc::none);
  ZC_IF_SOME(value, original) {
    auto encoded = value.encode();
    auto decoded = ModuleResolutionKey::decodeCanonical(encoded.asPtr());
    ZC_REQUIRE(decoded != zc::none);
    ZC_IF_SOME(roundTrip, decoded) { ZC_EXPECT(roundTrip.encode().asPtr() == encoded.asPtr()); }

    auto truncated = zc::heapArray(encoded.asPtr().slice(0, encoded.size() - 1));
    ZC_EXPECT(ModuleResolutionKey::decodeCanonical(truncated.asPtr()) == zc::none);
    auto unknownKind = zc::heapArray(encoded.asPtr());
    constexpr auto domainSize = "zom.module-resolution.v0"_zc.size() + 1;
    unknownKind[domainSize + requester().encode().size()] = 0xff;
    ZC_EXPECT(ModuleResolutionKey::decodeCanonical(unknownKind.asPtr()) == zc::none);
    zc::Vector<uint8_t> trailing(encoded.size() + 1);
    trailing.addAll(encoded.asPtr());
    trailing.add(0x00);
    ZC_EXPECT(ModuleResolutionKey::decodeCanonical(trailing.asPtr()) == zc::none);
  }

  auto prelude = request(ModuleDependencyKind::Prelude, zc::none, zc::none);
  ZC_REQUIRE(prelude != zc::none);
  ZC_IF_SOME(value, prelude) {
    ZC_EXPECT(ModuleResolutionKey::decodeCanonical(value.encode().asPtr()) != zc::none);
  }
}

}  // namespace zomlang::compiler::identity
