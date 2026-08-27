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

#include "zomlang/compiler/identity/key/package-key.h"

#include "zomlang/compiler/identity/canonical/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::identity {
namespace {

constexpr uint64_t kMaximumCanonicalUrlBytes = 4096;
constexpr uint64_t kMaximumResolvedVersionBytes = 256;
constexpr uint64_t kMaximumPathSegments = 256;
constexpr uint32_t kMaximumLeadingParentCount = 256;

zc::Vector<CanonicalPathSegment> cloneSegments(zc::ArrayPtr<const CanonicalPathSegment> input) {
  zc::Vector<CanonicalPathSegment> result(input.size());
  for (const auto& segment : input) { result.add(segment.clone()); }
  return result;
}

zc::Vector<CanonicalPathSegment> cloneSegments(zc::MemoryResource& resource,
                                               zc::ArrayPtr<const CanonicalPathSegment> input) {
  zc::Vector<CanonicalPathSegment> result(resource, input.size());
  for (const auto& segment : input) { result.add(segment.clone(resource)); }
  return result;
}

zc::Array<uint8_t> cloneBytes(zc::MemoryResource& resource, zc::ArrayPtr<const uint8_t> input) {
  auto result = zc::resourceHeapArray<uint8_t>(resource, input.size());
  for (size_t index = 0; index < input.size(); ++index) { result[index] = input[index]; }
  return result;
}

void encodeSegments(CanonicalEncoder& encoder, zc::ArrayPtr<const CanonicalPathSegment> input) {
  encoder.encodeSequenceSize(input.size());
  for (const auto& segment : input) { segment.encode(encoder); }
}

zc::Maybe<CanonicalUrl> decodeCanonicalUrl(CanonicalDecoder& decoder) {
  auto bytes = decoder.decodeByteString(kMaximumCanonicalUrlBytes);
  ZC_IF_SOME(value, bytes) {
    auto text = zc::str(value.asChars());
    return CanonicalUrl::fromCanonical(text);
  }
  return zc::none;
}

zc::Maybe<ResolvedVersion> decodeResolvedVersion(CanonicalDecoder& decoder) {
  auto bytes = decoder.decodeByteString(kMaximumResolvedVersionBytes);
  ZC_IF_SOME(value, bytes) {
    auto text = zc::str(value.asChars());
    return ResolvedVersion::fromCanonical(text);
  }
  return zc::none;
}

}  // namespace

CanonicalRelativePath::CanonicalRelativePath(zc::Vector<CanonicalPathSegment>&& canonical) noexcept
    : value(zc::mv(canonical)) {}

CanonicalRelativePath CanonicalRelativePath::from(zc::Vector<CanonicalPathSegment>&& segments) {
  return CanonicalRelativePath(zc::mv(segments));
}

zc::Maybe<CanonicalRelativePath> CanonicalRelativePath::decodeCanonical(CanonicalDecoder& decoder) {
  auto count = decoder.decodeSequenceSize(kMaximumPathSegments);
  if (count == zc::none) { return zc::none; }
  ZC_IF_SOME(size, count) {
    zc::Vector<CanonicalPathSegment> segments(static_cast<size_t>(size));
    for (uint64_t index = 0; index < size; ++index) {
      auto segment = CanonicalPathSegment::decodeCanonical(decoder);
      if (segment == zc::none) { return zc::none; }
      ZC_IF_SOME(value, segment) { segments.add(zc::mv(value)); }
    }
    return CanonicalRelativePath(zc::mv(segments));
  }
  return zc::none;
}

CanonicalRelativePath CanonicalRelativePath::clone() const {
  return CanonicalRelativePath(cloneSegments(value.asPtr()));
}

CanonicalRelativePath CanonicalRelativePath::clone(zc::MemoryResource& resource) const {
  return CanonicalRelativePath(cloneSegments(resource, value.asPtr()));
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

zc::Maybe<CanonicalWorkspaceRelativePath> CanonicalWorkspaceRelativePath::decodeCanonical(
    CanonicalDecoder& decoder) {
  auto leadingParents = decoder.decodeUint32();
  if (leadingParents == zc::none) { return zc::none; }
  ZC_IF_SOME(parentCount, leadingParents) {
    if (parentCount > kMaximumLeadingParentCount) { return zc::none; }
    auto path = CanonicalRelativePath::decodeCanonical(decoder);
    ZC_IF_SOME(value, path) {
      zc::Vector<CanonicalPathSegment> segments(value.segments().size());
      for (const auto& segment : value.segments()) { segments.add(segment.clone()); }
      return CanonicalWorkspaceRelativePath(parentCount, zc::mv(segments));
    }
  }
  return zc::none;
}

CanonicalWorkspaceRelativePath CanonicalWorkspaceRelativePath::clone() const {
  return CanonicalWorkspaceRelativePath(parentCount, cloneSegments(value.asPtr()));
}

CanonicalWorkspaceRelativePath CanonicalWorkspaceRelativePath::clone(
    zc::MemoryResource& resource) const {
  return CanonicalWorkspaceRelativePath(parentCount, cloneSegments(resource, value.asPtr()));
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

zc::Maybe<VcsRevision> VcsRevision::decodeCanonical(CanonicalDecoder& decoder) {
  auto algorithm = decoder.decodeUint8();
  ZC_IF_SOME(tag, algorithm) {
    size_t digestBytes = 0;
    if (tag == static_cast<uint8_t>(VcsRevisionAlgorithm::Sha1)) {
      digestBytes = 20;
    } else if (tag == static_cast<uint8_t>(VcsRevisionAlgorithm::Sha256)) {
      digestBytes = 32;
    } else {
      return zc::none;
    }
    auto digest = decoder.decodeBytes(digestBytes);
    ZC_IF_SOME(value, digest) {
      return VcsRevision::from(static_cast<VcsRevisionAlgorithm>(tag), value.asPtr());
    }
  }
  return zc::none;
}

VcsRevision VcsRevision::clone() const {
  return VcsRevision(algorithmValue, zc::heapArray(digestValue.asPtr()));
}

VcsRevision VcsRevision::clone(zc::MemoryResource& resource) const {
  return VcsRevision(algorithmValue, cloneBytes(resource, digestValue.asPtr()));
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

zc::Maybe<RegistryIdentity> RegistryIdentity::decodeCanonical(CanonicalDecoder& decoder) {
  auto indexUrl = decodeCanonicalUrl(decoder);
  if (indexUrl == zc::none) { return zc::none; }
  auto trustDomain = decoder.decodeDigest();
  if (trustDomain == zc::none) { return zc::none; }
  ZC_IF_SOME(url, indexUrl) {
    ZC_IF_SOME(digest, trustDomain) { return RegistryIdentity(zc::mv(url), digest); }
  }
  return zc::none;
}

RegistryIdentity RegistryIdentity::clone() const { return RegistryIdentity(url.clone(), trust); }
RegistryIdentity RegistryIdentity::clone(zc::MemoryResource& resource) const {
  return RegistryIdentity(url.clone(resource), trust);
}
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

zc::Maybe<CanonicalPackageSource> CanonicalPackageSource::decodeCanonical(
    CanonicalDecoder& decoder) {
  auto kind = decoder.decodeUint8();
  ZC_IF_SOME(tag, kind) {
    switch (static_cast<PackageSourceKind>(tag)) {
      case PackageSourceKind::Registry: {
        auto registry = RegistryIdentity::decodeCanonical(decoder);
        ZC_IF_SOME(value, registry) { return CanonicalPackageSource::registry(zc::mv(value)); }
        return zc::none;
      }
      case PackageSourceKind::Vcs: {
        auto repository = decodeCanonicalUrl(decoder);
        if (repository == zc::none) { return zc::none; }
        auto revision = VcsRevision::decodeCanonical(decoder);
        if (revision == zc::none) { return zc::none; }
        auto subdirectory = CanonicalRelativePath::decodeCanonical(decoder);
        if (subdirectory == zc::none) { return zc::none; }
        ZC_IF_SOME(url, repository) {
          ZC_IF_SOME(revisionValue, revision) {
            ZC_IF_SOME(path, subdirectory) {
              return CanonicalPackageSource::vcs(zc::mv(url), zc::mv(revisionValue), zc::mv(path));
            }
          }
        }
        return zc::none;
      }
      case PackageSourceKind::LocalPath: {
        auto path = CanonicalWorkspaceRelativePath::decodeCanonical(decoder);
        ZC_IF_SOME(value, path) { return CanonicalPackageSource::localPath(zc::mv(value)); }
        return zc::none;
      }
    }
  }
  return zc::none;
}

CanonicalPackageSource CanonicalPackageSource::clone() const {
  ZC_SWITCH_ONEOF(value) {
    ZC_CASE_ONEOF(source, RegistryPackageSource) {
      return registry(source.registry.clone());
    }  // namespace zomlang::compiler::identity
    ZC_CASE_ONEOF(source, VcsPackageSource) {
      return vcs(source.repository.clone(), source.revision.clone(), source.subdirectory.clone());
    }
    ZC_CASE_ONEOF(source, LocalPathPackageSource) {
      return localPath(source.canonicalPath.clone());
    }
  }
  ZC_UNREACHABLE
}

CanonicalPackageSource CanonicalPackageSource::clone(zc::MemoryResource& resource) const {
  ZC_SWITCH_ONEOF(value) {
    ZC_CASE_ONEOF(source, RegistryPackageSource) {
      return registry(source.registry.clone(resource));
    }
    ZC_CASE_ONEOF(source, VcsPackageSource) {
      return vcs(source.repository.clone(resource), source.revision.clone(resource),
                 source.subdirectory.clone(resource));
    }
    ZC_CASE_ONEOF(source, LocalPathPackageSource) {
      return localPath(source.canonicalPath.clone(resource));
    }
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

zc::Maybe<PackageBaseKey> PackageBaseKey::decodeCanonical(CanonicalDecoder& decoder) {
  auto source = CanonicalPackageSource::decodeCanonical(decoder);
  if (source == zc::none) { return zc::none; }
  auto name = PackageName::decodeCanonical(decoder);
  if (name == zc::none) { return zc::none; }
  auto version = decodeResolvedVersion(decoder);
  if (version == zc::none) { return zc::none; }
  ZC_IF_SOME(sourceValue, source) {
    ZC_IF_SOME(nameValue, name) {
      ZC_IF_SOME(versionValue, version) {
        return PackageBaseKey(zc::mv(sourceValue), zc::mv(nameValue), zc::mv(versionValue));
      }
    }
  }
  return zc::none;
}

PackageBaseKey PackageBaseKey::clone() const {
  return PackageBaseKey(sourceValue.clone(), nameValue.clone(), versionValue.clone());
}

PackageBaseKey PackageBaseKey::clone(zc::MemoryResource& resource) const {
  return PackageBaseKey(sourceValue.clone(resource), nameValue.clone(resource),
                        versionValue.clone(resource));
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

zc::Maybe<PackageKey> PackageKey::decodeCanonical(CanonicalDecoder& decoder) {
  auto source = CanonicalPackageSource::decodeCanonical(decoder);
  if (source == zc::none) { return zc::none; }
  auto name = PackageName::decodeCanonical(decoder);
  if (name == zc::none) { return zc::none; }
  auto version = decodeResolvedVersion(decoder);
  if (version == zc::none) { return zc::none; }
  auto features = SortedFeatureSet::decodeCanonical(decoder);
  if (features == zc::none) { return zc::none; }
  ZC_IF_SOME(sourceValue, source) {
    ZC_IF_SOME(nameValue, name) {
      ZC_IF_SOME(versionValue, version) {
        ZC_IF_SOME(featureValues, features) {
          return PackageKey(zc::mv(sourceValue), zc::mv(nameValue), zc::mv(versionValue),
                            zc::mv(featureValues));
        }
      }
    }
  }
  return zc::none;
}

PackageKey PackageKey::clone() const {
  return PackageKey(sourceValue.clone(), nameValue.clone(), versionValue.clone(),
                    featureValue.clone());
}
PackageKey PackageKey::clone(zc::MemoryResource& resource) const {
  return PackageKey(sourceValue.clone(resource), nameValue.clone(resource),
                    versionValue.clone(resource), featureValue.clone(resource));
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

zc::Maybe<PackageDependencyEdgeKey> PackageDependencyEdgeKey::decodeCanonical(
    CanonicalDecoder& decoder) {
  auto consumer = PackageKey::decodeCanonical(decoder);
  if (consumer == zc::none) { return zc::none; }
  auto alias = DependencyAlias::decodeCanonical(decoder);
  if (alias == zc::none) { return zc::none; }
  auto domain = decoder.decodeUint8();
  if (domain == zc::none) { return zc::none; }
  ZC_IF_SOME(value, domain) {
    if (value != static_cast<uint8_t>(DependencyDomain::Target) &&
        value != static_cast<uint8_t>(DependencyDomain::Development) &&
        value != static_cast<uint8_t>(DependencyDomain::Build)) {
      return zc::none;
    }
  }
  auto provider = PackageKey::decodeCanonical(decoder);
  if (provider == zc::none) { return zc::none; }
  ZC_IF_SOME(consumerValue, consumer) {
    ZC_IF_SOME(aliasValue, alias) {
      ZC_IF_SOME(domainValue, domain) {
        ZC_IF_SOME(providerValue, provider) {
          return from(zc::mv(consumerValue), zc::mv(aliasValue),
                      static_cast<DependencyDomain>(domainValue), zc::mv(providerValue));
        }
      }
    }
  }
  return zc::none;
}

PackageDependencyEdgeKey PackageDependencyEdgeKey::clone() const {
  return PackageDependencyEdgeKey(consumerValue.clone(), aliasValue.clone(), domainValue,
                                  providerValue.clone());
}
PackageDependencyEdgeKey PackageDependencyEdgeKey::clone(zc::MemoryResource& resource) const {
  return PackageDependencyEdgeKey(consumerValue.clone(resource), aliasValue.clone(resource),
                                  domainValue, providerValue.clone(resource));
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
