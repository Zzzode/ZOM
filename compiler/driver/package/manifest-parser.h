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

#include <cstddef>
#include <cstdint>

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/string.h"
#include "compiler/driver/package/dependency-manifest.h"
#include "compiler/driver/package/manifest-model.h"
#include "compiler/driver/package/source-inventory.h"

namespace zomlang::compiler::driver::package {

/// \brief Closed RFC 0012 manifest failure classification.
enum class ManifestIssue : uint8_t {
  ReadFailed = 0x01,
  InvalidUtf8 = 0x02,
  ByteOrderMarkPresent = 0x03,
  TomlSyntax = 0x04,
  UnknownTable = 0x05,
  UnknownKey = 0x06,
  MissingRequiredKey = 0x07,
  WrongValueType = 0x08,
  InvalidStrongScalar = 0x09,
  UnsupportedEdition = 0x0a,
  InvalidPath = 0x0b,
  PathOutsideRoot = 0x0c,
  DuplicateCanonicalValue = 0x0d,
  WorkspaceMemberMissing = 0x0e,
  NestedWorkspace = 0x0f,
  DuplicateWorkspacePackageName = 0x10,
  TargetCollision = 0x11,
  TargetPathCollision = 0x12,
  MissingTargetPath = 0x13,
  DependencySourceConflict = 0x14,
  InvalidVersionConstraint = 0x15,
  InvalidVcsSelector = 0x16,
  InvalidFeatureEdge = 0x17,
  FeatureCycle = 0x18
};

/// \brief Typed parser-stage failure with an exact original-byte span.
struct ManifestParseError final {
  ManifestIssue issue;
  uint64_t byteStart;
  uint64_t byteEnd;
};

/// \brief Canonically ordered primary and related package diagnostic anchors.
class DiagnosticProvenance final {
public:
  ZC_NODISCARD static zc::Maybe<DiagnosticProvenance> from(DiagnosticAnchor&& primary,
                                                           zc::Vector<DiagnosticAnchor>&& related);

  DiagnosticProvenance(DiagnosticProvenance&&) noexcept = default;
  DiagnosticProvenance& operator=(DiagnosticProvenance&&) noexcept = default;
  ZC_DISALLOW_COPY(DiagnosticProvenance);

  ZC_NODISCARD DiagnosticProvenance clone() const;
  ZC_NODISCARD const DiagnosticAnchor& primary() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const DiagnosticAnchor> related() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  DiagnosticProvenance(DiagnosticAnchor&& primary, zc::Vector<DiagnosticAnchor>&& related) noexcept;

  DiagnosticAnchor primaryValue;
  zc::Vector<DiagnosticAnchor> relatedValues;
};

/// \brief Complete typed RFC 0012 manifest failure.
class ManifestFailure final {
public:
  ZC_NODISCARD static ManifestFailure invalid(DiagnosticProvenance&& provenance,
                                              ManifestIssue issue);

  ManifestFailure(ManifestFailure&&) noexcept = default;
  ManifestFailure& operator=(ManifestFailure&&) noexcept = default;
  ZC_DISALLOW_COPY(ManifestFailure);

  ZC_NODISCARD ManifestFailure clone() const;
  ZC_NODISCARD const DiagnosticProvenance& provenance() const noexcept;
  ZC_NODISCARD ManifestIssue issue() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  ManifestFailure(DiagnosticProvenance&& provenance, ManifestIssue issue) noexcept;

  DiagnosticProvenance provenanceValue;
  ManifestIssue issueValue;
};

/// \brief Owning normalized RFC 0012 manifest record.
class NormalizedManifest final {
public:
  ~NormalizedManifest() noexcept(false);
  NormalizedManifest(NormalizedManifest&&) noexcept;
  NormalizedManifest& operator=(NormalizedManifest&&) noexcept;
  ZC_DISALLOW_COPY(NormalizedManifest);

  ZC_NODISCARD NormalizedManifest clone() const;

  /// \brief Returns whether the document contains `[package]`.
  ZC_NODISCARD bool hasPackage() const noexcept;

  /// \brief Returns whether the document contains `[workspace]`.
  ZC_NODISCARD bool hasWorkspace() const noexcept;

  /// \brief Returns the validated package name.
  /// \pre `hasPackage()` is true.
  ZC_NODISCARD zc::StringPtr packageName() const noexcept;

  /// \brief Returns the validated complete semantic version.
  /// \pre `hasPackage()` is true.
  ZC_NODISCARD zc::StringPtr packageVersion() const noexcept;

  /// \brief Returns the package edition year.
  /// \pre `hasPackage()` is true.
  ZC_NODISCARD uint32_t editionYear() const noexcept;

  /// \brief Returns the sorted workspace-member count.
  /// \pre `hasWorkspace()` is true.
  ZC_NODISCARD size_t workspaceMemberCount() const noexcept;

  /// \brief Returns canonically sorted explicit workspace member paths.
  /// \pre `hasWorkspace()` is true.
  ZC_NODISCARD zc::ArrayPtr<const identity::CanonicalWorkspaceRelativePath> workspaceMembers()
      const noexcept;

  /// \brief Returns the exact `[package].name` diagnostic origin.
  /// \pre `hasPackage()` is true.
  ZC_NODISCARD const DiagnosticAnchor& packageNameOrigin() const;

  /// \brief Returns the exact `[workspace]` diagnostic origin.
  /// \pre `hasWorkspace()` is true.
  ZC_NODISCARD const DiagnosticAnchor& workspaceOrigin() const;

  /// \brief Returns the source origin for one sorted workspace member.
  /// \pre `index < workspaceMemberCount()`.
  ZC_NODISCARD const DiagnosticAnchor& workspaceMemberOrigin(size_t index) const;

  /// \brief Returns the explicit library target, if present.
  ZC_NODISCARD bool hasLibrary() const noexcept;
  /// \pre `hasLibrary()` is true.
  ZC_NODISCARD const TargetManifest& library() const;

  ZC_NODISCARD zc::ArrayPtr<const TargetManifest> binaries() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const TargetManifest> tests() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const TargetManifest> benchmarks() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const TargetManifest> examples() const noexcept;

  /// \brief Returns whether `[build]` supplied a build-script contract.
  ZC_NODISCARD bool hasBuildScript() const noexcept;
  /// \pre `hasBuildScript()` is true.
  ZC_NODISCARD const BuildScriptManifest& buildScript() const;

  /// \brief Returns normalized target dependencies in canonical byte order.
  ZC_NODISCARD zc::ArrayPtr<const DependencyRequirement> targetDependencies() const noexcept;

  /// \brief Returns normalized development dependencies in canonical byte order.
  ZC_NODISCARD zc::ArrayPtr<const DependencyRequirement> developmentDependencies() const noexcept;

  /// \brief Returns normalized build dependencies in canonical byte order.
  ZC_NODISCARD zc::ArrayPtr<const DependencyRequirement> buildDependencies() const noexcept;

  /// \brief Returns the number of normalized feature definitions.
  ZC_NODISCARD size_t featureCount() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const FeatureManifest> features() const noexcept;

private:
  struct Impl;
  explicit NormalizedManifest(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class ManifestParser;
  friend class CanonicalManifestRecord;
};

/// \brief Complete normalized manifest without document or diagnostic provenance.
class CanonicalManifestRecord final {
public:
  ZC_NODISCARD static CanonicalManifestRecord from(const NormalizedManifest& source);
  /// \brief Constructs a package-only canonical manifest from already sorted resolver fields.
  ZC_NODISCARD static CanonicalManifestRecord forResolver(
      PackageManifest&& package, zc::Maybe<CanonicalTargetManifest>&& library,
      zc::Vector<DependencyRequirementWithoutOrigin>&& targetDependencies,
      zc::Vector<DependencyRequirementWithoutOrigin>&& developmentDependencies,
      zc::Vector<DependencyRequirementWithoutOrigin>&& buildDependencies,
      zc::Vector<CanonicalFeatureManifest>&& features);

  CanonicalManifestRecord(CanonicalManifestRecord&&) noexcept = default;
  CanonicalManifestRecord& operator=(CanonicalManifestRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalManifestRecord);

  ZC_NODISCARD CanonicalManifestRecord clone() const;
  /// \brief Clones this canonical record and all owned storage into `resource`.
  ZC_NODISCARD CanonicalManifestRecord clone(zc::MemoryResource& resource) const;
  ZC_NODISCARD zc::Maybe<const PackageManifest&> package() const noexcept;
  ZC_NODISCARD bool hasLibrary() const noexcept;
  ZC_NODISCARD zc::Maybe<const CanonicalTargetManifest&> library() const noexcept;
  ZC_NODISCARD bool hasBuildScript() const noexcept;
  ZC_NODISCARD zc::Maybe<const CanonicalBuildScriptManifest&> buildScript() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const DependencyRequirementWithoutOrigin> targetDependencies()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const DependencyRequirementWithoutOrigin> developmentDependencies()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const DependencyRequirementWithoutOrigin> buildDependencies()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CanonicalFeatureManifest> features() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  CanonicalManifestRecord(zc::Maybe<PackageManifest>&& package,
                          zc::Maybe<WorkspaceManifest>&& workspace,
                          zc::Maybe<CanonicalTargetManifest>&& library,
                          zc::Vector<CanonicalTargetManifest>&& binaries,
                          zc::Vector<CanonicalTargetManifest>&& tests,
                          zc::Vector<CanonicalTargetManifest>&& benchmarks,
                          zc::Vector<CanonicalTargetManifest>&& examples,
                          zc::Maybe<CanonicalBuildScriptManifest>&& buildScript,
                          zc::Vector<DependencyRequirementWithoutOrigin>&& targetDependencies,
                          zc::Vector<DependencyRequirementWithoutOrigin>&& developmentDependencies,
                          zc::Vector<DependencyRequirementWithoutOrigin>&& buildDependencies,
                          zc::Vector<CanonicalFeatureManifest>&& features) noexcept;

  zc::Maybe<PackageManifest> packageValue;
  zc::Maybe<WorkspaceManifest> workspaceValue;
  zc::Maybe<CanonicalTargetManifest> libraryValue;
  zc::Vector<CanonicalTargetManifest> binaryValues;
  zc::Vector<CanonicalTargetManifest> testValues;
  zc::Vector<CanonicalTargetManifest> benchmarkValues;
  zc::Vector<CanonicalTargetManifest> exampleValues;
  zc::Maybe<CanonicalBuildScriptManifest> buildScriptValue;
  zc::Vector<DependencyRequirementWithoutOrigin> targetDependencyValues;
  zc::Vector<DependencyRequirementWithoutOrigin> developmentDependencyValues;
  zc::Vector<DependencyRequirementWithoutOrigin> buildDependencyValues;
  zc::Vector<CanonicalFeatureManifest> featureValues;
};

using ManifestParseResult = zc::OneOf<NormalizedManifest, ManifestFailure>;
using RawManifestParseResult = zc::OneOf<NormalizedManifest, ManifestParseError>;

/// \brief TOML 1.0 parser for the closed RFC 0012 manifest schema.
class ManifestParser final {
public:
  ManifestParser();
  ~ManifestParser() noexcept(false);
  ManifestParser(ManifestParser&&) noexcept;
  ManifestParser& operator=(ManifestParser&&) noexcept;
  ZC_DISALLOW_COPY(ManifestParser);

  /// \brief Parse one workspace-relative `Zom.toml` document.
  /// \param documentPath Canonical path relative to the workspace root.
  /// \param source Exact original UTF-8 bytes.
  /// \param inventory Immutable regular-file inventory for this package root.
  /// \return An owning normalized record or a typed failure span.
  ZC_NODISCARD ManifestParseResult
  parseWorkspaceManifest(identity::CanonicalWorkspaceRelativePath&& documentPath,
                         zc::StringPtr source, const PackageSourceInventory& inventory) const;

private:
  ZC_NODISCARD RawManifestParseResult
  parseWorkspaceManifestRaw(identity::CanonicalWorkspaceRelativePath&& documentPath,
                            zc::StringPtr source, const PackageSourceInventory& inventory) const;

  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::driver::package
