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

#include "zomlang/compiler/identity/build-script-key.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::identity {
namespace {

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto result = Scalar::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid build-script identity scalar fixture");
}

SortedFeatureSet emptyFeatures() {
  zc::Vector<FeatureName> features;
  auto result = SortedFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("empty feature set was rejected");
}

PackageKey package(zc::StringPtr name) {
  zc::Vector<CanonicalPathSegment> segments;
  auto source =
      CanonicalPackageSource::localPath(CanonicalWorkspaceRelativePath::from(0, zc::mv(segments)));
  return PackageKey::from(zc::mv(source), scalar<PackageName>(name),
                          scalar<ResolvedVersion>("1.0.0"_zc), emptyFeatures());
}

CanonicalTargetSpecificationKey hostTarget() {
  zc::Vector<TargetFeatureName> features;
  auto sorted = SortedTargetFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(featureSet, sorted) {
    auto result = CanonicalTargetSpecificationKey::from(
        scalar<TargetComponentName>("x86_64"_zc), scalar<TargetComponentName>("zom"_zc),
        scalar<TargetComponentName>("linux"_zc), scalar<TargetComponentName>("gnu"_zc),
        scalar<TargetComponentName>("zom-v1"_zc), 64, Endianness::Little, zc::mv(featureSet));
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("host target fixture was rejected");
}

CanonicalRelativePath path(zc::StringPtr text) {
  zc::Vector<CanonicalPathSegment> segments;
  segments.add(scalar<CanonicalPathSegment>(text));
  return CanonicalRelativePath::from(zc::mv(segments));
}

Sha256Digest digest(zc::StringPtr text) {
  auto result = sha256(text.asBytes());
  ZC_IF_SOME(value, result) { return value; }
  ZC_FAIL_REQUIRE("digest fixture failed");
}

zc::Array<uint8_t> bytes(zc::StringPtr text) {
  zc::Vector<uint8_t> result(text.size());
  result.addAll(text.asBytes());
  return result.releaseAsArray();
}

PreparatoryBuildScriptKey preparatory(bool reverse = false) {
  zc::Vector<PackageKey> dependencies;
  if (reverse) {
    dependencies.add(package("z"_zc));
    dependencies.add(package("a"_zc));
  } else {
    dependencies.add(package("a"_zc));
    dependencies.add(package("z"_zc));
  }
  auto result = PreparatoryBuildScriptKey::from(
      package("app"_zc), scalar<TargetName>("build"_zc), hostTarget(),
      SemanticCompilerOptionsKey::from(2026, true, false, true), zc::mv(dependencies));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("preparatory key fixture was rejected");
}

BuildScriptOutputRecord outputRecord(bool reverse = false, bool changedContents = false) {
  zc::Vector<BuildScriptDigestEntry> sources;
  zc::Vector<BuildScriptEnvironmentEntry> environment;
  zc::Vector<BuildScriptDigestEntry> generated;
  zc::Vector<BuildScriptEnvironmentEntry> exported;
  if (reverse) {
    sources.add(BuildScriptDigestEntry::from(path("z.zom"_zc), digest("z"_zc)));
    sources.add(BuildScriptDigestEntry::from(path("a.zom"_zc),
                                             digest(changedContents ? "changed"_zc : "a"_zc)));
    generated.add(BuildScriptDigestEntry::from(path("out-z.zom"_zc), digest("oz"_zc)));
    generated.add(BuildScriptDigestEntry::from(path("out-a.zom"_zc), digest("oa"_zc)));
  } else {
    sources.add(BuildScriptDigestEntry::from(path("a.zom"_zc),
                                             digest(changedContents ? "changed"_zc : "a"_zc)));
    sources.add(BuildScriptDigestEntry::from(path("z.zom"_zc), digest("z"_zc)));
    generated.add(BuildScriptDigestEntry::from(path("out-a.zom"_zc), digest("oa"_zc)));
    generated.add(BuildScriptDigestEntry::from(path("out-z.zom"_zc), digest("oz"_zc)));
  }
  environment.add(BuildScriptEnvironmentEntry::from(scalar<SemanticEnvironmentName>("TARGET"_zc),
                                                    bytes("host"_zc)));
  exported.add(BuildScriptEnvironmentEntry::from(scalar<SemanticEnvironmentName>("MODE"_zc),
                                                 bytes("fast"_zc)));
  auto producer = preparatory(reverse).producerKey();
  auto result = BuildScriptOutputRecord::from(producer, zc::mv(sources), zc::mv(environment),
                                              zc::mv(generated), zc::mv(exported));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("output record fixture was rejected");
}

}  // namespace

ZC_TEST("PreparatoryBuildScriptKey canonicalizes dependencies and rejects duplicates") {
  auto first = preparatory(false);
  auto second = preparatory(true);
  ZC_EXPECT(first.encode().asPtr() == second.encode().asPtr());
  ZC_EXPECT(first.producerKey().digest() == second.producerKey().digest());
  ZC_EXPECT(zc::encodeHex(first.producerKey().digest().bytes()) ==
            "95fdf60a815f20e321ef6ed7d3dc4962744155b22128328403e44199f36f6800"_zc);

  auto rawDigest = sha256(first.encode());
  ZC_REQUIRE(rawDigest != zc::none);
  ZC_IF_SOME(value, rawDigest) { ZC_EXPECT(value != first.producerKey().digest()); }

  zc::Vector<PackageKey> duplicates;
  duplicates.add(package("dep"_zc));
  duplicates.add(package("dep"_zc));
  auto result = PreparatoryBuildScriptKey::from(
      package("app"_zc), scalar<TargetName>("build"_zc), hostTarget(),
      SemanticCompilerOptionsKey::from(2026, true, false, true), zc::mv(duplicates));
  ZC_EXPECT(result == zc::none);
}

ZC_TEST("Build-script output changes preserve producer identity") {
  auto first = outputRecord(false, false);
  auto second = outputRecord(false, true);
  ZC_EXPECT(first.producerKey().digest() == second.producerKey().digest());
  ZC_EXPECT(first.artifactFingerprint().digest() != second.artifactFingerprint().digest());
}

ZC_TEST("BuildScriptOutputRecord canonicalizes maps and domain-separates its fingerprint") {
  auto first = outputRecord(false);
  auto second = outputRecord(true);
  ZC_EXPECT(first.encode().asPtr() == second.encode().asPtr());
  ZC_EXPECT(first.artifactFingerprint().digest() == second.artifactFingerprint().digest());

  auto encoded = first.encode();
  auto rawDigest = sha256(encoded);
  ZC_REQUIRE(rawDigest != zc::none);
  ZC_IF_SOME(value, rawDigest) { ZC_EXPECT(value != first.artifactFingerprint().digest()); }
}

ZC_TEST("BuildScriptOutputRecord rejects duplicate map keys") {
  zc::Vector<BuildScriptDigestEntry> sources;
  sources.add(BuildScriptDigestEntry::from(path("a.zom"_zc), digest("a"_zc)));
  sources.add(BuildScriptDigestEntry::from(path("a.zom"_zc), digest("different"_zc)));
  zc::Vector<BuildScriptEnvironmentEntry> environment;
  zc::Vector<BuildScriptDigestEntry> generated;
  zc::Vector<BuildScriptEnvironmentEntry> exported;
  auto result =
      BuildScriptOutputRecord::from(preparatory().producerKey(), zc::mv(sources),
                                    zc::mv(environment), zc::mv(generated), zc::mv(exported));
  ZC_EXPECT(result == zc::none);
}

}  // namespace zomlang::compiler::identity
