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
#include "zomlang/compiler/identity/key/crate-key.h"
#include "zomlang/compiler/identity/key/package-key.h"
#include "zomlang/compiler/identity/semantic/semantic-version.h"
#include "zomlang/compiler/identity/crypto/sha256.h"

namespace zomlang::compiler::identity {
class CanonicalEncoder;
}

namespace zomlang::compiler::driver::package {

enum class InputDocumentKind : uint8_t { Manifest = 0x01, Lock = 0x02 };
enum class DiagnosticDocumentPathKind : uint8_t { Workspace = 0x01, Package = 0x02 };

struct WorkspaceDiagnosticDocumentPath final {
  identity::CanonicalWorkspaceRelativePath path;
};

struct PackageDiagnosticDocumentPath final {
  identity::Sha256Digest sourceDigest;
  identity::CanonicalRelativePath relativePath;
};

/// \brief Host-path-free document identity used by package diagnostics.
class DiagnosticDocumentPath final {
public:
  ZC_NODISCARD static DiagnosticDocumentPath workspace(
      identity::CanonicalWorkspaceRelativePath&& path);
  ZC_NODISCARD static DiagnosticDocumentPath package(
      const identity::Sha256Digest& sourceDigest, identity::CanonicalRelativePath&& relativePath);

  DiagnosticDocumentPath(DiagnosticDocumentPath&&) noexcept = default;
  DiagnosticDocumentPath& operator=(DiagnosticDocumentPath&&) noexcept = default;
  ZC_DISALLOW_COPY(DiagnosticDocumentPath);

  ZC_NODISCARD DiagnosticDocumentPath clone() const;
  ZC_NODISCARD DiagnosticDocumentPathKind kind() const noexcept;
  /// \pre `kind() == DiagnosticDocumentPathKind::Workspace`.
  ZC_NODISCARD const identity::CanonicalWorkspaceRelativePath& workspacePath() const;
  /// \pre `kind() == DiagnosticDocumentPathKind::Package`.
  ZC_NODISCARD const identity::Sha256Digest& packageSourceDigest() const;
  /// \pre `kind() == DiagnosticDocumentPathKind::Package`.
  ZC_NODISCARD const identity::CanonicalRelativePath& packageRelativePath() const;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit DiagnosticDocumentPath(WorkspaceDiagnosticDocumentPath&& path) noexcept;
  explicit DiagnosticDocumentPath(PackageDiagnosticDocumentPath&& path) noexcept;

  zc::OneOf<WorkspaceDiagnosticDocumentPath, PackageDiagnosticDocumentPath> value;
};

/// \brief Stable identity for one exact manifest or lock document.
class InputDocumentKey final {
public:
  ZC_NODISCARD static zc::Maybe<InputDocumentKey> from(InputDocumentKind kind,
                                                       DiagnosticDocumentPath&& path,
                                                       const identity::Sha256Digest& contentDigest);

  InputDocumentKey(InputDocumentKey&&) noexcept = default;
  InputDocumentKey& operator=(InputDocumentKey&&) noexcept = default;
  ZC_DISALLOW_COPY(InputDocumentKey);

  ZC_NODISCARD InputDocumentKey clone() const;
  ZC_NODISCARD InputDocumentKind kind() const noexcept;
  ZC_NODISCARD const DiagnosticDocumentPath& path() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& contentDigest() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  InputDocumentKey(InputDocumentKind kind, DiagnosticDocumentPath&& path,
                   const identity::Sha256Digest& contentDigest) noexcept;

  InputDocumentKind kindValue;
  DiagnosticDocumentPath pathValue;
  identity::Sha256Digest digestValue;
};

/// \brief Exact byte range in one identified manifest document.
class ManifestSpan final {
public:
  ZC_NODISCARD static zc::Maybe<ManifestSpan> from(InputDocumentKey&& document,
                                                   uint64_t documentByteLength, uint64_t byteStart,
                                                   uint64_t byteEnd);

  ManifestSpan(ManifestSpan&&) noexcept = default;
  ManifestSpan& operator=(ManifestSpan&&) noexcept = default;
  ZC_DISALLOW_COPY(ManifestSpan);

  ZC_NODISCARD ManifestSpan clone() const;
  ZC_NODISCARD const InputDocumentKey& document() const noexcept;
  ZC_NODISCARD uint64_t byteStart() const noexcept;
  ZC_NODISCARD uint64_t byteEnd() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  ManifestSpan(InputDocumentKey&& document, uint64_t byteStart, uint64_t byteEnd) noexcept;

  InputDocumentKey documentValue;
  uint64_t startValue;
  uint64_t endValue;
};

enum class DiagnosticAnchorKind : uint8_t { Manifest = 0x01 };

/// \brief Closed typed origin for package-pipeline diagnostics.
class DiagnosticAnchor final {
public:
  ZC_NODISCARD static DiagnosticAnchor manifest(ManifestSpan&& span);

  DiagnosticAnchor(DiagnosticAnchor&&) noexcept = default;
  DiagnosticAnchor& operator=(DiagnosticAnchor&&) noexcept = default;
  ZC_DISALLOW_COPY(DiagnosticAnchor);

  ZC_NODISCARD DiagnosticAnchor clone() const;
  ZC_NODISCARD DiagnosticAnchorKind kind() const noexcept;
  /// \pre `kind() == DiagnosticAnchorKind::Manifest`.
  ZC_NODISCARD const ManifestSpan& manifestSpan() const;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit DiagnosticAnchor(ManifestSpan&& span) noexcept;

  zc::OneOf<ManifestSpan> value;
};

/// \brief Validated `[package]` manifest fields.
class PackageManifest final {
public:
  ZC_NODISCARD static PackageManifest from(identity::PackageName&& name,
                                           identity::ResolvedVersion&& version,
                                           uint32_t editionYear);

  PackageManifest(PackageManifest&&) noexcept = default;
  PackageManifest& operator=(PackageManifest&&) noexcept = default;
  ZC_DISALLOW_COPY(PackageManifest);

  ZC_NODISCARD PackageManifest clone() const;
  /// \brief Clones this package record and all owned storage into `resource`.
  ZC_NODISCARD PackageManifest clone(zc::MemoryResource& resource) const;
  ZC_NODISCARD zc::StringPtr name() const noexcept;
  ZC_NODISCARD zc::StringPtr version() const noexcept;
  ZC_NODISCARD uint32_t editionYear() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  PackageManifest(identity::PackageName&& name, identity::ResolvedVersion&& version,
                  uint32_t editionYear) noexcept;

  identity::PackageName nameValue;
  identity::ResolvedVersion versionValue;
  uint32_t editionYearValue;
};

/// \brief Canonically sorted unique `[workspace].members`.
class WorkspaceManifest final {
public:
  ZC_NODISCARD static zc::Maybe<WorkspaceManifest> from(
      zc::Vector<identity::CanonicalWorkspaceRelativePath>&& members);

  WorkspaceManifest(WorkspaceManifest&&) noexcept = default;
  WorkspaceManifest& operator=(WorkspaceManifest&&) noexcept = default;
  ZC_DISALLOW_COPY(WorkspaceManifest);

  ZC_NODISCARD WorkspaceManifest clone() const;
  /// \brief Clones this workspace record and all owned storage into `resource`.
  ZC_NODISCARD WorkspaceManifest clone(zc::MemoryResource& resource) const;
  ZC_NODISCARD zc::ArrayPtr<const identity::CanonicalWorkspaceRelativePath> members()
      const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit WorkspaceManifest(
      zc::Vector<identity::CanonicalWorkspaceRelativePath>&& members) noexcept;

  zc::Vector<identity::CanonicalWorkspaceRelativePath> memberValues;
};

/// \brief One normalized manifest target with source provenance.
class TargetManifest final {
public:
  ZC_NODISCARD static zc::Maybe<TargetManifest> from(identity::CrateTargetKind kind,
                                                     identity::TargetName&& name,
                                                     identity::CanonicalRelativePath&& path,
                                                     bool implicit, DiagnosticAnchor&& origin);

  TargetManifest(TargetManifest&&) noexcept = default;
  TargetManifest& operator=(TargetManifest&&) noexcept = default;
  ZC_DISALLOW_COPY(TargetManifest);

  ZC_NODISCARD TargetManifest clone() const;
  ZC_NODISCARD identity::CrateTargetKind kind() const noexcept;
  ZC_NODISCARD zc::StringPtr name() const noexcept;
  ZC_NODISCARD const identity::CanonicalRelativePath& path() const noexcept;
  ZC_NODISCARD bool implicit() const noexcept;
  /// \brief Returns the retained manifest origin of this normalized target.
  ZC_NODISCARD const DiagnosticAnchor& origin() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  TargetManifest(identity::CrateTargetKind kind, identity::TargetName&& name,
                 identity::CanonicalRelativePath&& path, bool implicit,
                 DiagnosticAnchor&& origin) noexcept;

  identity::CrateTargetKind kindValue;
  identity::TargetName nameValue;
  identity::CanonicalRelativePath pathValue;
  bool implicitValue;
  DiagnosticAnchor originValue;
};

/// \brief Target fields used by signed canonical manifest records.
class CanonicalTargetManifest final {
public:
  ZC_NODISCARD static zc::Maybe<CanonicalTargetManifest> from(
      identity::CrateTargetKind kind, identity::TargetName&& name,
      identity::CanonicalRelativePath&& path, bool implicit);
  ZC_NODISCARD static CanonicalTargetManifest from(const TargetManifest& target);

  CanonicalTargetManifest(CanonicalTargetManifest&&) noexcept = default;
  CanonicalTargetManifest& operator=(CanonicalTargetManifest&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalTargetManifest);

  ZC_NODISCARD CanonicalTargetManifest clone() const;
  /// \brief Clones this target and all owned storage into `resource`.
  ZC_NODISCARD CanonicalTargetManifest clone(zc::MemoryResource& resource) const;
  ZC_NODISCARD identity::CrateTargetKind kind() const noexcept;
  ZC_NODISCARD zc::StringPtr name() const noexcept;
  ZC_NODISCARD const identity::CanonicalRelativePath& path() const noexcept;
  ZC_NODISCARD bool implicit() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  CanonicalTargetManifest(identity::CrateTargetKind kind, identity::TargetName&& name,
                          identity::CanonicalRelativePath&& path, bool implicit) noexcept;

  identity::CrateTargetKind kindValue;
  identity::TargetName nameValue;
  identity::CanonicalRelativePath pathValue;
  bool implicitValue;
};

/// \brief Normalized build-script target and declared capability inputs.
class BuildScriptManifest final {
public:
  ZC_NODISCARD static zc::Maybe<BuildScriptManifest> from(
      TargetManifest&& target, zc::Vector<identity::CanonicalRelativePath>&& inputs,
      zc::Vector<identity::CanonicalRelativePath>&& outputs,
      zc::Vector<identity::SemanticEnvironmentName>&& environment,
      zc::Vector<identity::SemanticEnvironmentName>&& exportedEnvironment);

  BuildScriptManifest(BuildScriptManifest&&) noexcept = default;
  BuildScriptManifest& operator=(BuildScriptManifest&&) noexcept = default;
  ZC_DISALLOW_COPY(BuildScriptManifest);

  ZC_NODISCARD BuildScriptManifest clone() const;
  ZC_NODISCARD const TargetManifest& target() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::CanonicalRelativePath> inputs() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::CanonicalRelativePath> outputs() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::SemanticEnvironmentName> environment() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::SemanticEnvironmentName> exportedEnvironment()
      const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  BuildScriptManifest(TargetManifest&& target, zc::Vector<identity::CanonicalRelativePath>&& inputs,
                      zc::Vector<identity::CanonicalRelativePath>&& outputs,
                      zc::Vector<identity::SemanticEnvironmentName>&& environment,
                      zc::Vector<identity::SemanticEnvironmentName>&& exportedEnvironment) noexcept;

  TargetManifest targetValue;
  zc::Vector<identity::CanonicalRelativePath> inputValues;
  zc::Vector<identity::CanonicalRelativePath> outputValues;
  zc::Vector<identity::SemanticEnvironmentName> environmentValues;
  zc::Vector<identity::SemanticEnvironmentName> exportedEnvironmentValues;
};

/// \brief Build-script fields used by signed canonical manifest records.
class CanonicalBuildScriptManifest final {
public:
  ZC_NODISCARD static CanonicalBuildScriptManifest from(const BuildScriptManifest& source);

  CanonicalBuildScriptManifest(CanonicalBuildScriptManifest&&) noexcept = default;
  CanonicalBuildScriptManifest& operator=(CanonicalBuildScriptManifest&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalBuildScriptManifest);

  ZC_NODISCARD CanonicalBuildScriptManifest clone() const;
  /// \brief Clones this build-script record and all owned storage into `resource`.
  ZC_NODISCARD CanonicalBuildScriptManifest clone(zc::MemoryResource& resource) const;
  ZC_NODISCARD const CanonicalTargetManifest& target() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::CanonicalRelativePath> inputs() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::CanonicalRelativePath> outputs() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::SemanticEnvironmentName> environment() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::SemanticEnvironmentName> exportedEnvironment()
      const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  CanonicalBuildScriptManifest(
      CanonicalTargetManifest&& target, zc::Vector<identity::CanonicalRelativePath>&& inputs,
      zc::Vector<identity::CanonicalRelativePath>&& outputs,
      zc::Vector<identity::SemanticEnvironmentName>&& environment,
      zc::Vector<identity::SemanticEnvironmentName>&& exportedEnvironment) noexcept;

  CanonicalTargetManifest targetValue;
  zc::Vector<identity::CanonicalRelativePath> inputValues;
  zc::Vector<identity::CanonicalRelativePath> outputValues;
  zc::Vector<identity::SemanticEnvironmentName> environmentValues;
  zc::Vector<identity::SemanticEnvironmentName> exportedEnvironmentValues;
};

/// \brief Sorts target records by origin-free canonical bytes and rejects duplicates.
ZC_NODISCARD bool sortTargetManifests(zc::Vector<TargetManifest>& targets);

enum class FeatureEdgeKind : uint8_t {
  Local = 0x01,
  EnableDependency = 0x02,
  EnableDependencyFeature = 0x03
};

struct LocalFeatureEdge final {
  identity::FeatureName feature;
};

struct EnableDependencyEdge final {
  identity::DependencyAlias dependency;
};

struct EnableDependencyFeatureEdge final {
  identity::DependencyAlias dependency;
  identity::FeatureName feature;
};

/// \brief Closed normalized feature activation edge.
class FeatureEdge final {
public:
  ZC_NODISCARD static FeatureEdge local(identity::FeatureName&& feature);
  ZC_NODISCARD static FeatureEdge enableDependency(identity::DependencyAlias&& dependency);
  ZC_NODISCARD static FeatureEdge enableDependencyFeature(identity::DependencyAlias&& dependency,
                                                          identity::FeatureName&& feature);

  FeatureEdge(FeatureEdge&&) noexcept = default;
  FeatureEdge& operator=(FeatureEdge&&) noexcept = default;
  ZC_DISALLOW_COPY(FeatureEdge);

  ZC_NODISCARD FeatureEdge clone() const;
  /// \brief Clones this edge and all owned storage into `resource`.
  ZC_NODISCARD FeatureEdge clone(zc::MemoryResource& resource) const;
  ZC_NODISCARD FeatureEdgeKind kind() const noexcept;
  /// \pre `kind() == FeatureEdgeKind::Local`.
  ZC_NODISCARD zc::StringPtr localFeature() const;
  /// \pre `kind() != FeatureEdgeKind::Local`.
  ZC_NODISCARD zc::StringPtr dependencyAlias() const;
  /// \pre `kind() == FeatureEdgeKind::EnableDependencyFeature`.
  ZC_NODISCARD zc::StringPtr dependencyFeature() const;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit FeatureEdge(LocalFeatureEdge&& edge) noexcept;
  explicit FeatureEdge(EnableDependencyEdge&& edge) noexcept;
  explicit FeatureEdge(EnableDependencyFeatureEdge&& edge) noexcept;

  zc::OneOf<LocalFeatureEdge, EnableDependencyEdge, EnableDependencyFeatureEdge> value;
};

/// \brief Feature edge paired with its exact diagnostic origin.
class FeatureEdgeRecord final {
public:
  ZC_NODISCARD static FeatureEdgeRecord from(FeatureEdge&& edge, DiagnosticAnchor&& origin);

  FeatureEdgeRecord(FeatureEdgeRecord&&) noexcept = default;
  FeatureEdgeRecord& operator=(FeatureEdgeRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(FeatureEdgeRecord);

  ZC_NODISCARD FeatureEdgeRecord clone() const;
  ZC_NODISCARD const FeatureEdge& edge() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  FeatureEdgeRecord(FeatureEdge&& edge, DiagnosticAnchor&& origin) noexcept;

  FeatureEdge edgeValue;
  DiagnosticAnchor originValue;
};

/// \brief One feature name and its canonically sorted unique outgoing edges.
class FeatureManifest final {
public:
  ZC_NODISCARD static zc::Maybe<FeatureManifest> from(identity::FeatureName&& name,
                                                      zc::Vector<FeatureEdgeRecord>&& edges);

  FeatureManifest(FeatureManifest&&) noexcept = default;
  FeatureManifest& operator=(FeatureManifest&&) noexcept = default;
  ZC_DISALLOW_COPY(FeatureManifest);

  ZC_NODISCARD FeatureManifest clone() const;
  ZC_NODISCARD zc::StringPtr name() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const FeatureEdgeRecord> edges() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  FeatureManifest(identity::FeatureName&& name, zc::Vector<FeatureEdgeRecord>&& edges) noexcept;

  identity::FeatureName nameValue;
  zc::Vector<FeatureEdgeRecord> edgeValues;
};

/// \brief Feature definition without diagnostic provenance.
class CanonicalFeatureManifest final {
public:
  ZC_NODISCARD static CanonicalFeatureManifest from(const FeatureManifest& source);

  CanonicalFeatureManifest(CanonicalFeatureManifest&&) noexcept = default;
  CanonicalFeatureManifest& operator=(CanonicalFeatureManifest&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalFeatureManifest);

  ZC_NODISCARD CanonicalFeatureManifest clone() const;
  /// \brief Clones this feature and all owned storage into `resource`.
  ZC_NODISCARD CanonicalFeatureManifest clone(zc::MemoryResource& resource) const;
  ZC_NODISCARD zc::StringPtr name() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const FeatureEdge> edges() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  CanonicalFeatureManifest(identity::FeatureName&& name, zc::Vector<FeatureEdge>&& edges) noexcept;

  identity::FeatureName nameValue;
  zc::Vector<FeatureEdge> edgeValues;
};

}  // namespace zomlang::compiler::driver::package
