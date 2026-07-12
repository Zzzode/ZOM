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
#include "zc/core/string.h"
#include "zomlang/compiler/driver/package/manifest-model.h"
#include "zomlang/compiler/driver/package/semver-constraint.h"
#include "zomlang/compiler/identity/package-key.h"

namespace zomlang::compiler::driver::package {

enum class VcsSelectorKind : uint8_t { Revision = 0x01, Tag = 0x02, Branch = 0x03 };

struct RevisionVcsSelector final {
  identity::VcsRevision revision;
};

struct TagVcsSelector final {
  zc::String tag;
};

struct BranchVcsSelector final {
  zc::String branch;
};

/// \brief Closed immutable VCS selector admitted by a dependency manifest.
class VcsSelector final {
public:
  ZC_NODISCARD static VcsSelector revision(identity::VcsRevision&& revision);
  ZC_NODISCARD static zc::Maybe<VcsSelector> tag(zc::StringPtr tag);
  ZC_NODISCARD static zc::Maybe<VcsSelector> branch(zc::StringPtr branch);

  VcsSelector(VcsSelector&&) noexcept = default;
  VcsSelector& operator=(VcsSelector&&) noexcept = default;
  ZC_DISALLOW_COPY(VcsSelector);

  ZC_NODISCARD VcsSelector clone() const;
  /// \brief Clones this selector and all owned storage into `resource`.
  ZC_NODISCARD VcsSelector clone(zc::MemoryResource& resource) const;
  ZC_NODISCARD VcsSelectorKind kind() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  explicit VcsSelector(RevisionVcsSelector&& selector) noexcept;
  explicit VcsSelector(TagVcsSelector&& selector) noexcept;
  explicit VcsSelector(BranchVcsSelector&& selector) noexcept;

  zc::OneOf<RevisionVcsSelector, TagVcsSelector, BranchVcsSelector> value;
};

enum class PackageSourceConstraintKind : uint8_t { Registry = 0x01, Vcs = 0x02, LocalPath = 0x03 };

struct RegistrySourceConstraint final {
  identity::RegistryIdentity registry;
};

struct VcsSourceConstraint final {
  identity::CanonicalUrl repository;
  VcsSelector selector;
  identity::CanonicalRelativePath subdirectory;
};

struct LocalPathSourceConstraint final {
  identity::CanonicalWorkspaceRelativePath canonicalPath;
};

/// \brief Closed registry, VCS, or local dependency source requirement.
class PackageSourceConstraint final {
public:
  ZC_NODISCARD static PackageSourceConstraint registry(identity::RegistryIdentity&& registry);
  ZC_NODISCARD static PackageSourceConstraint vcs(identity::CanonicalUrl&& repository,
                                                  VcsSelector&& selector,
                                                  identity::CanonicalRelativePath&& subdirectory);
  ZC_NODISCARD static PackageSourceConstraint localPath(
      identity::CanonicalWorkspaceRelativePath&& canonicalPath);

  PackageSourceConstraint(PackageSourceConstraint&&) noexcept = default;
  PackageSourceConstraint& operator=(PackageSourceConstraint&&) noexcept = default;
  ZC_DISALLOW_COPY(PackageSourceConstraint);

  ZC_NODISCARD PackageSourceConstraint clone() const;
  /// \brief Clones this source constraint and all owned storage into `resource`.
  ZC_NODISCARD PackageSourceConstraint clone(zc::MemoryResource& resource) const;
  ZC_NODISCARD PackageSourceConstraintKind kind() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  explicit PackageSourceConstraint(RegistrySourceConstraint&& source) noexcept;
  explicit PackageSourceConstraint(VcsSourceConstraint&& source) noexcept;
  explicit PackageSourceConstraint(LocalPathSourceConstraint&& source) noexcept;

  zc::OneOf<RegistrySourceConstraint, VcsSourceConstraint, LocalPathSourceConstraint> value;
};

/// \brief Dependency requirement fields without diagnostic provenance.
class DependencyRequirementWithoutOrigin final {
public:
  ZC_NODISCARD static zc::Maybe<DependencyRequirementWithoutOrigin> from(
      identity::DependencyAlias&& alias, identity::PackageName&& requiredPackage,
      identity::DependencyDomain domain, PackageSourceConstraint&& source,
      zc::Maybe<SemVerConstraint>&& versionCheck, identity::SortedFeatureSet&& requestedFeatures,
      bool useDefaultFeatures, bool optional);

  DependencyRequirementWithoutOrigin(DependencyRequirementWithoutOrigin&&) noexcept = default;
  DependencyRequirementWithoutOrigin& operator=(DependencyRequirementWithoutOrigin&&) noexcept =
      default;
  ZC_DISALLOW_COPY(DependencyRequirementWithoutOrigin);

  ZC_NODISCARD DependencyRequirementWithoutOrigin clone() const;
  /// \brief Clones this requirement and all owned storage into `resource`.
  ZC_NODISCARD DependencyRequirementWithoutOrigin clone(zc::MemoryResource& resource) const;
  ZC_NODISCARD zc::StringPtr alias() const noexcept;
  ZC_NODISCARD zc::StringPtr requiredPackage() const noexcept;
  ZC_NODISCARD identity::DependencyDomain domain() const noexcept;
  ZC_NODISCARD PackageSourceConstraintKind sourceKind() const noexcept;
  ZC_NODISCARD const PackageSourceConstraint& source() const noexcept;
  ZC_NODISCARD bool hasVersionCheck() const noexcept;
  ZC_NODISCARD const SemVerConstraint& versionCheck() const;
  ZC_NODISCARD zc::ArrayPtr<const identity::FeatureName> requestedFeatures() const noexcept;
  ZC_NODISCARD bool useDefaultFeatures() const noexcept;
  ZC_NODISCARD bool optional() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  DependencyRequirementWithoutOrigin(identity::DependencyAlias&& alias,
                                     identity::PackageName&& requiredPackage,
                                     identity::DependencyDomain domain,
                                     PackageSourceConstraint&& source,
                                     zc::Maybe<SemVerConstraint>&& versionCheck,
                                     identity::SortedFeatureSet&& requestedFeatures,
                                     bool useDefaultFeatures, bool optional) noexcept;

  identity::DependencyAlias aliasValue;
  identity::PackageName requiredPackageValue;
  identity::DependencyDomain domainValue;
  PackageSourceConstraint sourceValue;
  zc::Maybe<SemVerConstraint> versionCheckValue;
  identity::SortedFeatureSet requestedFeatureValues;
  bool useDefaultFeaturesValue;
  bool optionalValue;
};

/// \brief Complete normalized dependency requirement with exact manifest provenance.
class DependencyRequirement final {
public:
  ZC_NODISCARD static DependencyRequirement from(DependencyRequirementWithoutOrigin&& value,
                                                 DiagnosticAnchor&& origin);

  DependencyRequirement(DependencyRequirement&&) noexcept = default;
  DependencyRequirement& operator=(DependencyRequirement&&) noexcept = default;
  ZC_DISALLOW_COPY(DependencyRequirement);

  ZC_NODISCARD DependencyRequirement clone() const;
  ZC_NODISCARD const DependencyRequirementWithoutOrigin& withoutOrigin() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  DependencyRequirement(DependencyRequirementWithoutOrigin&& value,
                        DiagnosticAnchor&& origin) noexcept;

  DependencyRequirementWithoutOrigin value;
  DiagnosticAnchor originValue;
};

/// \brief Sorts dependency requirements by origin-free canonical bytes and rejects duplicates.
ZC_NODISCARD bool sortDependencyRequirements(zc::Vector<DependencyRequirement>& requirements);

}  // namespace zomlang::compiler::driver::package
