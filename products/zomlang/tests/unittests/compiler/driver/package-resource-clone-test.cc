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

#include "zc/ztest/test.h"
#include "zomlang/compiler/driver/package/manifest-parser.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/sha256.h"

namespace zomlang::compiler::driver::package {
namespace {

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto result = Scalar::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid canonical scalar fixture");
}

identity::ResolvedVersion version(zc::StringPtr text) {
  auto result = identity::ResolvedVersion::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid version fixture");
}

identity::CanonicalUrl url(zc::StringPtr text) {
  auto result = identity::CanonicalUrl::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid URL fixture");
}

identity::CanonicalRelativePath relativePath(zc::StringPtr first, zc::StringPtr second) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(scalar<identity::CanonicalPathSegment>(first));
  segments.add(scalar<identity::CanonicalPathSegment>(second));
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

identity::CanonicalWorkspaceRelativePath workspacePath(zc::StringPtr text) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(scalar<identity::CanonicalPathSegment>(text));
  return identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
}

identity::Sha256Digest digest(zc::StringPtr text) {
  auto result = identity::sha256(text.asBytes());
  ZC_IF_SOME(value, result) { return value; }
  ZC_FAIL_REQUIRE("digest fixture failed");
}

identity::SortedFeatureSet features(zc::StringPtr text = {}) {
  zc::Vector<identity::FeatureName> values;
  if (text.size() != 0) { values.add(scalar<identity::FeatureName>(text)); }
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("feature-set fixture failed");
}

DiagnosticAnchor origin() {
  auto document = InputDocumentKey::from(
      InputDocumentKind::Manifest, DiagnosticDocumentPath::workspace(workspacePath("Zom.toml"_zc)),
      digest("manifest"_zc));
  ZC_IF_SOME(documentValue, document) {
    auto span = ManifestSpan::from(zc::mv(documentValue), 64, 8, 16);
    ZC_IF_SOME(spanValue, span) { return DiagnosticAnchor::manifest(zc::mv(spanValue)); }
  }
  ZC_FAIL_REQUIRE("diagnostic origin fixture failed");
}

zc::Array<uint8_t> encode(const PackageSourceConstraint& source) {
  identity::CanonicalEncoder encoder;
  source.encode(encoder);
  return encoder.finish();
}

PackageSourceConstraint registrySource() {
  return PackageSourceConstraint::registry(identity::RegistryIdentity::from(
      url("https://example.com/index"_zc), digest("registry trust"_zc)));
}

PackageSourceConstraint vcsSource(VcsSelector&& selector) {
  return PackageSourceConstraint::vcs(url("https://example.com/repository.git"_zc),
                                      zc::mv(selector), relativePath("packages"_zc, "codec"_zc));
}

PackageSourceConstraint localSource() {
  return PackageSourceConstraint::localPath(workspacePath("local-codec"_zc));
}

VcsSelector vcsRevision() {
  uint8_t bytes[20];
  for (size_t index = 0; index < 20; ++index) { bytes[index] = static_cast<uint8_t>(index); }
  auto result =
      identity::VcsRevision::from(identity::VcsRevisionAlgorithm::Sha1, zc::arrayPtr(bytes));
  ZC_IF_SOME(value, result) { return VcsSelector::revision(zc::mv(value)); }
  ZC_FAIL_REQUIRE("VCS revision fixture failed");
}

VcsSelector vcsTag() {
  auto result = VcsSelector::tag("v1.2.3"_zc);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("VCS tag fixture failed");
}

VcsSelector vcsBranch() {
  auto result = VcsSelector::branch("release"_zc);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("VCS branch fixture failed");
}

class FailNthAllocationResource final : public zc::MemoryResource {
public:
  FailNthAllocationResource(zc::MemoryResource& upstream, size_t failureIndex)
      : upstream(upstream), failureIndex(failureIndex) {}

protected:
  void* doAllocate(size_t size, size_t alignment) override {
    ++allocationCount;
    ZC_REQUIRE(allocationCount != failureIndex, "injected manifest clone allocation failure");
    return upstream.allocate(size, alignment);
  }

  void doDeallocate(void* pointer, size_t size, size_t alignment) override {
    upstream.deallocate(pointer, size, alignment);
  }

private:
  zc::MemoryResource& upstream;
  size_t failureIndex;
  size_t allocationCount = 0;
};

DependencyRequirementWithoutOrigin requirement(
    zc::StringPtr alias, zc::StringPtr package, identity::DependencyDomain domain,
    PackageSourceConstraint&& source, zc::Maybe<SemVerConstraint>&& constraint = zc::none) {
  auto result = DependencyRequirementWithoutOrigin::from(
      scalar<identity::DependencyAlias>(alias), scalar<identity::PackageName>(package), domain,
      zc::mv(source), zc::mv(constraint), features("simd"_zc), true, false);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("dependency requirement fixture failed");
}

CanonicalFeatureManifest featureManifest() {
  zc::Vector<FeatureEdgeRecord> edges;
  edges.add(FeatureEdgeRecord::from(FeatureEdge::local(scalar<identity::FeatureName>("fast"_zc)),
                                    origin()));
  edges.add(FeatureEdgeRecord::from(
      FeatureEdge::enableDependency(scalar<identity::DependencyAlias>("registry_dep"_zc)),
      origin()));
  edges.add(FeatureEdgeRecord::from(
      FeatureEdge::enableDependencyFeature(scalar<identity::DependencyAlias>("vcs_dep"_zc),
                                           scalar<identity::FeatureName>("serde"_zc)),
      origin()));
  auto result = FeatureManifest::from(scalar<identity::FeatureName>("default"_zc), zc::mv(edges));
  ZC_IF_SOME(value, result) { return CanonicalFeatureManifest::from(value); }
  ZC_FAIL_REQUIRE("canonical feature fixture failed");
}

CanonicalManifestRecord manifestRecord() {
  auto library = CanonicalTargetManifest::from(identity::CrateTargetKind::Library,
                                               scalar<identity::TargetName>("codec"_zc),
                                               relativePath("src"_zc, "lib.zom"_zc), false);
  auto constraint = SemVerConstraint::parse(">=1.2.3-alpha,<2.0.0"_zc);
  ZC_REQUIRE(library != zc::none);
  ZC_REQUIRE(constraint != zc::none);

  zc::Vector<DependencyRequirementWithoutOrigin> targetDependencies;
  zc::Vector<DependencyRequirementWithoutOrigin> developmentDependencies;
  zc::Vector<DependencyRequirementWithoutOrigin> buildDependencies;
  ZC_IF_SOME(value, constraint) {
    targetDependencies.add(requirement("registry_dep"_zc, "registry_pkg"_zc,
                                       identity::DependencyDomain::Target, registrySource(),
                                       zc::mv(value)));
  }
  developmentDependencies.add(requirement("vcs_dep"_zc, "vcs_pkg"_zc,
                                          identity::DependencyDomain::Development,
                                          vcsSource(vcsRevision())));
  buildDependencies.add(requirement("local_dep"_zc, "local_pkg"_zc,
                                    identity::DependencyDomain::Build, localSource()));
  zc::Vector<CanonicalFeatureManifest> featureValues;
  featureValues.add(featureManifest());
  return CanonicalManifestRecord::forResolver(
      PackageManifest::from(scalar<identity::PackageName>("codec"_zc), version("1.2.3"_zc), 2026),
      zc::mv(library), zc::mv(targetDependencies), zc::mv(developmentDependencies),
      zc::mv(buildDependencies), zc::mv(featureValues));
}

}  // namespace

ZC_TEST("PackageResourceClone.PreservesEverySourceSelectorAndReleasesStorage") {
  zc::Vector<PackageSourceConstraint> sources;
  sources.add(registrySource());
  sources.add(vcsSource(vcsRevision()));
  sources.add(vcsSource(vcsTag()));
  sources.add(vcsSource(vcsBranch()));
  sources.add(localSource());

  zc::MemoryResource upstream;
  zc::CountingMemoryResource resource(upstream);
  {
    zc::Vector<PackageSourceConstraint> clones(resource, sources.size());
    for (const auto& source : sources) {
      clones.add(source.clone(resource));
      ZC_EXPECT(encode(source).asPtr() == encode(clones.back()).asPtr());
    }
    ZC_EXPECT(resource.currentAllocatedBytes() > 0);
  }
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);
  ZC_EXPECT(resource.peakAllocatedBytes() > 0);
}

ZC_TEST("PackageResourceClone.PreservesSemVerIntervalsAndPrereleaseCores") {
  auto source = SemVerConstraint::parse(">=1.2.3-alpha,<2.0.0"_zc);
  ZC_REQUIRE(source != zc::none);
  zc::MemoryResource upstream;
  zc::CountingMemoryResource resource(upstream);
  ZC_IF_SOME(value, source) {
    auto expected = value.encode();
    {
      auto clone = value.clone(resource);
      ZC_EXPECT(clone.encode().asPtr() == expected.asPtr());
      ZC_EXPECT(resource.currentAllocatedBytes() > 0);
    }
  }
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);
}

ZC_TEST("PackageResourceClone.PreservesCompleteManifestAcrossMoveAndDestruction") {
  auto source = manifestRecord();
  auto expected = source.encode();
  zc::MemoryResource upstream;
  zc::CountingMemoryResource resource(upstream);
  {
    auto clone = source.clone(resource);
    auto moved = zc::mv(clone);
    ZC_EXPECT(moved.encode().asPtr() == expected.asPtr());
    ZC_REQUIRE(moved.hasLibrary());
    ZC_EXPECT(moved.targetDependencies().size() == 1);
    ZC_EXPECT(moved.developmentDependencies().size() == 1);
    ZC_EXPECT(moved.buildDependencies().size() == 1);
    ZC_EXPECT(moved.features().size() == 1);
    ZC_EXPECT(moved.targetDependencies()[0].sourceKind() == PackageSourceConstraintKind::Registry);
    ZC_EXPECT(moved.developmentDependencies()[0].sourceKind() == PackageSourceConstraintKind::Vcs);
    ZC_EXPECT(moved.buildDependencies()[0].sourceKind() == PackageSourceConstraintKind::LocalPath);
    ZC_EXPECT(resource.currentAllocatedBytes() > 0);
  }
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);
  ZC_EXPECT(resource.peakAllocatedBytes() > 0);
}

ZC_TEST("PackageResourceClone.RollbackReturnsAllStorageAfterNestedAllocationFailure") {
  auto source = manifestRecord();
  zc::MemoryResource upstream;
  FailNthAllocationResource failing(upstream, 8);
  zc::CountingMemoryResource resource(failing);
  bool threw = false;
  try {
    auto clone = source.clone(resource);
    (void)clone;
  } catch (...) { threw = true; }
  ZC_EXPECT(threw);
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);
  ZC_EXPECT(resource.peakAllocatedBytes() > 0);
}

}  // namespace zomlang::compiler::driver::package
