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
#include "zc/core/vector.h"
#include "compiler/diagnostics/toolchain/module-root-argument.h"
#include "compiler/driver/package/workspace-normalizer.h"
#include "compiler/identity/key/crate-key.h"
#include "compiler/identity/key/package-key.h"
#include "compiler/identity/crypto/sha256.h"
#include "compiler/identity/sorted-feature-set.h"

namespace zomlang::compiler::ir {
class TargetRegistrySnapshot;
}

namespace zomlang::compiler::driver::package {

class VerifiedBuildScriptResultSet;
class VerifiedBuildScriptPlan;

enum class InvocationIssue : uint8_t {
  ManifestNotFound = 0x01,
  InvalidManifestPath = 0x02,
  MissingPackageSelection = 0x03,
  DuplicatePackageSelection = 0x04,
  MissingTargetSelection = 0x05,
  DuplicateTargetSelection = 0x06,
  PositionalSourceArgument = 0x07,
  InvalidFeatureList = 0x08,
  ConflictingLockMode = 0x09,
  UnknownTargetProfile = 0x0a,
  InvalidPanicStrategy = 0x0b,
};

enum class PackageLockMode : uint8_t { LockedOnly = 0x01, PreferLocked = 0x02, Update = 0x03 };
enum class PackagePanicStrategy : uint8_t { Abort = 0x01, Unwind = 0x02 };

struct SelectedLanguageOptions final {
  bool useUnicode = true;
  bool allowDollarIdentifiers = false;
  bool supportRegexLiterals = true;

  void encode(identity::CanonicalEncoder& encoder) const;
};

/// \brief Validated immutable registered-target profile name.
class RegisteredTargetProfileName final {
public:
  ZC_NODISCARD static zc::Maybe<RegisteredTargetProfileName> from(zc::StringPtr text);

  RegisteredTargetProfileName(RegisteredTargetProfileName&&) noexcept = default;
  RegisteredTargetProfileName& operator=(RegisteredTargetProfileName&&) noexcept = default;
  ZC_DISALLOW_COPY(RegisteredTargetProfileName);

  ZC_NODISCARD RegisteredTargetProfileName clone() const;
  ZC_NODISCARD zc::StringPtr text() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  explicit RegisteredTargetProfileName(zc::String&& value) noexcept;
  zc::String value;
};

/// \brief Package-layer token issued only by the immutable target registry.
class RegisteredTargetSelection final {
public:
  RegisteredTargetSelection(RegisteredTargetSelection&&) noexcept = default;
  RegisteredTargetSelection& operator=(RegisteredTargetSelection&&) noexcept = default;
  ZC_DISALLOW_COPY(RegisteredTargetSelection);

  ZC_NODISCARD RegisteredTargetSelection clone() const;
  ZC_NODISCARD const identity::Sha256Digest& registryRevision() const noexcept;
  ZC_NODISCARD zc::StringPtr profile() const noexcept;
  ZC_NODISCARD const identity::CanonicalTargetSpecificationKey& semanticProjection() const noexcept;
  ZC_NODISCARD PackagePanicStrategy panicStrategy() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  RegisteredTargetSelection(const identity::Sha256Digest& revision,
                            RegisteredTargetProfileName&& profile,
                            identity::CanonicalTargetSpecificationKey&& projection,
                            PackagePanicStrategy panic) noexcept;

  identity::Sha256Digest revisionValue;
  RegisteredTargetProfileName profileValue;
  identity::CanonicalTargetSpecificationKey projectionValue;
  PackagePanicStrategy panicValue;

  friend class RegisteredTargetService;
};

/// \brief One immutable target profile admitted during compiler construction.
class RegisteredTargetProfile final {
public:
  ZC_NODISCARD static RegisteredTargetProfile from(
      RegisteredTargetProfileName&& name,
      identity::CanonicalTargetSpecificationKey&& semanticProjection);

  RegisteredTargetProfile(RegisteredTargetProfile&&) noexcept = default;
  RegisteredTargetProfile& operator=(RegisteredTargetProfile&&) noexcept = default;
  ZC_DISALLOW_COPY(RegisteredTargetProfile);

  ZC_NODISCARD zc::StringPtr name() const noexcept;
  ZC_NODISCARD const identity::CanonicalTargetSpecificationKey& semanticProjection() const noexcept;

private:
  RegisteredTargetProfile(RegisteredTargetProfileName&& name,
                          identity::CanonicalTargetSpecificationKey&& projection) noexcept;
  RegisteredTargetProfileName nameValue;
  identity::CanonicalTargetSpecificationKey projectionValue;
  friend class RegisteredTargetService;
};

/// \brief Immutable profile lookup service with one compiler-owned host profile.
class RegisteredTargetService final {
public:
  RegisteredTargetService(RegisteredTargetService&&) noexcept = default;
  RegisteredTargetService& operator=(RegisteredTargetService&&) noexcept = default;
  ZC_DISALLOW_COPY(RegisteredTargetService);

  ZC_NODISCARD zc::Maybe<RegisteredTargetSelection> select(
      zc::Maybe<zc::StringPtr> requestedProfile, PackagePanicStrategy panic) const;
  ZC_NODISCARD const identity::Sha256Digest& revision() const noexcept;
  ZC_NODISCARD zc::StringPtr hostProfile() const noexcept;

private:
  ZC_NODISCARD static zc::Maybe<RegisteredTargetService> fromVerifiedRegistry(
      const identity::Sha256Digest& revision, RegisteredTargetProfileName&& hostProfile,
      zc::Vector<RegisteredTargetProfile>&& profiles);
  RegisteredTargetService(RegisteredTargetProfileName&& hostProfile,
                          zc::Vector<RegisteredTargetProfile>&& profiles,
                          const identity::Sha256Digest& revision) noexcept;
  RegisteredTargetProfileName hostProfileValue;
  zc::Vector<RegisteredTargetProfile> profileValues;
  identity::Sha256Digest revisionValue;
  friend class ::zomlang::compiler::ir::TargetRegistrySnapshot;
};

struct RequestedTargetSelection final {
  identity::CrateTargetKind kind;
  zc::Maybe<identity::TargetName> name;

  RequestedTargetSelection(identity::CrateTargetKind kind,
                           zc::Maybe<identity::TargetName>&& name) noexcept;
  RequestedTargetSelection(RequestedTargetSelection&&) noexcept = default;
  RequestedTargetSelection& operator=(RequestedTargetSelection&&) noexcept = default;
  ZC_DISALLOW_COPY(RequestedTargetSelection);
  ZC_NODISCARD RequestedTargetSelection clone() const;
  void encode(identity::CanonicalEncoder& encoder) const;
};

/// \brief Lossless pre-normalization command facts collected by the CLI.
struct RawPackageCompilationRequest final {
  zc::Vector<zc::String> packageSelections;
  zc::Vector<RequestedTargetSelection> targetSelections;
  zc::Vector<zc::String> featureLists;
  zc::Vector<zc::String> positionalArguments;
  zc::Vector<zc::String> targetProfiles;
  uint32_t lockedCount = 0;
  uint32_t updateLockCount = 0;
  uint32_t panicCount = 0;
  zc::String panicStrategy = zc::str("abort");
  bool useDefaultFeatures = true;
  SelectedLanguageOptions languageOptions;
};

class NormalizedPackageCompilationRequest final {
public:
  NormalizedPackageCompilationRequest(NormalizedPackageCompilationRequest&&) noexcept = default;
  NormalizedPackageCompilationRequest& operator=(NormalizedPackageCompilationRequest&&) noexcept =
      default;
  ZC_DISALLOW_COPY(NormalizedPackageCompilationRequest);

  ZC_NODISCARD zc::StringPtr package() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const RequestedTargetSelection> requestedTargets() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::FeatureName> requestedFeatures() const noexcept;
  ZC_NODISCARD bool useDefaultFeatures() const noexcept;
  ZC_NODISCARD const RegisteredTargetSelection& hostTarget() const noexcept;
  ZC_NODISCARD const RegisteredTargetSelection& target() const noexcept;
  ZC_NODISCARD const SelectedLanguageOptions& languageOptions() const noexcept;
  ZC_NODISCARD PackageLockMode lockMode() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  NormalizedPackageCompilationRequest(
      identity::PackageName&& package, zc::Vector<RequestedTargetSelection>&& targets,
      identity::SortedFeatureSet&& features, bool useDefaultFeatures,
      RegisteredTargetSelection&& hostTarget, RegisteredTargetSelection&& target,
      SelectedLanguageOptions languageOptions, PackageLockMode lockMode) noexcept;
  identity::PackageName packageValue;
  zc::Vector<RequestedTargetSelection> targetValues;
  identity::SortedFeatureSet featureValues;
  bool defaultFeatureValue;
  RegisteredTargetSelection hostTargetValue;
  RegisteredTargetSelection targetValue;
  SelectedLanguageOptions languageOptionValues;
  PackageLockMode lockModeValue;

  friend zc::OneOf<NormalizedPackageCompilationRequest, InvocationIssue>
  normalizePackageCompilationRequest(RawPackageCompilationRequest&&,
                                     const RegisteredTargetService&);
};

using PackageCompilationNormalizationResult =
    zc::OneOf<NormalizedPackageCompilationRequest, InvocationIssue>;

ZC_NODISCARD PackageCompilationNormalizationResult normalizePackageCompilationRequest(
    RawPackageCompilationRequest&& raw, const RegisteredTargetService& targets);

enum class TargetSelectionIssue : uint8_t {
  UnknownWorkspacePackage = 0x01,
  UnknownTarget = 0x02,
  UnknownRootFeature = 0x03,
};

/// \brief Closed package producers that may claim the compiler-reserved module root.
enum class PackageToolchainModuleRootProducer : uint8_t {
  UserTargetRoot = 0x01,
  DependencyAlias = 0x02,
};

/// \brief Canonical normalized-manifest field path for one reserved-root occurrence.
class PackageToolchainModuleRootFieldPath final {
public:
  PackageToolchainModuleRootFieldPath(PackageToolchainModuleRootFieldPath&&) noexcept = default;
  PackageToolchainModuleRootFieldPath& operator=(PackageToolchainModuleRootFieldPath&&) noexcept =
      default;
  ZC_DISALLOW_COPY(PackageToolchainModuleRootFieldPath);

  ZC_NODISCARD PackageToolchainModuleRootFieldPath clone() const;
  ZC_NODISCARD zc::ArrayPtr<const identity::CanonicalPathSegment> components() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;
  bool operator==(const PackageToolchainModuleRootFieldPath& other) const noexcept;

private:
  explicit PackageToolchainModuleRootFieldPath(
      zc::Vector<identity::CanonicalPathSegment>&& components) noexcept;

  zc::Vector<identity::CanonicalPathSegment> componentValues;

  friend class PackageToolchainModuleRootFailure;
};

/// \brief Complete selected-package failure for a reserved target or dependency root.
class PackageToolchainModuleRootFailure final {
public:
  /// \brief Builds a target-root failure from one selected normalized target.
  ZC_NODISCARD static zc::Maybe<PackageToolchainModuleRootFailure> userTargetRoot(
      const TargetManifest& target, identity::PackageKey&& package);
  /// \brief Builds a dependency-alias failure from one normalized dependency record.
  ZC_NODISCARD static zc::Maybe<PackageToolchainModuleRootFailure> dependencyAlias(
      const DependencyRequirement& dependency, identity::PackageKey&& package);

  PackageToolchainModuleRootFailure(PackageToolchainModuleRootFailure&&) noexcept = default;
  PackageToolchainModuleRootFailure& operator=(PackageToolchainModuleRootFailure&&) noexcept =
      default;
  ZC_DISALLOW_COPY(PackageToolchainModuleRootFailure);

  ZC_NODISCARD PackageToolchainModuleRootProducer producer() const noexcept;
  ZC_NODISCARD const DiagnosticProvenance& provenance() const noexcept;
  ZC_NODISCARD const identity::PackageKey& package() const noexcept;
  ZC_NODISCARD const PackageToolchainModuleRootFieldPath& fieldPath() const noexcept;
  ZC_NODISCARD const diagnostics::ModuleRootArgument& argument() const noexcept;

private:
  PackageToolchainModuleRootFailure(PackageToolchainModuleRootProducer producer,
                                    DiagnosticProvenance&& provenance,
                                    identity::PackageKey&& package,
                                    PackageToolchainModuleRootFieldPath&& fieldPath,
                                    diagnostics::ModuleRootArgument&& argument) noexcept;

  PackageToolchainModuleRootProducer producerValue;
  DiagnosticProvenance provenanceValue;
  identity::PackageKey packageValue;
  PackageToolchainModuleRootFieldPath fieldPathValue;
  diagnostics::ModuleRootArgument argumentValue;
};

/// \brief Independently reconstructs a selected-package reserved-root failure.
class PackageToolchainModuleRootFailureVerifier final {
public:
  ZC_NODISCARD static bool verify(const PackageToolchainModuleRootFailure& failure,
                                  const NormalizedPackageCompilationRequest& request,
                                  const NormalizedManifest& manifest,
                                  const identity::PackageKey& package);
};

/// \brief One verified target selection retained until build-script outputs are known.
class VerifiedCompilationRoot final {
public:
  ZC_NODISCARD static VerifiedCompilationRoot from(identity::PackageKey&& package,
                                                   identity::CrateTargetKind targetKind,
                                                   identity::TargetName&& targetName,
                                                   uint32_t editionYear, bool requiresBuildScript,
                                                   identity::CanonicalRelativePath&& sourcePath);

  VerifiedCompilationRoot(VerifiedCompilationRoot&&) noexcept = default;
  VerifiedCompilationRoot& operator=(VerifiedCompilationRoot&&) noexcept = default;
  ZC_DISALLOW_COPY(VerifiedCompilationRoot);

  ZC_NODISCARD const identity::PackageKey& packageKey() const noexcept;
  ZC_NODISCARD identity::CrateTargetKind targetKind() const noexcept;
  ZC_NODISCARD zc::StringPtr targetName() const noexcept;
  ZC_NODISCARD uint32_t editionYear() const noexcept;
  ZC_NODISCARD bool requiresBuildScript() const noexcept;
  ZC_NODISCARD const identity::CanonicalRelativePath& sourcePath() const noexcept;

private:
  VerifiedCompilationRoot(identity::PackageKey&& package, identity::CrateTargetKind targetKind,
                          identity::TargetName&& targetName, uint32_t editionYear,
                          bool requiresBuildScript,
                          identity::CanonicalRelativePath&& sourcePath) noexcept;
  identity::PackageKey packageValue;
  identity::CrateTargetKind targetKindValue;
  identity::TargetName targetNameValue;
  uint32_t editionYearValue;
  bool requiresBuildScriptValue;
  identity::CanonicalRelativePath sourcePathValue;
};

/// \brief One selected root whose complete RFC 0011 CrateKey is immutable.
class FinalizedCompilationRoot final {
public:
  ZC_NODISCARD static zc::Maybe<FinalizedCompilationRoot> from(
      identity::PackageKey&& package, identity::CrateKey&& crate,
      identity::CanonicalRelativePath&& sourcePath);
  FinalizedCompilationRoot(FinalizedCompilationRoot&&) noexcept = default;
  FinalizedCompilationRoot& operator=(FinalizedCompilationRoot&&) noexcept = default;
  ZC_DISALLOW_COPY(FinalizedCompilationRoot);

  ZC_NODISCARD const identity::PackageKey& packageKey() const noexcept;
  ZC_NODISCARD const identity::CrateKey& crateKey() const noexcept;
  ZC_NODISCARD const identity::CanonicalRelativePath& sourcePath() const noexcept;

private:
  FinalizedCompilationRoot(identity::PackageKey&& package, identity::CrateKey&& crate,
                           identity::CanonicalRelativePath&& sourcePath) noexcept;
  identity::PackageKey packageValue;
  identity::CrateKey crateValue;
  identity::CanonicalRelativePath sourcePathValue;
};

/// \brief Workspace-validated non-empty selected-root request.
class VerifiedPackageCompilationRequest final {
public:
  ZC_NODISCARD static zc::Maybe<VerifiedPackageCompilationRequest> from(
      zc::Vector<VerifiedCompilationRoot>&& roots, RegisteredTargetSelection&& hostTarget,
      RegisteredTargetSelection&& target, SelectedLanguageOptions languageOptions,
      PackageLockMode lockMode);

  VerifiedPackageCompilationRequest(VerifiedPackageCompilationRequest&&) noexcept = default;
  VerifiedPackageCompilationRequest& operator=(VerifiedPackageCompilationRequest&&) noexcept =
      default;
  ZC_DISALLOW_COPY(VerifiedPackageCompilationRequest);

  ZC_NODISCARD zc::ArrayPtr<const VerifiedCompilationRoot> roots() const noexcept;
  ZC_NODISCARD const RegisteredTargetSelection& hostTarget() const noexcept;
  ZC_NODISCARD const RegisteredTargetSelection& target() const noexcept;
  ZC_NODISCARD const SelectedLanguageOptions& languageOptions() const noexcept;
  ZC_NODISCARD PackageLockMode lockMode() const noexcept;

  /// \brief Finalizes every ordinary root from the stable build-producer plan.
  ZC_NODISCARD zc::Maybe<zc::Vector<FinalizedCompilationRoot>> finalizeRoots(
      const VerifiedBuildScriptPlan& buildPlan) const;

private:
  VerifiedPackageCompilationRequest(zc::Vector<VerifiedCompilationRoot>&& roots,
                                    RegisteredTargetSelection&& hostTarget,
                                    RegisteredTargetSelection&& target,
                                    SelectedLanguageOptions languageOptions,
                                    PackageLockMode lockMode) noexcept;
  zc::Vector<VerifiedCompilationRoot> rootValues;
  RegisteredTargetSelection hostTargetValue;
  RegisteredTargetSelection targetValue;
  SelectedLanguageOptions languageOptionValues;
  PackageLockMode lockModeValue;
};

using PackageCompilationVerificationResult =
    zc::OneOf<VerifiedPackageCompilationRequest, PackageToolchainModuleRootFailure,
              TargetSelectionIssue>;

/// \brief Resolves package, target, and feature names against one normalized local workspace.
ZC_NODISCARD PackageCompilationVerificationResult verifyPackageCompilationRequest(
    const NormalizedPackageCompilationRequest& request, const NormalizedWorkspace& workspace);

/// \brief Returns the stable non-secret display token for an invocation issue.
ZC_NODISCARD zc::StringPtr invocationIssueDisplay(InvocationIssue issue) noexcept;

}  // namespace zomlang::compiler::driver::package
