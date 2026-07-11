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

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/identity/canonical-scalar.h"
#include "zomlang/compiler/identity/canonical-url.h"
#include "zomlang/compiler/identity/semantic-version.h"
#include "zomlang/compiler/identity/sha256.h"
#include "zomlang/compiler/identity/sorted-feature-set.h"

namespace zomlang::compiler::identity {

class CanonicalEncoder;

/// \brief Package-root-relative path in canonical declaration order.
class CanonicalRelativePath final {
public:
  CanonicalRelativePath(CanonicalRelativePath&&) noexcept = default;
  CanonicalRelativePath& operator=(CanonicalRelativePath&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalRelativePath);

  ZC_NODISCARD static CanonicalRelativePath from(
      zc::Vector<CanonicalPathSegment>&& segments);
  ZC_NODISCARD CanonicalRelativePath clone() const;
  ZC_NODISCARD zc::ArrayPtr<const CanonicalPathSegment> segments() const noexcept;
  void encode(CanonicalEncoder& encoder) const;

private:
  explicit CanonicalRelativePath(zc::Vector<CanonicalPathSegment>&& canonical) noexcept;

  zc::Vector<CanonicalPathSegment> value;
};

/// \brief Workspace-relative canonical path with explicit leading parent count.
class CanonicalWorkspaceRelativePath final {
public:
  CanonicalWorkspaceRelativePath(CanonicalWorkspaceRelativePath&&) noexcept = default;
  CanonicalWorkspaceRelativePath& operator=(CanonicalWorkspaceRelativePath&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalWorkspaceRelativePath);

  ZC_NODISCARD static CanonicalWorkspaceRelativePath from(
      uint32_t leadingParentCount, zc::Vector<CanonicalPathSegment>&& segments);
  ZC_NODISCARD CanonicalWorkspaceRelativePath clone() const;
  ZC_NODISCARD uint32_t leadingParents() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CanonicalPathSegment> segments() const noexcept;
  void encode(CanonicalEncoder& encoder) const;

private:
  CanonicalWorkspaceRelativePath(uint32_t leadingParentCount,
                                 zc::Vector<CanonicalPathSegment>&& canonical) noexcept;

  uint32_t parentCount;
  zc::Vector<CanonicalPathSegment> value;
};

enum class VcsRevisionAlgorithm : uint8_t { Sha1 = 0x01, Sha256 = 0x02 };

/// \brief Immutable VCS content revision with a closed digest algorithm.
class VcsRevision final {
public:
  VcsRevision(VcsRevision&&) noexcept = default;
  VcsRevision& operator=(VcsRevision&&) noexcept = default;
  ZC_DISALLOW_COPY(VcsRevision);

  ZC_NODISCARD static zc::Maybe<VcsRevision> from(
      VcsRevisionAlgorithm algorithm, zc::ArrayPtr<const uint8_t> digest);
  ZC_NODISCARD VcsRevision clone() const;
  ZC_NODISCARD VcsRevisionAlgorithm algorithm() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> digest() const noexcept;
  void encode(CanonicalEncoder& encoder) const;

private:
  VcsRevision(VcsRevisionAlgorithm algorithm, zc::Array<uint8_t>&& digest) noexcept;

  VcsRevisionAlgorithm algorithmValue;
  zc::Array<uint8_t> digestValue;
};

/// \brief Canonical registry identity without aliases or credentials.
class RegistryIdentity final {
public:
  RegistryIdentity(RegistryIdentity&&) noexcept = default;
  RegistryIdentity& operator=(RegistryIdentity&&) noexcept = default;
  ZC_DISALLOW_COPY(RegistryIdentity);

  ZC_NODISCARD static RegistryIdentity from(CanonicalUrl&& indexUrl,
                                            const Sha256Digest& trustDomain);
  ZC_NODISCARD RegistryIdentity clone() const;
  void encode(CanonicalEncoder& encoder) const;

private:
  RegistryIdentity(CanonicalUrl&& indexUrl, const Sha256Digest& trustDomain) noexcept;

  CanonicalUrl url;
  Sha256Digest trust;
};

struct RegistryPackageSource final {
  RegistryIdentity registry;
};

struct VcsPackageSource final {
  CanonicalUrl repository;
  VcsRevision revision;
  CanonicalRelativePath subdirectory;
};

struct LocalPathPackageSource final {
  CanonicalWorkspaceRelativePath canonicalPath;
};

enum class PackageSourceKind : uint8_t { Registry = 0x01, Vcs = 0x02, LocalPath = 0x03 };

/// \brief Closed package-source union used by package identity.
class CanonicalPackageSource final {
public:
  CanonicalPackageSource(CanonicalPackageSource&&) noexcept = default;
  CanonicalPackageSource& operator=(CanonicalPackageSource&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalPackageSource);

  ZC_NODISCARD static CanonicalPackageSource registry(RegistryIdentity&& value);
  ZC_NODISCARD static CanonicalPackageSource vcs(CanonicalUrl&& repository,
                                                 VcsRevision&& revision,
                                                 CanonicalRelativePath&& subdirectory);
  ZC_NODISCARD static CanonicalPackageSource localPath(
      CanonicalWorkspaceRelativePath&& value);
  ZC_NODISCARD CanonicalPackageSource clone() const;
  ZC_NODISCARD PackageSourceKind kind() const noexcept;
  void encode(CanonicalEncoder& encoder) const;

private:
  explicit CanonicalPackageSource(RegistryPackageSource&& value) noexcept;
  explicit CanonicalPackageSource(VcsPackageSource&& value) noexcept;
  explicit CanonicalPackageSource(LocalPathPackageSource&& value) noexcept;

  zc::OneOf<RegistryPackageSource, VcsPackageSource, LocalPathPackageSource> value;
};

/// \brief Complete canonical package identity key.
class PackageKey final {
public:
  PackageKey(PackageKey&&) noexcept = default;
  PackageKey& operator=(PackageKey&&) noexcept = default;
  ZC_DISALLOW_COPY(PackageKey);

  ZC_NODISCARD static PackageKey from(CanonicalPackageSource&& source, PackageName&& name,
                                      ResolvedVersion&& version,
                                      SortedFeatureSet&& enabledFeatures);
  ZC_NODISCARD PackageKey clone() const;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  PackageKey(CanonicalPackageSource&& source, PackageName&& name, ResolvedVersion&& version,
             SortedFeatureSet&& enabledFeatures) noexcept;

  CanonicalPackageSource sourceValue;
  PackageName nameValue;
  ResolvedVersion versionValue;
  SortedFeatureSet featureValue;
};

enum class DependencyDomain : uint8_t { Target = 0x01, Development = 0x02, Build = 0x03 };

/// \brief Canonical resolved package dependency edge.
class PackageDependencyEdgeKey final {
public:
  PackageDependencyEdgeKey(PackageDependencyEdgeKey&&) noexcept = default;
  PackageDependencyEdgeKey& operator=(PackageDependencyEdgeKey&&) noexcept = default;
  ZC_DISALLOW_COPY(PackageDependencyEdgeKey);

  ZC_NODISCARD static zc::Maybe<PackageDependencyEdgeKey> from(PackageKey&& consumer,
                                                               DependencyAlias&& alias,
                                                               DependencyDomain domain,
                                                               PackageKey&& provider);
  ZC_NODISCARD PackageDependencyEdgeKey clone() const;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  PackageDependencyEdgeKey(PackageKey&& consumer, DependencyAlias&& alias,
                           DependencyDomain domain, PackageKey&& provider) noexcept;

  PackageKey consumerValue;
  DependencyAlias aliasValue;
  DependencyDomain domainValue;
  PackageKey providerValue;
};

}  // namespace zomlang::compiler::identity
