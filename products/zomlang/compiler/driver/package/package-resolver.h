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
#include "zomlang/compiler/driver/package/feature-resolver.h"
#include "zomlang/compiler/identity/package-key.h"

namespace zomlang::compiler::driver::package {

class LocalPackageRecord;
class VerifiedRegistryReleaseRecord;
class VerifiedVcsPackageRecord;

/// \brief Resolver input for one immutable available package release.
class ResolverRelease final {
public:
  ZC_NODISCARD static ResolverRelease from(PackageSourceConstraint&& acceptedSource,
                                           identity::PackageBaseKey&& base,
                                           CanonicalManifestRecord&& manifest, bool yanked);
  ZC_NODISCARD static ResolverRelease fromRegistry(const VerifiedRegistryReleaseRecord& release);
  ZC_NODISCARD static ResolverRelease fromVcs(const VerifiedVcsPackageRecord& release);
  ZC_NODISCARD static ResolverRelease fromVcs(const VerifiedVcsPackageRecord& release,
                                              PackageSourceConstraint&& acceptedSelector);
  ZC_NODISCARD static ResolverRelease fromLocal(const LocalPackageRecord& release);

  ResolverRelease(ResolverRelease&&) noexcept = default;
  ResolverRelease& operator=(ResolverRelease&&) noexcept = default;
  ZC_DISALLOW_COPY(ResolverRelease);

  ZC_NODISCARD ResolverRelease clone() const;
  ZC_NODISCARD const PackageSourceConstraint& acceptedSource() const noexcept;
  ZC_NODISCARD const identity::PackageBaseKey& base() const noexcept;
  ZC_NODISCARD const CanonicalManifestRecord& manifest() const noexcept;
  ZC_NODISCARD bool yanked() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  ResolverRelease(PackageSourceConstraint&& acceptedSource, identity::PackageBaseKey&& base,
                  CanonicalManifestRecord&& manifest, bool yanked) noexcept;

  PackageSourceConstraint sourceValue;
  identity::PackageBaseKey baseValue;
  CanonicalManifestRecord manifestValue;
  bool yankedValue;
};

/// \brief Exact root package and its requested feature activation.
class ResolverRoot final {
public:
  ZC_NODISCARD static ResolverRoot from(identity::PackageBaseKey&& base,
                                        identity::SortedFeatureSet&& requestedFeatures,
                                        bool useDefaultFeatures, bool includeDevelopment);

  ResolverRoot(ResolverRoot&&) noexcept = default;
  ResolverRoot& operator=(ResolverRoot&&) noexcept = default;
  ZC_DISALLOW_COPY(ResolverRoot);

  ZC_NODISCARD ResolverRoot clone() const;
  ZC_NODISCARD const identity::PackageBaseKey& base() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::FeatureName> requestedFeatures() const noexcept;
  ZC_NODISCARD bool useDefaultFeatures() const noexcept;
  ZC_NODISCARD bool includeDevelopment() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  ResolverRoot(identity::PackageBaseKey&& base, identity::SortedFeatureSet&& requestedFeatures,
               bool useDefaultFeatures, bool includeDevelopment) noexcept;

  identity::PackageBaseKey baseValue;
  identity::SortedFeatureSet requestedFeatureValues;
  bool useDefaultFeaturesValue;
  bool includeDevelopmentValue;
};

enum class ResolverIssue : uint8_t {
  NoVersionSatisfiesConstraints = 0x01,
  FeatureInvalid = 0x02,
  DependencyCycle = 0x03,
  DependencyLibraryTargetMissing = 0x04,
  SourceBindingMissing = 0x05,
  InvalidRoot = 0x06
};

/// \brief One content-addressed canonical incompatibility derivation record.
class IncompatibilityRecord final {
public:
  ZC_NODISCARD static IncompatibilityRecord from(const identity::Sha256Digest& id,
                                                 zc::Array<uint8_t>&& canonicalBytes);

  IncompatibilityRecord(IncompatibilityRecord&&) noexcept = default;
  IncompatibilityRecord& operator=(IncompatibilityRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(IncompatibilityRecord);

  ZC_NODISCARD const identity::Sha256Digest& id() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> canonicalBytes() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  IncompatibilityRecord(const identity::Sha256Digest& id,
                        zc::Array<uint8_t>&& canonicalBytes) noexcept;

  identity::Sha256Digest idValue;
  zc::Array<uint8_t> byteValues;
};

/// \brief Closed acyclic incompatibility derivation used by conflict diagnostics.
class IncompatibilityGraph final {
public:
  ZC_NODISCARD static IncompatibilityGraph from(const identity::Sha256Digest& root,
                                                zc::Vector<IncompatibilityRecord>&& records);

  IncompatibilityGraph(IncompatibilityGraph&&) noexcept = default;
  IncompatibilityGraph& operator=(IncompatibilityGraph&&) noexcept = default;
  ZC_DISALLOW_COPY(IncompatibilityGraph);

  ZC_NODISCARD const identity::Sha256Digest& root() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const IncompatibilityRecord> records() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  IncompatibilityGraph(const identity::Sha256Digest& root,
                       zc::Vector<IncompatibilityRecord>&& records) noexcept;

  identity::Sha256Digest rootValue;
  zc::Vector<IncompatibilityRecord> recordValues;
};

/// \brief Deterministic structural resolver failure and canonical explanation facts.
class PackageResolverFailure final {
public:
  ZC_NODISCARD static PackageResolverFailure from(ResolverIssue issue,
                                                  zc::Array<uint8_t>&& coordinate,
                                                  zc::Vector<zc::Array<uint8_t>>&& causes);
  ZC_NODISCARD static PackageResolverFailure withIncompatibility(
      ResolverIssue issue, zc::Array<uint8_t>&& coordinate, zc::Vector<zc::Array<uint8_t>>&& causes,
      IncompatibilityGraph&& graph);

  PackageResolverFailure(PackageResolverFailure&&) noexcept = default;
  PackageResolverFailure& operator=(PackageResolverFailure&&) noexcept = default;
  ZC_DISALLOW_COPY(PackageResolverFailure);

  ZC_NODISCARD ResolverIssue issue() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> coordinate() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const zc::Array<uint8_t>> causes() const noexcept;
  ZC_NODISCARD bool hasIncompatibilityGraph() const noexcept;
  /// \pre `hasIncompatibilityGraph()` is true.
  ZC_NODISCARD const IncompatibilityGraph& incompatibilityGraph() const;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  PackageResolverFailure(ResolverIssue issue, zc::Array<uint8_t>&& coordinate,
                         zc::Vector<zc::Array<uint8_t>>&& causes,
                         zc::Maybe<IncompatibilityGraph>&& graph) noexcept;

  ResolverIssue issueValue;
  zc::Array<uint8_t> coordinateValue;
  zc::Vector<zc::Array<uint8_t>> causeValues;
  zc::Maybe<IncompatibilityGraph> graphValue;
};

/// \brief One selected package release in one independently expanded feature domain.
class ResolvedPackageSelection final {
public:
  ZC_NODISCARD static ResolvedPackageSelection from(identity::PackageBaseKey&& base,
                                                    FeatureActivationDomain domain,
                                                    identity::SortedFeatureSet&& features);

  ResolvedPackageSelection(ResolvedPackageSelection&&) noexcept = default;
  ResolvedPackageSelection& operator=(ResolvedPackageSelection&&) noexcept = default;
  ZC_DISALLOW_COPY(ResolvedPackageSelection);

  ZC_NODISCARD const identity::PackageBaseKey& base() const noexcept;
  ZC_NODISCARD FeatureActivationDomain domain() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::FeatureName> features() const noexcept;
  ZC_NODISCARD identity::PackageKey packageKey() const;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  ResolvedPackageSelection(identity::PackageBaseKey&& base, FeatureActivationDomain domain,
                           identity::SortedFeatureSet&& features) noexcept;

  identity::PackageBaseKey baseValue;
  FeatureActivationDomain domainValue;
  identity::SortedFeatureSet featureValues;
};

/// \brief Canonically ordered selected graph before lockfile projection.
class PackageResolution final {
public:
  ZC_NODISCARD static PackageResolution from(
      zc::Vector<ResolvedPackageSelection>&& packages,
      zc::Vector<identity::PackageDependencyEdgeKey>&& edges);

  PackageResolution(PackageResolution&&) noexcept = default;
  PackageResolution& operator=(PackageResolution&&) noexcept = default;
  ZC_DISALLOW_COPY(PackageResolution);

  ZC_NODISCARD zc::ArrayPtr<const ResolvedPackageSelection> packages() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::PackageDependencyEdgeKey> edges() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  PackageResolution(zc::Vector<ResolvedPackageSelection>&& packages,
                    zc::Vector<identity::PackageDependencyEdgeKey>&& edges) noexcept;

  zc::Vector<ResolvedPackageSelection> packageValues;
  zc::Vector<identity::PackageDependencyEdgeKey> edgeValues;
};

using PackageResolutionResult = zc::OneOf<PackageResolution, PackageResolverFailure>;

/// \brief Deterministic operation counts for resolver performance gates.
struct PackageResolverMetrics final {
  uint64_t decisions = 0;
  uint64_t selectedPackages = 0;
  uint64_t emittedEdges = 0;
};

/// \brief Deterministic single-version package and feature resolver.
class PackageResolver final {
public:
  ZC_NODISCARD static PackageResolutionResult resolve(zc::ArrayPtr<const ResolverRoot> roots,
                                                      zc::ArrayPtr<const ResolverRelease> releases);
  ZC_NODISCARD static PackageResolutionResult resolve(zc::ArrayPtr<const ResolverRoot> roots,
                                                      zc::ArrayPtr<const ResolverRelease> releases,
                                                      PackageResolverMetrics& metrics);
};

}  // namespace zomlang::compiler::driver::package
