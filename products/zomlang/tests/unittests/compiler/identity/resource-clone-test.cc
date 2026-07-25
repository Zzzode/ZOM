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

#include "zc/core/memory.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/package-key.h"

namespace zomlang::compiler::identity {
namespace {

template <typename Scalar>
Scalar requireScalar(zc::StringPtr text) {
  auto value = Scalar::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid canonical scalar resource-clone input");
}

CanonicalUrl requireUrl(zc::StringPtr text) {
  auto value = CanonicalUrl::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid canonical URL resource-clone input");
}

ResolvedVersion requireVersion(zc::StringPtr text) {
  auto value = ResolvedVersion::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid canonical version resource-clone input");
}

template <typename Scalar>
void expectScalarClone(zc::MemoryResource& resource, zc::StringPtr text) {
  auto value = requireScalar<Scalar>(text);
  auto clone = value.clone(resource);
  auto moved = zc::mv(clone);
  ZC_EXPECT(moved == value);
}

CanonicalRelativePath relativePath(zc::StringPtr first, zc::StringPtr second) {
  zc::Vector<CanonicalPathSegment> segments;
  segments.add(requireScalar<CanonicalPathSegment>(first));
  segments.add(requireScalar<CanonicalPathSegment>(second));
  return CanonicalRelativePath::from(zc::mv(segments));
}

CanonicalWorkspaceRelativePath workspacePath(uint32_t parents, zc::StringPtr first,
                                             zc::StringPtr second) {
  zc::Vector<CanonicalPathSegment> segments;
  segments.add(requireScalar<CanonicalPathSegment>(first));
  segments.add(requireScalar<CanonicalPathSegment>(second));
  return CanonicalWorkspaceRelativePath::from(parents, zc::mv(segments));
}

SortedFeatureSet features() {
  zc::Vector<FeatureName> values;
  values.add(requireScalar<FeatureName>("simd-wide"_zc));
  values.add(requireScalar<FeatureName>("alloc"_zc));
  auto result = SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(admitted, result) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid resource-clone features were rejected");
}

VcsRevision revision() {
  uint8_t bytes[32];
  for (size_t index = 0; index < zc::size(bytes); ++index) {
    bytes[index] = static_cast<uint8_t>(index);
  }
  auto result = VcsRevision::from(VcsRevisionAlgorithm::Sha256, zc::arrayPtr(bytes));
  ZC_IF_SOME(admitted, result) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid resource-clone VCS revision was rejected");
}

PackageKey package(CanonicalPackageSource&& source, zc::StringPtr name) {
  return PackageKey::from(zc::mv(source), requireScalar<PackageName>(name),
                          requireVersion("1.2.3-rc.1+build.7"_zc), features());
}

void expectSourceClone(zc::MemoryResource& resource, const CanonicalPackageSource& source) {
  auto expected = PackageBaseKey::from(source.clone(), requireScalar<PackageName>("source"_zc),
                                       requireVersion("1.0.0"_zc));
  auto clone = expected.clone(resource);
  ZC_EXPECT(clone.encode().asPtr() == expected.encode().asPtr());
}

}  // namespace

ZC_TEST("Canonical identity leaves clone through an explicit memory resource") {
  zc::MemoryResource upstream;
  zc::CountingMemoryResource resource(upstream);
  {
    expectScalarClone<CanonicalPathSegment>(resource, "src"_zc);
    expectScalarClone<PackageName>(resource, "package"_zc);
    expectScalarClone<TargetName>(resource, "target"_zc);
    expectScalarClone<DependencyAlias>(resource, "dependency"_zc);
    expectScalarClone<FeatureName>(resource, "feature-secondary"_zc);
    expectScalarClone<TargetComponentName>(resource, "x86_64-modern"_zc);
    expectScalarClone<TargetFeatureName>(resource, "avx2.0"_zc);
    expectScalarClone<SemanticEnvironmentName>(resource, "ZOM_TARGET"_zc);
    expectScalarClone<SemanticIdentifier>(resource, "semanticValue"_zc);
    expectScalarClone<ModulePathSegment>(resource, "moduleName"_zc);
    expectScalarClone<DeclaredDefinitionName>(resource, "init"_zc);

    auto version = requireVersion("2.0.0-alpha.1+build.3"_zc);
    auto versionClone = version.clone(resource);
    ZC_EXPECT(versionClone == version);
    auto url = requireUrl("https://example.com/registry/index"_zc);
    auto urlClone = url.clone(resource);
    ZC_EXPECT(urlClone == url);
  }
  ZC_EXPECT(resource.peakAllocatedBytes() > 0);
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);
}

ZC_TEST("Canonical identity admission owns accepted bytes through the explicit resource") {
  zc::MemoryResource upstream;
  zc::CountingMemoryResource resource(upstream);
  {
    auto package = PackageName::fromCanonical(resource, "package_01"_zc);
    auto feature = FeatureName::fromCanonical(resource, "simd-wide"_zc);
    auto version = ResolvedVersion::fromCanonical(resource, "2.3.4-rc.1+build.7"_zc);
    ZC_REQUIRE(package != zc::none);
    ZC_REQUIRE(feature != zc::none);
    ZC_REQUIRE(version != zc::none);
    ZC_IF_SOME(value, package) { ZC_EXPECT(value.text() == "package_01"_zc); }
    ZC_IF_SOME(value, feature) { ZC_EXPECT(value.text() == "simd-wide"_zc); }
    ZC_IF_SOME(value, version) { ZC_EXPECT(value.text() == "2.3.4-rc.1+build.7"_zc); }
    ZC_EXPECT(resource.currentAllocatedBytes() > 0);
  }
  ZC_EXPECT(resource.peakAllocatedBytes() > 0);
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);

  const size_t peakBeforeInvalid = resource.peakAllocatedBytes();
  ZC_EXPECT(PackageName::fromCanonical(resource, "Package"_zc) == zc::none);
  ZC_EXPECT(ResolvedVersion::fromCanonical(resource, "01.0.0"_zc) == zc::none);
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);
  ZC_EXPECT(resource.peakAllocatedBytes() == peakBeforeInvalid);
}

ZC_TEST("Canonical Unicode admission keeps normalization storage in the explicit resource") {
  zc::MemoryResource upstream;
  zc::CountingMemoryResource resource(upstream);
  {
    auto identifier = SemanticIdentifier::fromCanonical(resource, "caf\xC3\xA9"_zc);
    ZC_REQUIRE(identifier != zc::none);
    ZC_IF_SOME(value, identifier) { ZC_EXPECT(value.text() == "caf\xC3\xA9"_zc); }
    ZC_EXPECT(resource.currentAllocatedBytes() > 0);
  }
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);

  ZC_EXPECT(SemanticIdentifier::fromCanonical(resource, "caf\x65\xCC\x81"_zc) == zc::none);
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);
  ZC_EXPECT(SemanticIdentifier::fromCanonical(resource, "\xC0\x80"_zc) == zc::none);
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);
}

ZC_TEST("Package identity composites clone every source variant through one resource") {
  Sha256Digest trust;
  auto registrySource = CanonicalPackageSource::registry(
      RegistryIdentity::from(requireUrl("https://registry.example/index"_zc), trust));
  auto vcsSource =
      CanonicalPackageSource::vcs(requireUrl("ssh://example.com/repository"_zc), revision(),
                                  relativePath("vendor"_zc, "dependency"_zc));
  auto localSource =
      CanonicalPackageSource::localPath(workspacePath(2, "workspace"_zc, "package"_zc));

  zc::MemoryResource upstream;
  zc::CountingMemoryResource resource(upstream);
  {
    expectSourceClone(resource, registrySource);
    expectSourceClone(resource, vcsSource);
    expectSourceClone(resource, localSource);

    auto consumer = package(localSource.clone(), "consumer"_zc);
    auto provider = package(vcsSource.clone(), "provider"_zc);
    auto admitted = PackageDependencyEdgeKey::from(zc::mv(consumer),
                                                   requireScalar<DependencyAlias>("provider"_zc),
                                                   DependencyDomain::Build, zc::mv(provider));
    ZC_IF_SOME(edge, admitted) {
      auto expected = edge.encode();
      auto clone = edge.clone(resource);
      auto moved = zc::mv(clone);
      ZC_EXPECT(moved.encode().asPtr() == expected.asPtr());
      ZC_EXPECT(moved.consumer().features().size() == 2);
      ZC_EXPECT(moved.provider().source().vcsSubdirectory().segments().size() == 2);
      ZC_EXPECT(resource.currentAllocatedBytes() > 0);
    } else {
      ZC_FAIL_REQUIRE("valid resource-clone package edge was rejected");
    }
  }
  ZC_EXPECT(resource.peakAllocatedBytes() > 0);
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);
}

}  // namespace zomlang::compiler::identity
