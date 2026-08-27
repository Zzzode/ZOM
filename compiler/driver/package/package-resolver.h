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
#include "compiler/driver/package/feature-resolver.h"
#include "compiler/driver/package/lockfile.h"
#include "compiler/identity/key/package-key.h"

namespace zomlang::compiler::driver::package {

class LocalPackageRecord;
class VerifiedRegistryReleaseRecord;
class VerifiedVcsPackageRecord;

/// \brief Resolver input for one immutable available package release.
class ResolverRelease final {
public:
  ZC_NODISCARD static ResolverRelease fromRegistry(const VerifiedRegistryReleaseRecord& release);
  ZC_NODISCARD static ResolverRelease fromRegistry(zc::MemoryResource& resource,
                                                   const VerifiedRegistryReleaseRecord& release);
  ZC_NODISCARD static ResolverRelease fromVcs(const VerifiedVcsPackageRecord& release);
  ZC_NODISCARD static ResolverRelease fromVcs(zc::MemoryResource& resource,
                                              const VerifiedVcsPackageRecord& release);
  ZC_NODISCARD static ResolverRelease fromVcs(const VerifiedVcsPackageRecord& release,
                                              PackageSourceConstraint&& acceptedSelector);
  ZC_NODISCARD static ResolverRelease fromVcs(zc::MemoryResource& resource,
                                              const VerifiedVcsPackageRecord& release,
                                              PackageSourceConstraint&& acceptedSelector);
  ZC_NODISCARD static ResolverRelease fromLocal(const LocalPackageRecord& release);
  ZC_NODISCARD static ResolverRelease fromLocal(zc::MemoryResource& resource,
                                                const LocalPackageRecord& release);

  ResolverRelease(ResolverRelease&&) noexcept = default;
  ResolverRelease& operator=(ResolverRelease&&) noexcept = default;
  ZC_DISALLOW_COPY(ResolverRelease);

  ZC_NODISCARD ResolverRelease clone() const;
  ZC_NODISCARD ResolverRelease clone(zc::MemoryResource& resource) const;
  ZC_NODISCARD const PackageSourceConstraint& acceptedSource() const noexcept;
  ZC_NODISCARD const identity::PackageBaseKey& base() const noexcept;
  ZC_NODISCARD const CanonicalManifestRecord& manifest() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& manifestDigest() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& sourceTreeDigest() const noexcept;
  ZC_NODISCARD bool hasArchive() const noexcept;
  /// \pre `hasArchive()` is true.
  ZC_NODISCARD ArchiveFormat archiveFormat() const;
  /// \pre `hasArchive()` is true.
  ZC_NODISCARD const identity::Sha256Digest& archiveDigest() const;
  /// \pre `hasArchive()` is true.
  ZC_NODISCARD const SigningKeyId& signingKey() const;
  ZC_NODISCARD bool yanked() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  ResolverRelease(PackageSourceConstraint&& acceptedSource, identity::PackageBaseKey&& base,
                  CanonicalManifestRecord&& manifest, const identity::Sha256Digest& manifestDigest,
                  const identity::Sha256Digest& sourceTreeDigest,
                  zc::Maybe<ArchiveFormat> archiveFormat,
                  zc::Maybe<identity::Sha256Digest> archiveDigest,
                  zc::Maybe<SigningKeyId> signingKey, bool yanked) noexcept;

  PackageSourceConstraint sourceValue;
  identity::PackageBaseKey baseValue;
  CanonicalManifestRecord manifestValue;
  identity::Sha256Digest manifestDigestValue;
  identity::Sha256Digest sourceTreeDigestValue;
  zc::Maybe<ArchiveFormat> archiveFormatValue;
  zc::Maybe<identity::Sha256Digest> archiveDigestValue;
  zc::Maybe<SigningKeyId> signingKeyValue;
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
  ZC_NODISCARD ResolverRoot clone(zc::MemoryResource& resource) const;
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
  InvalidRoot = 0x06,
  InvalidResolutionOutput = 0x07,
  LockInputMismatch = 0x08
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

/// \brief Immutable source materialization identity for one resolved package.
class SourceViewKey final {
public:
  /// \param source Immutable canonical source identity.
  /// \param sourceTreeDigest Verified digest of the materialized source tree.
  /// \return One complete immutable source-view key.
  ZC_NODISCARD static SourceViewKey from(identity::CanonicalPackageSource&& source,
                                         const identity::Sha256Digest& sourceTreeDigest);

  SourceViewKey(SourceViewKey&&) noexcept = default;
  SourceViewKey& operator=(SourceViewKey&&) noexcept = default;
  ZC_DISALLOW_COPY(SourceViewKey);

  ZC_NODISCARD SourceViewKey clone() const;
  ZC_NODISCARD SourceViewKey clone(zc::MemoryResource& resource) const;
  ZC_NODISCARD const identity::CanonicalPackageSource& source() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& sourceTreeDigest() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  SourceViewKey(identity::CanonicalPackageSource&& source,
                const identity::Sha256Digest& sourceTreeDigest) noexcept;

  identity::CanonicalPackageSource sourceValue;
  identity::Sha256Digest sourceTreeDigestValue;
};

/// \brief One unique resolved package with verified manifest and source material.
class ResolvedPackageRecord final {
public:
  /// \param key Final package identity selected by the resolver.
  /// \param manifest Canonical verified package manifest.
  /// \param manifestDigest Domain-separated digest of `manifest`.
  /// \param sourceTreeDigest Verified digest of the source tree.
  /// \param sourceView Source identity paired with `sourceTreeDigest`.
  /// \param libraryTarget Canonical library target, or none for a binary-only root.
  /// \return A record when every identity and digest relation is valid; otherwise none.
  ZC_NODISCARD static zc::Maybe<ResolvedPackageRecord> from(
      zc::MemoryResource& resource, identity::PackageKey&& key, CanonicalManifestRecord&& manifest,
      const identity::Sha256Digest& manifestDigest, const identity::Sha256Digest& sourceTreeDigest,
      SourceViewKey&& sourceView, zc::Maybe<identity::TargetName> libraryTarget);
  ZC_NODISCARD static zc::Maybe<ResolvedPackageRecord> from(
      identity::PackageKey&& key, CanonicalManifestRecord&& manifest,
      const identity::Sha256Digest& manifestDigest, const identity::Sha256Digest& sourceTreeDigest,
      SourceViewKey&& sourceView, zc::Maybe<identity::TargetName> libraryTarget);

  ResolvedPackageRecord(ResolvedPackageRecord&&) noexcept = default;
  ResolvedPackageRecord& operator=(ResolvedPackageRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(ResolvedPackageRecord);

  ZC_NODISCARD ResolvedPackageRecord clone() const;
  ZC_NODISCARD ResolvedPackageRecord clone(zc::MemoryResource& resource) const;
  ZC_NODISCARD const identity::PackageKey& key() const noexcept;
  ZC_NODISCARD const CanonicalManifestRecord& manifest() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& manifestDigest() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& sourceTreeDigest() const noexcept;
  ZC_NODISCARD const SourceViewKey& sourceView() const noexcept;
  ZC_NODISCARD zc::Maybe<const identity::TargetName&> libraryTarget() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  ResolvedPackageRecord(identity::PackageKey&& key, CanonicalManifestRecord&& manifest,
                        const identity::Sha256Digest& manifestDigest,
                        const identity::Sha256Digest& sourceTreeDigest, SourceViewKey&& sourceView,
                        zc::Maybe<identity::TargetName> libraryTarget) noexcept;

  identity::PackageKey keyValue;
  CanonicalManifestRecord manifestValue;
  identity::Sha256Digest manifestDigestValue;
  identity::Sha256Digest sourceTreeDigestValue;
  SourceViewKey sourceViewValue;
  zc::Maybe<identity::TargetName> libraryTargetValue;
};

/// \brief Feature closure for one package activation domain.
class ResolvedFeatureSet final {
public:
  /// \param base Selected package coordinate before feature activation.
  /// \param domain Independently expanded target or build domain.
  /// \param features Canonically sorted feature closure.
  /// \return A feature set for a valid closed activation domain; otherwise none.
  ZC_NODISCARD static zc::Maybe<ResolvedFeatureSet> from(identity::PackageBaseKey&& base,
                                                         FeatureActivationDomain domain,
                                                         identity::SortedFeatureSet&& features);

  ResolvedFeatureSet(ResolvedFeatureSet&&) noexcept = default;
  ResolvedFeatureSet& operator=(ResolvedFeatureSet&&) noexcept = default;
  ZC_DISALLOW_COPY(ResolvedFeatureSet);

  ZC_NODISCARD ResolvedFeatureSet clone() const;
  ZC_NODISCARD ResolvedFeatureSet clone(zc::MemoryResource& resource) const;
  ZC_NODISCARD const identity::PackageBaseKey& base() const noexcept;
  ZC_NODISCARD FeatureActivationDomain domain() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::FeatureName> features() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  ResolvedFeatureSet(identity::PackageBaseKey&& base, FeatureActivationDomain domain,
                     identity::SortedFeatureSet&& features) noexcept;

  identity::PackageBaseKey baseValue;
  FeatureActivationDomain domainValue;
  identity::SortedFeatureSet featureValues;
};

enum class ResolutionOutputIssue : uint8_t {
  EmptyPackages = 0x01,
  DuplicatePackage = 0x02,
  DuplicateEdge = 0x03,
  DuplicateFeatureSet = 0x04,
  DanglingEdge = 0x05,
  MissingPackageRecord = 0x06,
  LockGraphMismatch = 0x07
};

/// \brief Canonical resolver authority output with an exact verified lock projection.
class ResolutionOutput final {
public:
  ResolutionOutput(ResolutionOutput&&) noexcept = default;
  ResolutionOutput& operator=(ResolutionOutput&&) noexcept = default;
  ZC_DISALLOW_COPY(ResolutionOutput);

  /// \return Canonically sorted unique resolved package records.
  ZC_NODISCARD zc::ArrayPtr<const ResolvedPackageRecord> packages() const noexcept;
  /// \return Canonically sorted unique dependency edges.
  ZC_NODISCARD zc::ArrayPtr<const identity::PackageDependencyEdgeKey> edges() const noexcept;
  /// \return Canonically sorted unique package-domain feature closures.
  ZC_NODISCARD zc::ArrayPtr<const ResolvedFeatureSet> featureSets() const noexcept;
  /// \return Exact verified lock projection emitted with this resolution.
  ZC_NODISCARD const VerifiedLockGraph& lockGraph() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;
  ZC_NODISCARD zc::Array<uint8_t> encode(zc::MemoryResource& resource) const;

private:
  friend class PackageResolver;
  /// \param packages Unique resolved package records produced by the resolver.
  /// \param edges Unique closed dependency edges produced by the resolver.
  /// \param featureSets Unique package-domain feature closures.
  /// \param lockGraph Exact verified projection of `packages` and `edges`.
  /// \return Canonical output, or the first structural invariant violation.
  ZC_NODISCARD static zc::OneOf<ResolutionOutput, ResolutionOutputIssue> from(
      zc::MemoryResource& resource, zc::Vector<ResolvedPackageRecord>&& packages,
      zc::Vector<identity::PackageDependencyEdgeKey>&& edges,
      zc::Vector<ResolvedFeatureSet>&& featureSets, VerifiedLockGraph&& lockGraph);

  ResolutionOutput(zc::Vector<ResolvedPackageRecord>&& packages,
                   zc::Vector<identity::PackageDependencyEdgeKey>&& edges,
                   zc::Vector<ResolvedFeatureSet>&& featureSets,
                   VerifiedLockGraph&& lockGraph) noexcept;

  zc::Vector<ResolvedPackageRecord> packageValues;
  zc::Vector<identity::PackageDependencyEdgeKey> edgeValues;
  zc::Vector<ResolvedFeatureSet> featureSetValues;
  VerifiedLockGraph lockGraphValue;
};

using ResolutionResult = zc::OneOf<ResolutionOutput, PackageResolverFailure>;

/// \brief Deterministic operation counts for resolver performance gates.
struct PackageResolverMetrics final {
  uint64_t decisions = 0;
  uint64_t selectedPackages = 0;
  uint64_t emittedEdges = 0;
};

/// \brief Deterministic single-version package and feature resolver.
class PackageResolver final {
public:
  ZC_NODISCARD static ResolutionResult resolve(zc::MemoryResource& resource,
                                               zc::ArrayPtr<const ResolverRoot> roots,
                                               zc::ArrayPtr<const ResolverRelease> releases);
  ZC_NODISCARD static ResolutionResult resolve(zc::MemoryResource& resource,
                                               zc::ArrayPtr<const ResolverRoot> roots,
                                               zc::ArrayPtr<const ResolverRelease> releases,
                                               PackageResolverMetrics& metrics);
  /// \param roots Current verified root and target-domain requests.
  /// \param releases Source-verified releases available without remote discovery.
  /// \param locked Canonical lock graph whose exact selection must be retained.
  /// \param metrics Deterministic replay work counters; solver invocations remain zero.
  /// \return Exact resolution output, or a structural resolver failure.
  ZC_NODISCARD static ResolutionResult resolveLocked(zc::MemoryResource& resource,
                                                     zc::ArrayPtr<const ResolverRoot> roots,
                                                     zc::ArrayPtr<const ResolverRelease> releases,
                                                     const VerifiedLockGraph& locked,
                                                     LockReplayMetrics& metrics);
};

}  // namespace zomlang::compiler::driver::package
