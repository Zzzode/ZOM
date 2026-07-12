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

#include "zomlang/compiler/identity/package-key.h"

#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::identity {
namespace {

zc::Vector<CanonicalPathSegment> cloneSegments(zc::ArrayPtr<const CanonicalPathSegment> input) {
  zc::Vector<CanonicalPathSegment> result(input.size());
  for (const auto& segment : input) { result.add(segment.clone()); }
  return result;
}

void encodeSegments(CanonicalEncoder& encoder, zc::ArrayPtr<const CanonicalPathSegment> input) {
  encoder.encodeSequenceSize(input.size());
  for (const auto& segment : input) { segment.encode(encoder); }
}

}  // namespace

CanonicalRelativePath::CanonicalRelativePath(zc::Vector<CanonicalPathSegment>&& canonical) noexcept
    : value(zc::mv(canonical)) {}

CanonicalRelativePath CanonicalRelativePath::from(zc::Vector<CanonicalPathSegment>&& segments) {
  return CanonicalRelativePath(zc::mv(segments));
}

CanonicalRelativePath CanonicalRelativePath::clone() const {
  return CanonicalRelativePath(cloneSegments(value.asPtr()));
}

zc::ArrayPtr<const CanonicalPathSegment> CanonicalRelativePath::segments() const noexcept {
  return value.asPtr();
}

void CanonicalRelativePath::encode(CanonicalEncoder& encoder) const {
  encodeSegments(encoder, value.asPtr());
}

CanonicalWorkspaceRelativePath::CanonicalWorkspaceRelativePath(
    uint32_t leadingParentCount, zc::Vector<CanonicalPathSegment>&& canonical) noexcept
    : parentCount(leadingParentCount), value(zc::mv(canonical)) {}

CanonicalWorkspaceRelativePath CanonicalWorkspaceRelativePath::from(
    uint32_t leadingParentCount, zc::Vector<CanonicalPathSegment>&& segments) {
  return CanonicalWorkspaceRelativePath(leadingParentCount, zc::mv(segments));
}

CanonicalWorkspaceRelativePath CanonicalWorkspaceRelativePath::clone() const {
  return CanonicalWorkspaceRelativePath(parentCount, cloneSegments(value.asPtr()));
}

uint32_t CanonicalWorkspaceRelativePath::leadingParents() const noexcept { return parentCount; }

zc::ArrayPtr<const CanonicalPathSegment> CanonicalWorkspaceRelativePath::segments() const noexcept {
  return value.asPtr();
}

void CanonicalWorkspaceRelativePath::encode(CanonicalEncoder& encoder) const {
  encoder.encodeUint32(parentCount);
  encodeSegments(encoder, value.asPtr());
}

VcsRevision::VcsRevision(VcsRevisionAlgorithm algorithm, zc::Array<uint8_t>&& digest) noexcept
    : algorithmValue(algorithm), digestValue(zc::mv(digest)) {}

zc::Maybe<VcsRevision> VcsRevision::from(VcsRevisionAlgorithm algorithm,
                                         zc::ArrayPtr<const uint8_t> digest) {
  size_t expected = 0;
  switch (algorithm) {
    case VcsRevisionAlgorithm::Sha1:
      expected = 20;
      break;
    case VcsRevisionAlgorithm::Sha256:
      expected = 32;
      break;
  }
  if (expected == 0) { return zc::none; }
  if (digest.size() != expected) { return zc::none; }
  return VcsRevision(algorithm, zc::heapArray(digest));
}

VcsRevision VcsRevision::clone() const {
  return VcsRevision(algorithmValue, zc::heapArray(digestValue.asPtr()));
}

VcsRevisionAlgorithm VcsRevision::algorithm() const noexcept { return algorithmValue; }

zc::ArrayPtr<const uint8_t> VcsRevision::digest() const noexcept { return digestValue.asPtr(); }

void VcsRevision::encode(CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(algorithmValue));
  for (uint8_t byte : digestValue) { encoder.encodeUint8(byte); }
}

RegistryIdentity::RegistryIdentity(CanonicalUrl&& indexUrl,
                                   const Sha256Digest& trustDomain) noexcept
    : url(zc::mv(indexUrl)), trust(trustDomain) {}

RegistryIdentity RegistryIdentity::from(CanonicalUrl&& indexUrl, const Sha256Digest& trustDomain) {
  return RegistryIdentity(zc::mv(indexUrl), trustDomain);
}

RegistryIdentity RegistryIdentity::clone() const { return RegistryIdentity(url.clone(), trust); }
const CanonicalUrl& RegistryIdentity::indexUrl() const noexcept { return url; }
const Sha256Digest& RegistryIdentity::trustDomain() const noexcept { return trust; }

void RegistryIdentity::encode(CanonicalEncoder& encoder) const {
  url.encode(encoder);
  encoder.encodeDigest(trust);
}

CanonicalPackageSource::CanonicalPackageSource(RegistryPackageSource&& source) noexcept
    : value(zc::mv(source)) {}

CanonicalPackageSource::CanonicalPackageSource(VcsPackageSource&& source) noexcept
    : value(zc::mv(source)) {}

CanonicalPackageSource::CanonicalPackageSource(LocalPathPackageSource&& source) noexcept
    : value(zc::mv(source)) {}

CanonicalPackageSource CanonicalPackageSource::registry(RegistryIdentity&& value) {
  return CanonicalPackageSource(RegistryPackageSource{zc::mv(value)});
}

CanonicalPackageSource CanonicalPackageSource::vcs(CanonicalUrl&& repository,
                                                   VcsRevision&& revision,
                                                   CanonicalRelativePath&& subdirectory) {
  return CanonicalPackageSource(
      VcsPackageSource{zc::mv(repository), zc::mv(revision), zc::mv(subdirectory)});
}

CanonicalPackageSource CanonicalPackageSource::localPath(CanonicalWorkspaceRelativePath&& value) {
  return CanonicalPackageSource(LocalPathPackageSource{zc::mv(value)});
}

CanonicalPackageSource CanonicalPackageSource::clone() const {ZC_SWITCH_ONEOF(value){
    ZC_CASE_ONEOF(source, RegistryPackageSource){return registry(source.registry.clone());
}  // namespace zomlang::compiler::identity
ZC_CASE_ONEOF(source, VcsPackageSource) {
  return vcs(source.repository.clone(), source.revision.clone(), source.subdirectory.clone());
}
ZC_CASE_ONEOF(source, LocalPathPackageSource) { return localPath(source.canonicalPath.clone()); }
}
ZC_UNREACHABLE
}

PackageSourceKind CanonicalPackageSource::kind() const noexcept {
  if (value.is<RegistryPackageSource>()) { return PackageSourceKind::Registry; }
  if (value.is<VcsPackageSource>()) { return PackageSourceKind::Vcs; }
  return PackageSourceKind::LocalPath;
}

const RegistryIdentity& CanonicalPackageSource::registryIdentity() const {
  return value.get<RegistryPackageSource>().registry;
}
const CanonicalUrl& CanonicalPackageSource::vcsRepository() const {
  return value.get<VcsPackageSource>().repository;
}
const VcsRevision& CanonicalPackageSource::vcsRevision() const {
  return value.get<VcsPackageSource>().revision;
}
const CanonicalRelativePath& CanonicalPackageSource::vcsSubdirectory() const {
  return value.get<VcsPackageSource>().subdirectory;
}
const CanonicalWorkspaceRelativePath& CanonicalPackageSource::localPath() const {
  return value.get<LocalPathPackageSource>().canonicalPath;
}

void CanonicalPackageSource::encode(CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(kind()));
  ZC_SWITCH_ONEOF(value) {
    ZC_CASE_ONEOF(source, RegistryPackageSource) { source.registry.encode(encoder); }
    ZC_CASE_ONEOF(source, VcsPackageSource) {
      source.repository.encode(encoder);
      source.revision.encode(encoder);
      source.subdirectory.encode(encoder);
    }
    ZC_CASE_ONEOF(source, LocalPathPackageSource) { source.canonicalPath.encode(encoder); }
  }
}

PackageBaseKey::PackageBaseKey(CanonicalPackageSource&& source, PackageName&& name,
                               ResolvedVersion&& version) noexcept
    : sourceValue(zc::mv(source)), nameValue(zc::mv(name)), versionValue(zc::mv(version)) {}

PackageBaseKey PackageBaseKey::from(CanonicalPackageSource&& source, PackageName&& name,
                                    ResolvedVersion&& version) {
  return PackageBaseKey(zc::mv(source), zc::mv(name), zc::mv(version));
}

PackageBaseKey PackageBaseKey::clone() const {
  return PackageBaseKey(sourceValue.clone(), nameValue.clone(), versionValue.clone());
}

const CanonicalPackageSource& PackageBaseKey::source() const noexcept { return sourceValue; }
zc::StringPtr PackageBaseKey::name() const noexcept { return nameValue.text(); }
zc::StringPtr PackageBaseKey::version() const noexcept { return versionValue.text(); }

void PackageBaseKey::encode(CanonicalEncoder& encoder) const {
  sourceValue.encode(encoder);
  nameValue.encode(encoder);
  versionValue.encode(encoder);
}

zc::Array<uint8_t> PackageBaseKey::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

PackageKey::PackageKey(CanonicalPackageSource&& source, PackageName&& name,
                       ResolvedVersion&& version, SortedFeatureSet&& enabledFeatures) noexcept
    : sourceValue(zc::mv(source)),
      nameValue(zc::mv(name)),
      versionValue(zc::mv(version)),
      featureValue(zc::mv(enabledFeatures)) {}

PackageKey PackageKey::from(CanonicalPackageSource&& source, PackageName&& name,
                            ResolvedVersion&& version, SortedFeatureSet&& enabledFeatures) {
  return PackageKey(zc::mv(source), zc::mv(name), zc::mv(version), zc::mv(enabledFeatures));
}

PackageKey PackageKey::clone() const {
  return PackageKey(sourceValue.clone(), nameValue.clone(), versionValue.clone(),
                    featureValue.clone());
}
const CanonicalPackageSource& PackageKey::source() const noexcept { return sourceValue; }
zc::StringPtr PackageKey::name() const noexcept { return nameValue.text(); }
zc::StringPtr PackageKey::version() const noexcept { return versionValue.text(); }
zc::ArrayPtr<const FeatureName> PackageKey::features() const noexcept {
  return featureValue.values();
}

void PackageKey::encode(CanonicalEncoder& encoder) const {
  sourceValue.encode(encoder);
  nameValue.encode(encoder);
  versionValue.encode(encoder);
  featureValue.encode(encoder);
}

zc::Array<uint8_t> PackageKey::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

PackageDependencyEdgeKey::PackageDependencyEdgeKey(PackageKey&& consumer, DependencyAlias&& alias,
                                                   DependencyDomain domain,
                                                   PackageKey&& provider) noexcept
    : consumerValue(zc::mv(consumer)),
      aliasValue(zc::mv(alias)),
      domainValue(domain),
      providerValue(zc::mv(provider)) {}

zc::Maybe<PackageDependencyEdgeKey> PackageDependencyEdgeKey::from(PackageKey&& consumer,
                                                                   DependencyAlias&& alias,
                                                                   DependencyDomain domain,
                                                                   PackageKey&& provider) {
  if (domain != DependencyDomain::Target && domain != DependencyDomain::Development &&
      domain != DependencyDomain::Build) {
    return zc::none;
  }
  return PackageDependencyEdgeKey(zc::mv(consumer), zc::mv(alias), domain, zc::mv(provider));
}

PackageDependencyEdgeKey PackageDependencyEdgeKey::clone() const {
  return PackageDependencyEdgeKey(consumerValue.clone(), aliasValue.clone(), domainValue,
                                  providerValue.clone());
}
const PackageKey& PackageDependencyEdgeKey::consumer() const noexcept { return consumerValue; }
zc::StringPtr PackageDependencyEdgeKey::alias() const noexcept { return aliasValue.text(); }
DependencyDomain PackageDependencyEdgeKey::domain() const noexcept { return domainValue; }
const PackageKey& PackageDependencyEdgeKey::provider() const noexcept { return providerValue; }

void PackageDependencyEdgeKey::encode(CanonicalEncoder& encoder) const {
  consumerValue.encode(encoder);
  aliasValue.encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(domainValue));
  providerValue.encode(encoder);
}

zc::Array<uint8_t> PackageDependencyEdgeKey::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

}  // namespace zomlang::compiler::identity
