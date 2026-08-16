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

#include "zomlang/compiler/driver/package/manifest-model.h"

#include "zc/core/encoding.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::driver::package {
namespace {

identity::PackageName packageName(zc::StringPtr text) {
  auto value = identity::PackageName::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid package name fixture");
}

identity::ResolvedVersion version(zc::StringPtr text) {
  auto value = identity::ResolvedVersion::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid version fixture");
}

identity::CanonicalWorkspaceRelativePath workspacePath(zc::StringPtr text) {
  auto segment = identity::CanonicalPathSegment::fromCanonical(text);
  zc::Vector<identity::CanonicalPathSegment> segments;
  ZC_IF_SOME(admitted, segment) { segments.add(zc::mv(admitted)); }
  ZC_REQUIRE(segments.size() == 1);
  return identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
}

identity::CanonicalRelativePath relativePath(zc::StringPtr first, zc::StringPtr second) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  for (auto text : {first, second}) {
    auto segment = identity::CanonicalPathSegment::fromCanonical(text);
    ZC_IF_SOME(admitted, segment) { segments.add(zc::mv(admitted)); }
  }
  ZC_REQUIRE(segments.size() == 2);
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

identity::TargetName targetName(zc::StringPtr text) {
  auto value = identity::TargetName::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid target name fixture");
}

identity::FeatureName featureName(zc::StringPtr text) {
  auto value = identity::FeatureName::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid feature name fixture");
}

identity::SemanticEnvironmentName environmentName(zc::StringPtr text) {
  auto value = identity::SemanticEnvironmentName::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid environment-name fixture");
}

identity::DependencyAlias dependencyAlias(zc::StringPtr text) {
  auto value = identity::DependencyAlias::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid dependency alias fixture");
}

InputDocumentKey documentKey();

DiagnosticAnchor origin() {
  auto span = ManifestSpan::from(documentKey(), 100, 12, 34);
  ZC_IF_SOME(admitted, span) { return DiagnosticAnchor::manifest(zc::mv(admitted)); }
  ZC_FAIL_REQUIRE("valid manifest origin was rejected");
}

InputDocumentKey documentKey() {
  auto document = InputDocumentKey::from(
      InputDocumentKind::Manifest, DiagnosticDocumentPath::workspace(workspacePath("Zom.toml"_zc)),
      identity::Sha256Digest());
  ZC_IF_SOME(admitted, document) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid input document key was rejected");
}

void expectDigest(zc::ArrayPtr<const uint8_t> bytes, zc::StringPtr expected) {
  auto digest = identity::sha256(bytes);
  ZC_IF_SOME(value, digest) {
    ZC_EXPECT(zc::encodeHex(value.bytes()) == expected);
    return;
  }
  ZC_FAIL_EXPECT("manifest model bytes could not be hashed");
}

}  // namespace

ZC_TEST("ManifestModel.PackageManifestPassesFixedCodecVector") {
  const uint8_t expected[] = {
      0, 0, 0, 0, 0,   0,   0,   1,   'a', 0, 0, 0,    0,
      0, 0, 0, 5, '0', '.', '0', '.', '0', 0, 0, 0x07, 0xea,
  };
  auto package = PackageManifest::from(packageName("a"_zc), version("0.0.0"_zc), 2026);
  auto encoded = package.encode();
  ZC_EXPECT(encoded.asPtr() == zc::arrayPtr(expected));
  ZC_EXPECT(package.name() == "a"_zc);
  ZC_EXPECT(package.version() == "0.0.0"_zc);
  ZC_EXPECT(package.editionYear() == 2026);
  expectDigest(encoded.asPtr(),
               "985acd76f63afee5540cade9888f595b89cd13bdbc3ea8808993f7770b007272"_zc);
}

ZC_TEST("ManifestModel.WorkspaceMembersSortAndRejectDuplicates") {
  zc::Vector<identity::CanonicalWorkspaceRelativePath> members;
  members.add(workspacePath("b"_zc));
  members.add(workspacePath("a"_zc));
  auto workspace = WorkspaceManifest::from(zc::mv(members));
  ZC_REQUIRE(workspace != zc::none);
  ZC_IF_SOME(admitted, workspace) {
    const uint8_t expected[] = {
        0, 0, 0, 0,   0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
        0, 0, 1, 'a', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 'b',
    };
    ZC_REQUIRE(admitted.members().size() == 2);
    ZC_EXPECT(admitted.members()[0].segments()[0].text() == "a"_zc);
    ZC_EXPECT(admitted.members()[1].segments()[0].text() == "b"_zc);
    auto encoded = admitted.encode();
    ZC_EXPECT(encoded.size() == 50);
    ZC_EXPECT(encoded.asPtr() == zc::arrayPtr(expected));
    expectDigest(encoded.asPtr(),
                 "9cae7b71111c890171caa8d3567a01fd83f694049720b28aaacaf79288fbed96"_zc);
  }

  zc::Vector<identity::CanonicalWorkspaceRelativePath> duplicates;
  duplicates.add(workspacePath("a"_zc));
  duplicates.add(workspacePath("a"_zc));
  ZC_EXPECT(WorkspaceManifest::from(zc::mv(duplicates)) == zc::none);
}

ZC_TEST("ManifestModel.DocumentAndSpanEnforceClosedInvariants") {
  auto document = documentKey();
  ZC_EXPECT(document.kind() == InputDocumentKind::Manifest);
  ZC_EXPECT(document.encode().size() == 62);
  ZC_EXPECT(InputDocumentKey::from(static_cast<InputDocumentKind>(0xff),
                                   DiagnosticDocumentPath::workspace(workspacePath("Zom.toml"_zc)),
                                   identity::Sha256Digest()) == zc::none);

  auto span = ManifestSpan::from(zc::mv(document), 100, 12, 34);
  ZC_REQUIRE(span != zc::none);
  ZC_IF_SOME(admitted, span) {
    ZC_EXPECT(admitted.byteStart() == 12);
    ZC_EXPECT(admitted.byteEnd() == 34);
  }
  ZC_EXPECT(ManifestSpan::from(documentKey(), 100, 35, 34) == zc::none);
  ZC_EXPECT(ManifestSpan::from(documentKey(), 100, 0, 101) == zc::none);
}

ZC_TEST("ManifestModel.TargetRecordsSeparateOriginFromCanonicalBytes") {
  auto span = ManifestSpan::from(documentKey(), 100, 12, 34);
  ZC_REQUIRE(span != zc::none);
  zc::Maybe<TargetManifest> target;
  ZC_IF_SOME(admittedSpan, span) {
    target = TargetManifest::from(identity::CrateTargetKind::Library, targetName("core"_zc),
                                  relativePath("src"_zc, "lib.zom"_zc), false,
                                  DiagnosticAnchor::manifest(zc::mv(admittedSpan)));
  }
  ZC_REQUIRE(target != zc::none);
  ZC_IF_SOME(admitted, target) {
    auto canonical = CanonicalTargetManifest::from(admitted);
    ZC_EXPECT(canonical.kind() == identity::CrateTargetKind::Library);
    ZC_EXPECT(canonical.name() == "core"_zc);
    ZC_EXPECT(canonical.path().segments().size() == 2);
    ZC_EXPECT(!canonical.implicit());
    auto encoded = canonical.encode();
    ZC_EXPECT(encoded.size() == 48);
    expectDigest(encoded.asPtr(),
                 "76f4e13c0213661cac013a8edb49850060ef0f5d24c7959b43c7ea0d890bab74"_zc);
  }

  auto invalidSpan = ManifestSpan::from(documentKey(), 0, 0, 0);
  ZC_IF_SOME(admittedSpan, invalidSpan) {
    ZC_EXPECT(TargetManifest::from(static_cast<identity::CrateTargetKind>(0xff),
                                   targetName("core"_zc), relativePath("src"_zc, "lib.zom"_zc),
                                   false,
                                   DiagnosticAnchor::manifest(zc::mv(admittedSpan))) == zc::none);
  }
}

ZC_TEST("ManifestModel.FeatureEdgesAreClosedSortedAndUnique") {
  auto local = FeatureEdge::local(featureName("fast"_zc));
  auto dependency = FeatureEdge::enableDependency(dependencyAlias("math"_zc));
  auto dependencyFeature =
      FeatureEdge::enableDependencyFeature(dependencyAlias("math"_zc), featureName("simd"_zc));
  ZC_EXPECT(local.kind() == FeatureEdgeKind::Local);
  ZC_EXPECT(dependency.kind() == FeatureEdgeKind::EnableDependency);
  ZC_EXPECT(dependencyFeature.kind() == FeatureEdgeKind::EnableDependencyFeature);
  ZC_EXPECT(local.encode().size() == 13);
  ZC_EXPECT(dependency.encode().size() == 13);
  ZC_EXPECT(dependencyFeature.encode().size() == 25);
  expectDigest(local.encode().asPtr(),
               "7491d27a5e338da718ad5e3f9b13ea3433135b0e7e51fc987779c1266bf6a8ab"_zc);
  expectDigest(dependency.encode().asPtr(),
               "b943fb6b93f81acc6dfd858f1d7f308d27d368450a60b4947067389ff60b64ac"_zc);
  expectDigest(dependencyFeature.encode().asPtr(),
               "5a74c4234e30e7a064f4ad1dbdaba04b4a2ce7af9bdf2869e5ec21e7c05a3853"_zc);

  zc::Vector<FeatureEdgeRecord> edges;
  edges.add(FeatureEdgeRecord::from(zc::mv(dependencyFeature), origin()));
  edges.add(FeatureEdgeRecord::from(zc::mv(local), origin()));
  edges.add(FeatureEdgeRecord::from(zc::mv(dependency), origin()));
  auto manifest = FeatureManifest::from(featureName("default"_zc), zc::mv(edges));
  ZC_REQUIRE(manifest != zc::none);
  ZC_IF_SOME(admitted, manifest) {
    ZC_REQUIRE(admitted.edges().size() == 3);
    ZC_EXPECT(admitted.edges()[0].edge().kind() == FeatureEdgeKind::Local);
    ZC_EXPECT(admitted.edges()[1].edge().kind() == FeatureEdgeKind::EnableDependency);
    ZC_EXPECT(admitted.edges()[2].edge().kind() == FeatureEdgeKind::EnableDependencyFeature);
  }

  zc::Vector<FeatureEdgeRecord> duplicates;
  duplicates.add(FeatureEdgeRecord::from(FeatureEdge::local(featureName("fast"_zc)), origin()));
  duplicates.add(FeatureEdgeRecord::from(FeatureEdge::local(featureName("fast"_zc)), origin()));
  ZC_EXPECT(FeatureManifest::from(featureName("default"_zc), zc::mv(duplicates)) == zc::none);
}

ZC_TEST("ManifestModel.BuildScriptSeparatesOriginAndSortsCapabilities") {
  auto target = TargetManifest::from(identity::CrateTargetKind::BuildScript, targetName("build"_zc),
                                     relativePath("tools"_zc, "build.zom"_zc), false, origin());
  ZC_REQUIRE(target != zc::none);
  zc::Vector<identity::CanonicalRelativePath> inputs;
  inputs.add(relativePath("src"_zc, "z.zom"_zc));
  inputs.add(relativePath("src"_zc, "a.zom"_zc));
  zc::Vector<identity::CanonicalRelativePath> outputs;
  outputs.add(relativePath("generated"_zc, "out.zom"_zc));
  zc::Vector<identity::SemanticEnvironmentName> environment;
  environment.add(environmentName("ZOM_TARGET"_zc));
  environment.add(environmentName("HOME"_zc));
  zc::Vector<identity::SemanticEnvironmentName> exported;
  exported.add(environmentName("GENERATED_MODE"_zc));
  ZC_IF_SOME(targetValue, target) {
    auto manifest = BuildScriptManifest::from(zc::mv(targetValue), zc::mv(inputs), zc::mv(outputs),
                                              zc::mv(environment), zc::mv(exported));
    ZC_REQUIRE(manifest != zc::none);
    ZC_IF_SOME(admitted, manifest) {
      ZC_REQUIRE(admitted.inputs().size() == 3);
      ZC_EXPECT(admitted.inputs()[0].segments()[1].text() == "a.zom"_zc);
      ZC_EXPECT(admitted.inputs()[1].segments()[1].text() == "z.zom"_zc);
      ZC_EXPECT(admitted.inputs()[2].segments()[1].text() == "build.zom"_zc);
      ZC_REQUIRE(admitted.environment().size() == 2);
      ZC_EXPECT(admitted.environment()[0].text() == "HOME"_zc);
      ZC_EXPECT(admitted.environment()[1].text() == "ZOM_TARGET"_zc);
      auto canonical = CanonicalBuildScriptManifest::from(admitted);
      auto encoded = canonical.encode();
      ZC_EXPECT(encoded.size() == 279);
      expectDigest(encoded.asPtr(),
                   "749f28943e0ce43ecd7226182c94dd13d534a55db58a6a1795e6229da5a94f55"_zc);
    }
  }

  zc::Vector<identity::CanonicalRelativePath> duplicateInputs;
  duplicateInputs.add(relativePath("src"_zc, "a.zom"_zc));
  duplicateInputs.add(relativePath("src"_zc, "a.zom"_zc));
  zc::Vector<identity::CanonicalRelativePath> noOutputs;
  zc::Vector<identity::SemanticEnvironmentName> noEnvironment;
  zc::Vector<identity::SemanticEnvironmentName> noExports;
  auto duplicateTarget =
      TargetManifest::from(identity::CrateTargetKind::BuildScript, targetName("build"_zc),
                           relativePath("tools"_zc, "build.zom"_zc), false, origin());
  ZC_IF_SOME(targetValue, duplicateTarget) {
    ZC_EXPECT(BuildScriptManifest::from(zc::mv(targetValue), zc::mv(duplicateInputs),
                                        zc::mv(noOutputs), zc::mv(noEnvironment),
                                        zc::mv(noExports)) == zc::none);
  }
}

}  // namespace zomlang::compiler::driver::package
