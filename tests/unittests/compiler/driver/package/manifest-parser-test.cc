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

#include "compiler/driver/package/manifest-parser.h"

#include "zc/core/encoding.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "compiler/identity/crypto/sha256.h"

namespace zomlang::compiler::driver::package {
namespace {

identity::CanonicalWorkspaceRelativePath manifestPath() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  auto segment = identity::CanonicalPathSegment::fromCanonical("Zom.toml"_zc);
  ZC_IF_SOME(value, segment) { segments.add(zc::mv(value)); }
  ZC_IREQUIRE(segments.size() == 1, "Zom.toml must be a canonical path segment");
  return identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
}

ManifestParseResult parse(zc::StringPtr source) {
  zc::Vector<identity::CanonicalRelativePath> files;
  auto inventory = PackageSourceInventory::from(zc::mv(files));
  ZC_IF_SOME(admitted, inventory) {
    ManifestParser parser;
    return parser.parseWorkspaceManifest(manifestPath(), source, admitted);
  }
  ZC_FAIL_REQUIRE("empty source inventory was rejected");
}

identity::CanonicalRelativePath sourcePath(zc::StringPtr text) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  size_t start = 0;
  for (size_t index = 0; index <= text.size(); ++index) {
    if (index < text.size() && text[index] != '/') { continue; }
    const zc::String segmentText = zc::heapString(text.slice(start, index));
    auto segment = identity::CanonicalPathSegment::fromCanonical(segmentText);
    ZC_IF_SOME(admitted, segment) { segments.add(zc::mv(admitted)); }
    start = index + 1;
  }
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

ManifestParseResult parseWithFiles(zc::StringPtr source, zc::ArrayPtr<const zc::StringPtr> paths) {
  zc::Vector<identity::CanonicalRelativePath> files;
  for (const auto path : paths) { files.add(sourcePath(path)); }
  auto inventory = PackageSourceInventory::from(zc::mv(files));
  ZC_IF_SOME(admitted, inventory) {
    ManifestParser parser;
    return parser.parseWorkspaceManifest(manifestPath(), source, admitted);
  }
  ZC_FAIL_REQUIRE("valid source inventory was rejected");
}

size_t offsetOf(zc::StringPtr source, zc::StringPtr needle) {
  ZC_REQUIRE(needle.size() != 0 && needle.size() <= source.size());
  for (size_t offset = 0; offset + needle.size() <= source.size(); ++offset) {
    if (source.slice(offset, offset + needle.size()) == needle) { return offset; }
  }
  ZC_FAIL_REQUIRE("expected manifest text was not found");
}

}  // namespace

ZC_TEST("ManifestParser.ParsesMinimalPackage") {
  auto result = parse(R"toml([package]
name = "geometry"
version = "0.1.0"
edition = "2026"
)toml"_zc);

  ZC_REQUIRE(result.is<NormalizedManifest>());
  const auto& manifest = result.get<NormalizedManifest>();
  ZC_EXPECT(manifest.hasPackage());
  ZC_EXPECT(!manifest.hasWorkspace());
  ZC_EXPECT(manifest.packageName() == "geometry"_zc);
  ZC_EXPECT(manifest.packageVersion() == "0.1.0"_zc);
  ZC_EXPECT(manifest.editionYear() == 2026);
  ZC_EXPECT(manifest.targetDependencies().size() == 0);
  ZC_EXPECT(manifest.developmentDependencies().size() == 0);
  ZC_EXPECT(manifest.buildDependencies().size() == 0);
  ZC_EXPECT(manifest.featureCount() == 0);
}

ZC_TEST("ManifestParser.RejectsEncodingAndTomlFailures") {
  auto bom = parse("\xef\xbb\xbf[package]\nname=\"p\"\nversion=\"1.0.0\"\nedition=\"2026\"\n"_zc);
  ZC_REQUIRE(bom.is<ManifestFailure>());
  ZC_EXPECT(bom.get<ManifestFailure>().issue() == ManifestIssue::ByteOrderMarkPresent);

  auto syntax = parse("[package\n"_zc);
  ZC_REQUIRE(syntax.is<ManifestFailure>());
  ZC_EXPECT(syntax.get<ManifestFailure>().issue() == ManifestIssue::TomlSyntax);

  auto invalidUtf8 = parse("[package]\nname = \"\xff\"\n"_zc);
  ZC_REQUIRE(invalidUtf8.is<ManifestFailure>());
  ZC_EXPECT(invalidUtf8.get<ManifestFailure>().issue() == ManifestIssue::InvalidUtf8);
}

ZC_TEST("ManifestParser.RejectsClosedSchemaViolations") {
  auto unknownTable = parse("[profile]\nrelease = true\n"_zc);
  ZC_REQUIRE(unknownTable.is<ManifestFailure>());
  ZC_EXPECT(unknownTable.get<ManifestFailure>().issue() == ManifestIssue::UnknownTable);

  auto unknownKey = parse(R"toml([package]
name = "geometry"
version = "0.1.0"
edition = "2026"
description = "not admitted"
)toml"_zc);
  ZC_REQUIRE(unknownKey.is<ManifestFailure>());
  ZC_EXPECT(unknownKey.get<ManifestFailure>().issue() == ManifestIssue::UnknownKey);
  ZC_EXPECT(unknownKey.get<ManifestFailure>().provenance().primary().manifestSpan().byteStart() ==
            63);

  auto wrongType = parse(R"toml([package]
name = 7
version = "0.1.0"
edition = "2026"
)toml"_zc);
  ZC_REQUIRE(wrongType.is<ManifestFailure>());
  ZC_EXPECT(wrongType.get<ManifestFailure>().issue() == ManifestIssue::WrongValueType);
}

ZC_TEST("ManifestParser.ValidatesRequiredPackageScalars") {
  auto missing = parse("[package]\nname = \"geometry\"\nedition = \"2026\"\n"_zc);
  ZC_REQUIRE(missing.is<ManifestFailure>());
  ZC_EXPECT(missing.get<ManifestFailure>().issue() == ManifestIssue::MissingRequiredKey);

  auto invalidName =
      parse("[package]\nname = \"bad/name\"\nversion = \"0.1.0\"\nedition = \"2026\"\n"_zc);
  ZC_REQUIRE(invalidName.is<ManifestFailure>());
  ZC_EXPECT(invalidName.get<ManifestFailure>().issue() == ManifestIssue::InvalidStrongScalar);

  auto edition =
      parse("[package]\nname = \"geometry\"\nversion = \"0.1.0\"\nedition = \"2025\"\n"_zc);
  ZC_REQUIRE(edition.is<ManifestFailure>());
  ZC_EXPECT(edition.get<ManifestFailure>().issue() == ManifestIssue::UnsupportedEdition);
}

ZC_TEST("ManifestParser.ValidatesWorkspaceAndKnownTables") {
  auto workspace = parse("[workspace]\nmembers = [\"packages/core\"]\n"_zc);
  ZC_REQUIRE(workspace.is<NormalizedManifest>());
  ZC_EXPECT(workspace.get<NormalizedManifest>().hasWorkspace());
  ZC_EXPECT(workspace.get<NormalizedManifest>().workspaceMemberCount() == 1);

  auto emptyWorkspace = parse("[workspace]\nmembers = []\n"_zc);
  ZC_REQUIRE(emptyWorkspace.is<ManifestFailure>());
  ZC_EXPECT(emptyWorkspace.get<ManifestFailure>().issue() == ManifestIssue::MissingRequiredKey);

  auto duplicateMember =
      parse("[workspace]\nmembers = [\"packages/core\", \"packages/core\"]\n"_zc);
  ZC_REQUIRE(duplicateMember.is<ManifestFailure>());
  ZC_EXPECT(duplicateMember.get<ManifestFailure>().issue() ==
            ManifestIssue::DuplicateCanonicalValue);

  auto unknownTargetKey = parse(R"toml([package]
name = "geometry"
version = "0.1.0"
edition = "2026"

[lib]
crate-type = "static"
)toml"_zc);
  ZC_REQUIRE(unknownTargetKey.is<ManifestFailure>());
  ZC_EXPECT(unknownTargetKey.get<ManifestFailure>().issue() == ManifestIssue::UnknownKey);
}

ZC_TEST("ManifestParser.ValidatesDependenciesAndFeatures") {
  auto valid = parse(R"toml([package]
name = "geometry"
version = "0.1.0"
edition = "2026"

[dependencies]
math = { path = "../math", features = ["fast"], optional = true }

[features]
default = ["dep:math", "math/fast"]
)toml"_zc);
  ZC_REQUIRE(valid.is<NormalizedManifest>());
  ZC_REQUIRE(valid.get<NormalizedManifest>().targetDependencies().size() == 1);
  const auto& dependency = valid.get<NormalizedManifest>().targetDependencies()[0].withoutOrigin();
  ZC_EXPECT(dependency.alias() == "math"_zc);
  ZC_EXPECT(dependency.requiredPackage() == "math"_zc);
  ZC_EXPECT(dependency.domain() == identity::DependencyDomain::Target);
  ZC_EXPECT(dependency.sourceKind() == PackageSourceConstraintKind::LocalPath);
  ZC_EXPECT(!dependency.hasVersionCheck());
  ZC_REQUIRE(dependency.requestedFeatures().size() == 1);
  ZC_EXPECT(dependency.requestedFeatures()[0].text() == "fast"_zc);
  ZC_EXPECT(dependency.useDefaultFeatures());
  ZC_EXPECT(dependency.optional());
  ZC_EXPECT(valid.get<NormalizedManifest>().featureCount() == 1);

  auto shorthand = parse(R"toml([package]
name = "geometry"
version = "0.1.0"
edition = "2026"

[dependencies]
math = "1.0.0"
)toml"_zc);
  ZC_REQUIRE(shorthand.is<ManifestFailure>());
  ZC_EXPECT(shorthand.get<ManifestFailure>().issue() == ManifestIssue::WrongValueType);

  auto unknownDependencyKey = parse(R"toml([package]
name = "geometry"
version = "0.1.0"
edition = "2026"

[dependencies]
math = { path = "../math", magic = true }
)toml"_zc);
  ZC_REQUIRE(unknownDependencyKey.is<ManifestFailure>());
  ZC_EXPECT(unknownDependencyKey.get<ManifestFailure>().issue() == ManifestIssue::UnknownKey);

  auto invalidFeature = parse(R"toml([package]
name = "geometry"
version = "0.1.0"
edition = "2026"

[features]
default = ["dep:"]
)toml"_zc);
  ZC_REQUIRE(invalidFeature.is<ManifestFailure>());
  ZC_EXPECT(invalidFeature.get<ManifestFailure>().issue() == ManifestIssue::InvalidFeatureEdge);

  auto emptyVersionIntersection = parse(R"toml([package]
name = "geometry"
version = "0.1.0"
edition = "2026"

[dependencies]
math = { path = "../math", version = ">2.0.0,<1.0.0" }
)toml"_zc);
  ZC_REQUIRE(emptyVersionIntersection.is<NormalizedManifest>());

  auto versionWithWhitespace = parse(R"toml([package]
name = "geometry"
version = "0.1.0"
edition = "2026"

[dependencies]
math = { path = "../math", version = ">= 1.0.0" }
)toml"_zc);
  ZC_REQUIRE(versionWithWhitespace.is<ManifestFailure>());
  ZC_EXPECT(versionWithWhitespace.get<ManifestFailure>().issue() ==
            ManifestIssue::InvalidVersionConstraint);
}

ZC_TEST("ManifestParser.ValidatesFeatureGraphSemantics") {
  auto requiredDependency = parse(R"toml([package]
name = "geometry"
version = "0.1.0"
edition = "2026"

[dependencies]
math = { path = "../math" }

[features]
default = ["dep:math"]
)toml"_zc);
  ZC_REQUIRE(requiredDependency.is<ManifestFailure>());
  ZC_EXPECT(requiredDependency.get<ManifestFailure>().issue() == ManifestIssue::InvalidFeatureEdge);

  auto unknownDependency = parse(R"toml([package]
name = "geometry"
version = "0.1.0"
edition = "2026"

[features]
default = ["missing/fast"]
)toml"_zc);
  ZC_REQUIRE(unknownDependency.is<ManifestFailure>());
  ZC_EXPECT(unknownDependency.get<ManifestFailure>().issue() == ManifestIssue::InvalidFeatureEdge);

  auto duplicateEdge = parse(R"toml([package]
name = "geometry"
version = "0.1.0"
edition = "2026"

[features]
default = ["fast", "fast"]
fast = []
)toml"_zc);
  ZC_REQUIRE(duplicateEdge.is<ManifestFailure>());
  ZC_EXPECT(duplicateEdge.get<ManifestFailure>().issue() == ManifestIssue::DuplicateCanonicalValue);

  auto cycle = parse(R"toml([package]
name = "geometry"
version = "0.1.0"
edition = "2026"

[features]
fast = ["simd"]
simd = ["fast"]
)toml"_zc);
  ZC_REQUIRE(cycle.is<ManifestFailure>());
  ZC_EXPECT(cycle.get<ManifestFailure>().issue() == ManifestIssue::FeatureCycle);
}

ZC_TEST("ManifestParser.RetainsSortedDependencyDomainsSourcesAndConstraints") {
  auto result = parse(R"toml([package]
name = "app"
version = "1.0.0"
edition = "2026"

[dependencies]
registry = { version = "^1.2.3", registry = "https://packages.example/index", trust-domain-sha256 = "0000000000000000000000000000000000000000000000000000000000000000" }
code = { git = "https://git.example/repo", rev = "0000000000000000000000000000000000000000", subdirectory = "packages/code", default-features = false }

[dev-dependencies]
fixtures = { path = "../fixtures", version = ">=2.0.0,<3.0.0" }

[build-dependencies]
build_tool = { path = "../generator" }
)toml"_zc);

  ZC_REQUIRE(result.is<NormalizedManifest>());
  const auto& manifest = result.get<NormalizedManifest>();
  ZC_REQUIRE(manifest.targetDependencies().size() == 2);
  ZC_EXPECT(manifest.targetDependencies()[0].withoutOrigin().alias() == "code"_zc);
  ZC_EXPECT(manifest.targetDependencies()[0].withoutOrigin().sourceKind() ==
            PackageSourceConstraintKind::Vcs);
  ZC_EXPECT(!manifest.targetDependencies()[0].withoutOrigin().useDefaultFeatures());
  ZC_EXPECT(manifest.targetDependencies()[1].withoutOrigin().alias() == "registry"_zc);
  ZC_EXPECT(manifest.targetDependencies()[1].withoutOrigin().sourceKind() ==
            PackageSourceConstraintKind::Registry);
  ZC_REQUIRE(manifest.targetDependencies()[1].withoutOrigin().hasVersionCheck());
  const auto& version = manifest.targetDependencies()[1].withoutOrigin().versionCheck();
  ZC_REQUIRE(version.intervals().size() == 1);
  ZC_EXPECT(version.intervals()[0].lower().version() == "1.2.3"_zc);
  ZC_EXPECT(version.intervals()[0].upper().version() == "2.0.0"_zc);

  ZC_REQUIRE(manifest.developmentDependencies().size() == 1);
  ZC_EXPECT(manifest.developmentDependencies()[0].withoutOrigin().domain() ==
            identity::DependencyDomain::Development);
  ZC_EXPECT(manifest.developmentDependencies()[0].withoutOrigin().sourceKind() ==
            PackageSourceConstraintKind::LocalPath);
  ZC_REQUIRE(manifest.buildDependencies().size() == 1);
  ZC_EXPECT(manifest.buildDependencies()[0].withoutOrigin().domain() ==
            identity::DependencyDomain::Build);
}

ZC_TEST("ManifestParser.RetainsExplicitTargetsAndBuildContract") {
  const zc::StringPtr files[] = {
      "src/lib.zom"_zc,
      "src/bin/zeta.zom"_zc,
      "cmd/alpha.zom"_zc,
      "tests/integration.zom"_zc,
      "benches/throughput.zom"_zc,
      "examples/hello.zom"_zc,
      "tools/build.zom"_zc,
      "data/z.txt"_zc,
      "data/a.txt"_zc,
  };
  auto result = parseWithFiles(R"toml([package]
name = "app"
version = "1.0.0"
edition = "2026"

[lib]

[[bin]]
name = "zeta"

[[bin]]
name = "alpha"
path = "cmd/alpha.zom"

[[test]]
name = "integration"

[[bench]]
name = "throughput"

[[example]]
name = "hello"

[build]
path = "tools/build.zom"
inputs = ["data/z.txt", "data/a.txt"]
outputs = ["generated/output.zom"]
environment = ["ZOM_TARGET", "HOME"]
exported-environment = ["GENERATED_MODE"]
)toml"_zc,
                               zc::arrayPtr(files));

  ZC_REQUIRE(result.is<NormalizedManifest>());
  const auto& manifest = result.get<NormalizedManifest>();
  ZC_REQUIRE(manifest.hasLibrary());
  ZC_EXPECT(manifest.library().name() == "app"_zc);
  ZC_EXPECT(manifest.library().path().segments()[1].text() == "lib.zom"_zc);
  ZC_REQUIRE(manifest.binaries().size() == 2);
  ZC_EXPECT(manifest.binaries()[0].name() == "zeta"_zc);
  ZC_EXPECT(manifest.binaries()[1].name() == "alpha"_zc);
  ZC_EXPECT(manifest.binaries()[0].path().segments()[2].text() == "zeta.zom"_zc);
  ZC_EXPECT(manifest.binaries()[1].path().segments()[1].text() == "alpha.zom"_zc);
  ZC_EXPECT(manifest.tests().size() == 1);
  ZC_EXPECT(manifest.benchmarks().size() == 1);
  ZC_EXPECT(manifest.examples().size() == 1);
  ZC_REQUIRE(manifest.hasBuildScript());
  const auto& build = manifest.buildScript();
  ZC_EXPECT(build.target().name() == "build"_zc);
  ZC_REQUIRE(build.inputs().size() == 3);
  ZC_EXPECT(build.inputs()[0].segments()[1].text() == "a.txt"_zc);
  ZC_EXPECT(build.inputs()[1].segments()[1].text() == "z.txt"_zc);
  ZC_EXPECT(build.inputs()[2].segments()[1].text() == "build.zom"_zc);
  ZC_REQUIRE(build.environment().size() == 2);
  ZC_EXPECT(build.environment()[0].text() == "HOME"_zc);
  ZC_EXPECT(build.environment()[1].text() == "ZOM_TARGET"_zc);
}

ZC_TEST("ManifestParser.RetainsExactTargetAndDependencyAliasOrigins") {
  const auto source = R"toml([package]
name = "app"
version = "1.0.0"
edition = "2026"

[[bin]]
name = "core"
path = "src/bin/core.zom"

[dependencies]
core = { package = "provider", path = "../provider" }
)toml"_zc;
  const zc::StringPtr files[] = {"src/bin/core.zom"_zc};
  auto result = parseWithFiles(source, zc::arrayPtr(files));
  ZC_REQUIRE(result.is<NormalizedManifest>());
  const auto& manifest = result.get<NormalizedManifest>();
  ZC_REQUIRE(manifest.binaries().size() == 1);
  const auto& targetSpan = manifest.binaries()[0].origin().manifestSpan();
  const auto targetHeader = "[[bin]]"_zc;
  const auto targetStart = offsetOf(source, targetHeader);
  ZC_EXPECT(targetSpan.byteStart() == targetStart);
  ZC_EXPECT(targetSpan.byteEnd() == targetStart + targetHeader.size());

  ZC_REQUIRE(manifest.targetDependencies().size() == 1);
  const auto& dependencySpan = manifest.targetDependencies()[0].origin().manifestSpan();
  const auto aliasStart = offsetOf(source, "core = { package"_zc);
  ZC_EXPECT(dependencySpan.byteStart() == aliasStart);
  ZC_EXPECT(dependencySpan.byteEnd() == aliasStart + "core"_zc.size());
}

ZC_TEST("ManifestParser.RejectsInvalidBuildCapabilities") {
  auto duplicateInput = parse(R"toml([package]
name = "app"
version = "1.0.0"
edition = "2026"

[build]
path = "tools/build.zom"
inputs = ["data/a.txt", "data/a.txt"]
outputs = ["generated/output.zom"]
)toml"_zc);
  ZC_REQUIRE(duplicateInput.is<ManifestFailure>());
  ZC_EXPECT(duplicateInput.get<ManifestFailure>().issue() ==
            ManifestIssue::DuplicateCanonicalValue);

  auto invalidEnvironment = parse(R"toml([package]
name = "app"
version = "1.0.0"
edition = "2026"

[build]
path = "tools/build.zom"
inputs = []
outputs = ["generated/output.zom"]
environment = ["bad-name"]
)toml"_zc);
  ZC_REQUIRE(invalidEnvironment.is<ManifestFailure>());
  ZC_EXPECT(invalidEnvironment.get<ManifestFailure>().issue() ==
            ManifestIssue::InvalidStrongScalar);
}

ZC_TEST("ManifestParser.CanonicalRecordPassesFixedMinimalCodecVector") {
  auto result = parse(R"toml([package]
name = "a"
version = "0.0.0"
edition = "2026"
)toml"_zc);
  ZC_REQUIRE(result.is<NormalizedManifest>());
  auto canonical = CanonicalManifestRecord::from(result.get<NormalizedManifest>());
  const uint8_t expected[] = {
      1, 0,    0,    0, 0, 0, 0, 0, 1, 'a', 0, 0, 0, 0, 0, 0, 0, 5, '0', '.', '0', '.', '0', 0,
      0, 0x07, 0xea, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0, 0,   0,   0,   0,   0,   0,
      0, 0,    0,    0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0, 0,   0,   0,   0,   0,   0,
      0, 0,    0,    0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0, 0,   0,   0,   0,
  };
  auto encoded = canonical.encode();
  ZC_EXPECT(encoded.size() == 94);
  ZC_EXPECT(encoded.asPtr() == zc::arrayPtr(expected));
  auto digest = identity::sha256(encoded.asPtr());
  ZC_REQUIRE(digest != zc::none);
  ZC_IF_SOME(admitted, digest) {
    ZC_EXPECT(zc::encodeHex(admitted.bytes()) ==
              "8e5e2a3e3841ca90015cbc321ed40536c260868a8a8c691bc241ec64c6e6fd30"_zc);
  }
}

ZC_TEST("ManifestParser.CanonicalRecordIsIndependentOfSourceAndInputOrder") {
  const zc::StringPtr files[] = {"src/bin/long_name.zom"_zc, "src/bin/x.zom"_zc};
  auto first = parseWithFiles(R"toml([package]
name = "app"
version = "1.0.0"
edition = "2026"

[[bin]]
name = "long_name"

[[bin]]
name = "x"

[dependencies]
long_dep = { path = "../long_dep" }
x = { path = "../x" }

[features]
long_feature = []
x = []
)toml"_zc,
                              zc::arrayPtr(files));
  auto second = parseWithFiles(R"toml([package]
edition = "2026"
version = "1.0.0"
name = "app"

[[bin]]
name = "x"

[[bin]]
name = "long_name"

[dependencies]
x = { path = "../x" }
long_dep = { path = "../long_dep" }

[features]
x = []
long_feature = []
)toml"_zc,
                               zc::arrayPtr(files));
  ZC_REQUIRE(first.is<NormalizedManifest>());
  ZC_REQUIRE(second.is<NormalizedManifest>());
  auto firstCanonical = CanonicalManifestRecord::from(first.get<NormalizedManifest>());
  auto secondCanonical = CanonicalManifestRecord::from(second.get<NormalizedManifest>());
  ZC_EXPECT(firstCanonical.encode().asPtr() == secondCanonical.encode().asPtr());
}

ZC_TEST("ManifestParser.DerivesImplicitTargetsFromRegularFileInventory") {
  const zc::StringPtr files[] = {"src/main.zom"_zc, "src/lib.zom"_zc};
  auto result = parseWithFiles(R"toml([package]
name = "app"
version = "1.0.0"
edition = "2026"
)toml"_zc,
                               zc::arrayPtr(files));
  ZC_REQUIRE(result.is<NormalizedManifest>());
  const auto& manifest = result.get<NormalizedManifest>();
  ZC_REQUIRE(manifest.hasLibrary());
  ZC_EXPECT(manifest.library().implicit());
  ZC_EXPECT(manifest.library().name() == "app"_zc);
  ZC_REQUIRE(manifest.binaries().size() == 1);
  ZC_EXPECT(manifest.binaries()[0].implicit());
  ZC_EXPECT(manifest.binaries()[0].name() == "app"_zc);
}

ZC_TEST("ManifestParser.RejectsMissingAndCollidingTargetInventory") {
  auto missing = parse(R"toml([package]
name = "app"
version = "1.0.0"
edition = "2026"

[[bin]]
name = "app"
path = "cmd/app.zom"
)toml"_zc);
  ZC_REQUIRE(missing.is<ManifestFailure>());
  ZC_EXPECT(missing.get<ManifestFailure>().issue() == ManifestIssue::MissingTargetPath);

  const zc::StringPtr pathCollisionFiles[] = {"shared/root.zom"_zc};
  auto pathCollision = parseWithFiles(R"toml([package]
name = "app"
version = "1.0.0"
edition = "2026"

[lib]
path = "shared/root.zom"

[[bin]]
name = "app"
path = "shared/root.zom"
)toml"_zc,
                                      zc::arrayPtr(pathCollisionFiles));
  ZC_REQUIRE(pathCollision.is<ManifestFailure>());
  ZC_EXPECT(pathCollision.get<ManifestFailure>().issue() == ManifestIssue::TargetPathCollision);

  const zc::StringPtr nameCollisionFiles[] = {"cmd/first.zom"_zc, "cmd/second.zom"_zc};
  auto nameCollision = parseWithFiles(R"toml([package]
name = "app"
version = "1.0.0"
edition = "2026"

[[bin]]
name = "app"
path = "cmd/first.zom"

[[bin]]
name = "app"
path = "cmd/second.zom"
)toml"_zc,
                                      zc::arrayPtr(nameCollisionFiles));
  ZC_REQUIRE(nameCollision.is<ManifestFailure>());
  ZC_EXPECT(nameCollision.get<ManifestFailure>().issue() == ManifestIssue::TargetCollision);
}

}  // namespace zomlang::compiler::driver::package
