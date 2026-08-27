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

#include "compiler/driver/package/build-script-plan.h"

#include "zc/ztest/test.h"
#include "compiler/driver/package/manifest-parser.h"

namespace zomlang::compiler::driver::package {
namespace {

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto result = Scalar::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid build-plan scalar fixture");
}

identity::SortedFeatureSet emptyFeatures() {
  zc::Vector<identity::FeatureName> values;
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("empty build-plan feature set was rejected");
}

identity::PackageKey package(zc::StringPtr name) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  auto source = identity::CanonicalPackageSource::localPath(
      identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments)));
  return identity::PackageKey::from(zc::mv(source), scalar<identity::PackageName>(name),
                                    scalar<identity::ResolvedVersion>("1.0.0"_zc), emptyFeatures());
}

identity::CanonicalTargetSpecificationKey projection() {
  zc::Vector<identity::TargetFeatureName> features;
  auto sorted = identity::SortedTargetFeatureSet::from(zc::mv(features));
  ZC_REQUIRE(sorted != zc::none);
  ZC_IF_SOME(values, sorted) {
    auto result = identity::CanonicalTargetSpecificationKey::from(
        scalar<identity::TargetComponentName>("x86_64"_zc),
        scalar<identity::TargetComponentName>("zom"_zc),
        scalar<identity::TargetComponentName>("none"_zc),
        scalar<identity::TargetComponentName>("unknown"_zc),
        scalar<identity::TargetComponentName>("zom"_zc), 64, identity::Endianness::Little,
        zc::mv(values));
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("build-plan target projection was rejected");
}

identity::PreparatoryBuildScriptKey preparatory(zc::StringPtr name) {
  zc::Vector<identity::PackageKey> dependencies;
  auto result = identity::PreparatoryBuildScriptKey::from(
      package(name), scalar<identity::TargetName>("build"_zc), projection(),
      identity::SemanticCompilerOptionsKey::from(2026, true, false, true), zc::mv(dependencies));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("build-plan preparatory key was rejected");
}

identity::CanonicalRelativePath path(zc::StringPtr first, zc::StringPtr second) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(scalar<identity::CanonicalPathSegment>(first));
  segments.add(scalar<identity::CanonicalPathSegment>(second));
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

CanonicalBuildScriptManifest contract() {
  zc::Vector<identity::CanonicalRelativePath> files;
  files.add(path("tools"_zc, "build.zom"_zc));
  files.add(path("data"_zc, "input.txt"_zc));
  auto inventory = PackageSourceInventory::from(zc::mv(files));
  ZC_REQUIRE(inventory != zc::none);
  ZC_IF_SOME(sourceInventory, inventory) {
    zc::Vector<identity::CanonicalPathSegment> documentSegments;
    documentSegments.add(scalar<identity::CanonicalPathSegment>("Zom.toml"_zc));
    ManifestParser parser;
    auto parsed = parser.parseWorkspaceManifest(
        identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(documentSegments)),
        R"toml([package]
name = "fixture"
version = "1.0.0"
edition = "2026"

[build]
path = "tools/build.zom"
inputs = ["data/input.txt"]
outputs = ["generated/out.zom"]
environment = ["HOME"]
exported-environment = ["MODE"]
)toml"_zc,
        sourceInventory);
    if (parsed.is<NormalizedManifest>()) {
      return CanonicalBuildScriptManifest::from(parsed.get<NormalizedManifest>().buildScript());
    }
  }
  ZC_FAIL_REQUIRE("build-plan contract was rejected");
}

BuildScriptPlanNodeKey key(zc::StringPtr name) {
  return BuildScriptPlanNodeKey::from(preparatory(name));
}

BuildScriptPlanNode node(zc::StringPtr name,
                         zc::Vector<BuildScriptPlanNodeKey>&& predecessors = {}) {
  auto result = BuildScriptPlanNode::from(key(name), contract(), zc::mv(predecessors));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("build-plan node was rejected");
}

}  // namespace

ZC_TEST("Build-script plan derives one canonical predecessor-first order") {
  zc::Vector<BuildScriptPlanNodeKey> appPredecessors;
  appPredecessors.add(key("codec"_zc));
  appPredecessors.add(key("math"_zc));
  zc::Vector<BuildScriptPlanNode> nodes;
  nodes.add(node("app"_zc, zc::mv(appPredecessors)));
  nodes.add(node("math"_zc));
  nodes.add(node("codec"_zc));
  auto plan = VerifiedBuildScriptPlan::from(zc::mv(nodes));
  ZC_REQUIRE(plan != zc::none);
  ZC_IF_SOME(value, plan) {
    ZC_REQUIRE(value.executionOrder().size() == 3);
    const auto& first = value.nodes()[value.executionOrder()[0]];
    const auto& second = value.nodes()[value.executionOrder()[1]];
    const auto& third = value.nodes()[value.executionOrder()[2]];
    ZC_EXPECT(first.key().encode().asPtr() < second.key().encode().asPtr());
    ZC_EXPECT(third.key().encode().asPtr() == key("app"_zc).encode().asPtr());
  }
}

ZC_TEST("Build-script plan rejects duplicate nodes predecessors dangling edges and cycles") {
  {
    zc::Vector<BuildScriptPlanNode> nodes;
    nodes.add(node("app"_zc));
    nodes.add(node("app"_zc));
    ZC_EXPECT(VerifiedBuildScriptPlan::from(zc::mv(nodes)) == zc::none);
  }
  {
    zc::Vector<BuildScriptPlanNodeKey> predecessors;
    predecessors.add(key("dep"_zc));
    predecessors.add(key("dep"_zc));
    ZC_EXPECT(BuildScriptPlanNode::from(key("app"_zc), contract(), zc::mv(predecessors)) ==
              zc::none);
  }
  {
    zc::Vector<BuildScriptPlanNodeKey> predecessors;
    predecessors.add(key("missing"_zc));
    zc::Vector<BuildScriptPlanNode> nodes;
    nodes.add(node("app"_zc, zc::mv(predecessors)));
    ZC_EXPECT(VerifiedBuildScriptPlan::from(zc::mv(nodes)) == zc::none);
  }
  {
    zc::Vector<BuildScriptPlanNodeKey> firstPredecessors;
    firstPredecessors.add(key("second"_zc));
    zc::Vector<BuildScriptPlanNodeKey> secondPredecessors;
    secondPredecessors.add(key("first"_zc));
    zc::Vector<BuildScriptPlanNode> nodes;
    nodes.add(node("first"_zc, zc::mv(firstPredecessors)));
    nodes.add(node("second"_zc, zc::mv(secondPredecessors)));
    ZC_EXPECT(VerifiedBuildScriptPlan::from(zc::mv(nodes)) == zc::none);
  }
}

}  // namespace zomlang::compiler::driver::package
