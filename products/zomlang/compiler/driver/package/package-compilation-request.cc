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

#include "zomlang/compiler/driver/package/package-compilation-request.h"

#include "zomlang/compiler/driver/package/build-script-plan.h"
#include "zomlang/compiler/driver/package/build-script-runtime.h"
#include "zomlang/compiler/driver/package/feature-resolver.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::driver::package {
namespace {

bool validProfileCharacter(char value) {
  return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '.' ||
         value == '_' || value == '-';
}

bool digestIsZero(const identity::Sha256Digest& digest) {
  for (const auto value : digest.bytes()) {
    if (value != 0) { return false; }
  }
  return true;
}

void sortProfiles(zc::Vector<RegisteredTargetProfile>& profiles) {
  for (size_t index = 1; index < profiles.size(); ++index) {
    auto current = zc::mv(profiles[index]);
    size_t insertion = index;
    while (insertion != 0 && current.name() < profiles[insertion - 1].name()) {
      profiles[insertion] = zc::mv(profiles[insertion - 1]);
      --insertion;
    }
    profiles[insertion] = zc::mv(current);
  }
}

void sortTargets(zc::Vector<RequestedTargetSelection>& targets) {
  for (size_t index = 1; index < targets.size(); ++index) {
    auto current = zc::mv(targets[index]);
    identity::CanonicalEncoder currentEncoder;
    current.encode(currentEncoder);
    auto currentBytes = currentEncoder.finish();
    size_t insertion = index;
    while (insertion != 0) {
      identity::CanonicalEncoder previousEncoder;
      targets[insertion - 1].encode(previousEncoder);
      if (!(currentBytes.asPtr() < previousEncoder.finish().asPtr())) { break; }
      targets[insertion] = zc::mv(targets[insertion - 1]);
      --insertion;
    }
    targets[insertion] = zc::mv(current);
  }
}

bool hasDuplicateTargets(zc::ArrayPtr<const RequestedTargetSelection> targets) {
  for (size_t index = 1; index < targets.size(); ++index) {
    identity::CanonicalEncoder left;
    identity::CanonicalEncoder right;
    targets[index - 1].encode(left);
    targets[index].encode(right);
    if (left.finish().asPtr() == right.finish().asPtr()) { return true; }
  }
  return false;
}

zc::Maybe<const NormalizedManifest&> findPackage(const NormalizedWorkspace& workspace,
                                                 zc::StringPtr name) {
  if (workspace.root().hasPackage() && workspace.root().packageName() == name) {
    return workspace.root();
  }
  for (const auto& member : workspace.members()) {
    if (member.manifest().packageName() == name) { return member.manifest(); }
  }
  return zc::none;
}

identity::CanonicalWorkspaceRelativePath packageDirectory(const NormalizedWorkspace& workspace,
                                                          zc::StringPtr name) {
  if (workspace.root().hasPackage() && workspace.root().packageName() == name) {
    zc::Vector<identity::CanonicalPathSegment> empty;
    return identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(empty));
  }
  for (const auto& member : workspace.members()) {
    if (member.manifest().packageName() == name) { return member.packageDirectory().clone(); }
  }
  ZC_UNREACHABLE;
}

zc::Maybe<const TargetManifest&> findTarget(const NormalizedManifest& manifest,
                                            const RequestedTargetSelection& requested) {
  if (requested.kind == identity::CrateTargetKind::Library) {
    if (requested.name != zc::none || !manifest.hasLibrary()) { return zc::none; }
    return manifest.library();
  }
  zc::ArrayPtr<const TargetManifest> candidates;
  switch (requested.kind) {
    case identity::CrateTargetKind::Binary:
      candidates = manifest.binaries();
      break;
    case identity::CrateTargetKind::Test:
      candidates = manifest.tests();
      break;
    case identity::CrateTargetKind::Benchmark:
      candidates = manifest.benchmarks();
      break;
    case identity::CrateTargetKind::Example:
      candidates = manifest.examples();
      break;
    case identity::CrateTargetKind::Library:
    case identity::CrateTargetKind::BuildScript:
      return zc::none;
  }
  ZC_IF_SOME(name, requested.name) {
    for (const auto& candidate : candidates) {
      if (candidate.name() == name.text()) { return candidate; }
    }
  }
  return zc::none;
}

identity::SortedFeatureSet featureSet(zc::ArrayPtr<const identity::FeatureName> values) {
  zc::Vector<identity::FeatureName> copied(values.size());
  for (const auto& value : values) { copied.add(value.clone()); }
  auto result = identity::SortedFeatureSet::from(zc::mv(copied));
  ZC_IF_SOME(admitted, result) { return zc::mv(admitted); }
  ZC_UNREACHABLE;
}

zc::Array<uint8_t> rootSortKey(const VerifiedCompilationRoot& root) {
  identity::CanonicalEncoder encoder;
  const auto package = root.packageKey().encode();
  encoder.encodeByteString(package);
  encoder.encodeUint8(static_cast<uint8_t>(root.targetKind()));
  encoder.encodeByteString(root.targetName().asBytes());
  encoder.encodeUint32(root.editionYear());
  encoder.encodeBool(root.requiresBuildScript());
  root.sourcePath().encode(encoder);
  return encoder.finish();
}

void sortRoots(zc::Vector<VerifiedCompilationRoot>& roots) {
  for (size_t index = 1; index < roots.size(); ++index) {
    auto current = zc::mv(roots[index]);
    auto currentBytes = rootSortKey(current);
    size_t insertion = index;
    while (insertion != 0 && currentBytes.asPtr() < rootSortKey(roots[insertion - 1]).asPtr()) {
      roots[insertion] = zc::mv(roots[insertion - 1]);
      --insertion;
    }
    roots[insertion] = zc::mv(current);
  }
}

zc::Maybe<identity::SortedFeatureSet> parseFeatures(zc::ArrayPtr<const zc::String> lists) {
  zc::Vector<identity::FeatureName> features;
  for (const auto& list : lists) {
    if (list.size() == 0) { return zc::none; }
    size_t begin = 0;
    while (begin <= list.size()) {
      size_t end = begin;
      while (end < list.size() && list[end] != ',') { ++end; }
      if (end == begin) { return zc::none; }
      auto featureText = zc::str(list.slice(begin, end));
      auto admitted = identity::FeatureName::fromSource(featureText);
      if (admitted == zc::none) { return zc::none; }
      ZC_IF_SOME(value, admitted) { features.add(zc::mv(value)); }
      if (end == list.size()) { break; }
      begin = end + 1;
    }
  }
  return identity::SortedFeatureSet::from(zc::mv(features));
}

}  // namespace

void SelectedLanguageOptions::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeBool(useUnicode);
  encoder.encodeBool(allowDollarIdentifiers);
  encoder.encodeBool(supportRegexLiterals);
}

RegisteredTargetProfileName::RegisteredTargetProfileName(zc::String&& input) noexcept
    : value(zc::mv(input)) {}

zc::Maybe<RegisteredTargetProfileName> RegisteredTargetProfileName::from(zc::StringPtr text) {
  if (text.size() == 0 || text.size() > 255) { return zc::none; }
  for (const auto value : text) {
    if (!validProfileCharacter(value)) { return zc::none; }
  }
  return RegisteredTargetProfileName(zc::str(text));
}

RegisteredTargetProfileName RegisteredTargetProfileName::clone() const {
  return RegisteredTargetProfileName(zc::str(value));
}

zc::StringPtr RegisteredTargetProfileName::text() const noexcept { return value; }

void RegisteredTargetProfileName::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeByteString(value.asBytes());
}

RegisteredTargetSelection::RegisteredTargetSelection(
    const identity::Sha256Digest& revision, RegisteredTargetProfileName&& profile,
    identity::CanonicalTargetSpecificationKey&& projection, PackagePanicStrategy panic) noexcept
    : revisionValue(revision),
      profileValue(zc::mv(profile)),
      projectionValue(zc::mv(projection)),
      panicValue(panic) {}

RegisteredTargetSelection RegisteredTargetSelection::clone() const {
  return RegisteredTargetSelection(revisionValue, profileValue.clone(), projectionValue.clone(),
                                   panicValue);
}

const identity::Sha256Digest& RegisteredTargetSelection::registryRevision() const noexcept {
  return revisionValue;
}

zc::StringPtr RegisteredTargetSelection::profile() const noexcept { return profileValue.text(); }

const identity::CanonicalTargetSpecificationKey& RegisteredTargetSelection::semanticProjection()
    const noexcept {
  return projectionValue;
}

PackagePanicStrategy RegisteredTargetSelection::panicStrategy() const noexcept {
  return panicValue;
}

void RegisteredTargetSelection::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeDigest(revisionValue);
  profileValue.encode(encoder);
  projectionValue.encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(panicValue));
}

RegisteredTargetProfile::RegisteredTargetProfile(
    RegisteredTargetProfileName&& name,
    identity::CanonicalTargetSpecificationKey&& projection) noexcept
    : nameValue(zc::mv(name)), projectionValue(zc::mv(projection)) {}

RegisteredTargetProfile RegisteredTargetProfile::from(
    RegisteredTargetProfileName&& name,
    identity::CanonicalTargetSpecificationKey&& semanticProjection) {
  return RegisteredTargetProfile(zc::mv(name), zc::mv(semanticProjection));
}

zc::StringPtr RegisteredTargetProfile::name() const noexcept { return nameValue.text(); }

const identity::CanonicalTargetSpecificationKey& RegisteredTargetProfile::semanticProjection()
    const noexcept {
  return projectionValue;
}

RegisteredTargetService::RegisteredTargetService(RegisteredTargetProfileName&& hostProfile,
                                                 zc::Vector<RegisteredTargetProfile>&& profiles,
                                                 const identity::Sha256Digest& revision) noexcept
    : hostProfileValue(zc::mv(hostProfile)),
      profileValues(zc::mv(profiles)),
      revisionValue(revision) {}

zc::Maybe<RegisteredTargetService> RegisteredTargetService::fromVerifiedRegistry(
    const identity::Sha256Digest& revision, RegisteredTargetProfileName&& hostProfile,
    zc::Vector<RegisteredTargetProfile>&& profiles) {
  if (digestIsZero(revision) || profiles.size() == 0) { return zc::none; }
  sortProfiles(profiles);
  bool foundHost = false;
  for (size_t index = 0; index < profiles.size(); ++index) {
    if (index != 0 && profiles[index - 1].nameValue.text() == profiles[index].nameValue.text()) {
      return zc::none;
    }
    if (profiles[index].nameValue.text() == hostProfile.text()) { foundHost = true; }
  }
  if (!foundHost) { return zc::none; }
  return RegisteredTargetService(zc::mv(hostProfile), zc::mv(profiles), revision);
}

zc::Maybe<RegisteredTargetSelection> RegisteredTargetService::select(
    zc::Maybe<zc::StringPtr> requestedProfile, PackagePanicStrategy panic) const {
  if (panic != PackagePanicStrategy::Abort && panic != PackagePanicStrategy::Unwind) {
    return zc::none;
  }
  zc::StringPtr requested = hostProfileValue.text();
  ZC_IF_SOME(value, requestedProfile) { requested = value; }
  for (const auto& profile : profileValues) {
    if (profile.nameValue.text() == requested) {
      return RegisteredTargetSelection(revisionValue, profile.nameValue.clone(),
                                       profile.projectionValue.clone(), panic);
    }
  }
  return zc::none;
}

const identity::Sha256Digest& RegisteredTargetService::revision() const noexcept {
  return revisionValue;
}

zc::StringPtr RegisteredTargetService::hostProfile() const noexcept {
  return hostProfileValue.text();
}

RequestedTargetSelection::RequestedTargetSelection(
    identity::CrateTargetKind targetKind, zc::Maybe<identity::TargetName>&& targetName) noexcept
    : kind(targetKind), name(zc::mv(targetName)) {}

RequestedTargetSelection RequestedTargetSelection::clone() const {
  zc::Maybe<identity::TargetName> copied;
  ZC_IF_SOME(value, name) { copied = value.clone(); }
  return RequestedTargetSelection(kind, zc::mv(copied));
}

void RequestedTargetSelection::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(kind));
  ZC_IF_SOME(value, name) {
    encoder.encodeSome();
    value.encode(encoder);
  } else {
    encoder.encodeNone();
  }
}

NormalizedPackageCompilationRequest::NormalizedPackageCompilationRequest(
    identity::PackageName&& package, zc::Vector<RequestedTargetSelection>&& targets,
    identity::SortedFeatureSet&& features, bool useDefaultFeatures,
    RegisteredTargetSelection&& hostTarget, RegisteredTargetSelection&& target,
    SelectedLanguageOptions languageOptions, PackageLockMode lockMode) noexcept
    : packageValue(zc::mv(package)),
      targetValues(zc::mv(targets)),
      featureValues(zc::mv(features)),
      defaultFeatureValue(useDefaultFeatures),
      hostTargetValue(zc::mv(hostTarget)),
      targetValue(zc::mv(target)),
      languageOptionValues(languageOptions),
      lockModeValue(lockMode) {}

zc::StringPtr NormalizedPackageCompilationRequest::package() const noexcept {
  return packageValue.text();
}

zc::ArrayPtr<const RequestedTargetSelection> NormalizedPackageCompilationRequest::requestedTargets()
    const noexcept {
  return targetValues;
}

zc::ArrayPtr<const identity::FeatureName> NormalizedPackageCompilationRequest::requestedFeatures()
    const noexcept {
  return featureValues.values();
}

bool NormalizedPackageCompilationRequest::useDefaultFeatures() const noexcept {
  return defaultFeatureValue;
}

const RegisteredTargetSelection& NormalizedPackageCompilationRequest::hostTarget() const noexcept {
  return hostTargetValue;
}

const RegisteredTargetSelection& NormalizedPackageCompilationRequest::target() const noexcept {
  return targetValue;
}

const SelectedLanguageOptions& NormalizedPackageCompilationRequest::languageOptions()
    const noexcept {
  return languageOptionValues;
}

PackageLockMode NormalizedPackageCompilationRequest::lockMode() const noexcept {
  return lockModeValue;
}

void NormalizedPackageCompilationRequest::encode(identity::CanonicalEncoder& encoder) const {
  packageValue.encode(encoder);
  encoder.encodeSequenceSize(targetValues.size());
  for (const auto& target : targetValues) { target.encode(encoder); }
  featureValues.encode(encoder);
  encoder.encodeBool(defaultFeatureValue);
  hostTargetValue.encode(encoder);
  targetValue.encode(encoder);
  languageOptionValues.encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(lockModeValue));
}

zc::Array<uint8_t> NormalizedPackageCompilationRequest::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

PackageCompilationNormalizationResult normalizePackageCompilationRequest(
    RawPackageCompilationRequest&& raw, const RegisteredTargetService& targets) {
  if (raw.positionalArguments.size() != 0) { return InvocationIssue::PositionalSourceArgument; }
  if (raw.packageSelections.size() == 0) { return InvocationIssue::MissingPackageSelection; }
  if (raw.packageSelections.size() != 1) { return InvocationIssue::DuplicatePackageSelection; }
  auto package = identity::PackageName::fromSource(raw.packageSelections[0]);
  if (package == zc::none) { return InvocationIssue::MissingPackageSelection; }
  if (raw.targetSelections.size() == 0) { return InvocationIssue::MissingTargetSelection; }
  sortTargets(raw.targetSelections);
  if (hasDuplicateTargets(raw.targetSelections)) {
    return InvocationIssue::DuplicateTargetSelection;
  }
  auto features = parseFeatures(raw.featureLists);
  if (features == zc::none) { return InvocationIssue::InvalidFeatureList; }
  if (raw.lockedCount > 1 || raw.updateLockCount > 1 ||
      (raw.lockedCount != 0 && raw.updateLockCount != 0)) {
    return InvocationIssue::ConflictingLockMode;
  }
  if (raw.targetProfiles.size() > 1) { return InvocationIssue::UnknownTargetProfile; }
  if (raw.panicCount > 1) { return InvocationIssue::InvalidPanicStrategy; }
  PackagePanicStrategy panic;
  if (raw.panicStrategy == "abort") {
    panic = PackagePanicStrategy::Abort;
  } else if (raw.panicStrategy == "unwind") {
    panic = PackagePanicStrategy::Unwind;
  } else {
    return InvocationIssue::InvalidPanicStrategy;
  }
  auto host = targets.select(zc::none, panic);
  zc::Maybe<zc::StringPtr> requestedProfile;
  if (raw.targetProfiles.size() == 1) { requestedProfile = raw.targetProfiles[0]; }
  auto target = targets.select(requestedProfile, panic);
  if (host == zc::none || target == zc::none) { return InvocationIssue::UnknownTargetProfile; }
  PackageLockMode lockMode = PackageLockMode::PreferLocked;
  if (raw.lockedCount != 0) {
    lockMode = PackageLockMode::LockedOnly;
  } else if (raw.updateLockCount != 0) {
    lockMode = PackageLockMode::Update;
  }
  ZC_IF_SOME(packageValue, package) {
    ZC_IF_SOME(featureValue, features) {
      ZC_IF_SOME(hostValue, host) {
        ZC_IF_SOME(targetValue, target) {
          return NormalizedPackageCompilationRequest(
              zc::mv(packageValue), zc::mv(raw.targetSelections), zc::mv(featureValue),
              raw.useDefaultFeatures, zc::mv(hostValue), zc::mv(targetValue), raw.languageOptions,
              lockMode);
        }
      }
    }
  }
  ZC_UNREACHABLE;
}

VerifiedCompilationRoot::VerifiedCompilationRoot(
    identity::PackageKey&& package, identity::CrateTargetKind targetKind,
    identity::TargetName&& targetName, uint32_t editionYear, bool requiresBuildScript,
    identity::CanonicalRelativePath&& sourcePath) noexcept
    : packageValue(zc::mv(package)),
      targetKindValue(targetKind),
      targetNameValue(zc::mv(targetName)),
      editionYearValue(editionYear),
      requiresBuildScriptValue(requiresBuildScript),
      sourcePathValue(zc::mv(sourcePath)) {}

VerifiedCompilationRoot VerifiedCompilationRoot::from(
    identity::PackageKey&& package, identity::CrateTargetKind targetKind,
    identity::TargetName&& targetName, uint32_t editionYear, bool requiresBuildScript,
    identity::CanonicalRelativePath&& sourcePath) {
  return VerifiedCompilationRoot(zc::mv(package), targetKind, zc::mv(targetName), editionYear,
                                 requiresBuildScript, zc::mv(sourcePath));
}

const identity::PackageKey& VerifiedCompilationRoot::packageKey() const noexcept {
  return packageValue;
}

identity::CrateTargetKind VerifiedCompilationRoot::targetKind() const noexcept {
  return targetKindValue;
}

zc::StringPtr VerifiedCompilationRoot::targetName() const noexcept {
  return targetNameValue.text();
}

uint32_t VerifiedCompilationRoot::editionYear() const noexcept { return editionYearValue; }

bool VerifiedCompilationRoot::requiresBuildScript() const noexcept {
  return requiresBuildScriptValue;
}

const identity::CanonicalRelativePath& VerifiedCompilationRoot::sourcePath() const noexcept {
  return sourcePathValue;
}

FinalizedCompilationRoot::FinalizedCompilationRoot(
    identity::PackageKey&& package, identity::CrateKey&& crate,
    identity::CanonicalRelativePath&& sourcePath) noexcept
    : packageValue(zc::mv(package)),
      crateValue(zc::mv(crate)),
      sourcePathValue(zc::mv(sourcePath)) {}

zc::Maybe<FinalizedCompilationRoot> FinalizedCompilationRoot::from(
    identity::PackageKey&& package, identity::CrateKey&& crate,
    identity::CanonicalRelativePath&& sourcePath) {
  if (package.encode().asPtr() != crate.package().encode().asPtr()) { return zc::none; }
  return FinalizedCompilationRoot(zc::mv(package), zc::mv(crate), zc::mv(sourcePath));
}

const identity::PackageKey& FinalizedCompilationRoot::packageKey() const noexcept {
  return packageValue;
}

const identity::CrateKey& FinalizedCompilationRoot::crateKey() const noexcept { return crateValue; }

const identity::CanonicalRelativePath& FinalizedCompilationRoot::sourcePath() const noexcept {
  return sourcePathValue;
}

VerifiedPackageCompilationRequest::VerifiedPackageCompilationRequest(
    zc::Vector<VerifiedCompilationRoot>&& roots, RegisteredTargetSelection&& hostTarget,
    RegisteredTargetSelection&& target, SelectedLanguageOptions languageOptions,
    PackageLockMode lockMode) noexcept
    : rootValues(zc::mv(roots)),
      hostTargetValue(zc::mv(hostTarget)),
      targetValue(zc::mv(target)),
      languageOptionValues(languageOptions),
      lockModeValue(lockMode) {}

zc::Maybe<VerifiedPackageCompilationRequest> VerifiedPackageCompilationRequest::from(
    zc::Vector<VerifiedCompilationRoot>&& roots, RegisteredTargetSelection&& hostTarget,
    RegisteredTargetSelection&& target, SelectedLanguageOptions languageOptions,
    PackageLockMode lockMode) {
  if (roots.size() == 0 || digestIsZero(hostTarget.registryRevision()) ||
      hostTarget.registryRevision() != target.registryRevision()) {
    return zc::none;
  }
  sortRoots(roots);
  for (size_t index = 1; index < roots.size(); ++index) {
    if (rootSortKey(roots[index - 1]).asPtr() == rootSortKey(roots[index]).asPtr()) {
      return zc::none;
    }
  }
  return VerifiedPackageCompilationRequest(zc::mv(roots), zc::mv(hostTarget), zc::mv(target),
                                           languageOptions, lockMode);
}

zc::ArrayPtr<const VerifiedCompilationRoot> VerifiedPackageCompilationRequest::roots()
    const noexcept {
  return rootValues;
}

const RegisteredTargetSelection& VerifiedPackageCompilationRequest::hostTarget() const noexcept {
  return hostTargetValue;
}

const RegisteredTargetSelection& VerifiedPackageCompilationRequest::target() const noexcept {
  return targetValue;
}

const SelectedLanguageOptions& VerifiedPackageCompilationRequest::languageOptions() const noexcept {
  return languageOptionValues;
}

PackageLockMode VerifiedPackageCompilationRequest::lockMode() const noexcept {
  return lockModeValue;
}

zc::Maybe<zc::Vector<FinalizedCompilationRoot>> VerifiedPackageCompilationRequest::finalizeRoots(
    const VerifiedBuildScriptPlan& buildPlan) const {
  zc::Vector<FinalizedCompilationRoot> finalized(rootValues.size());
  for (const auto& root : rootValues) {
    zc::Maybe<identity::BuildScriptProducerKey> buildProducer;
    if (root.requiresBuildScript()) {
      size_t matches = 0;
      for (const auto& node : buildPlan.nodes()) {
        if (node.key().preparatory().package().encode().asPtr() ==
            root.packageKey().encode().asPtr()) {
          buildProducer = node.key().preparatory().producerKey();
          ++matches;
        }
      }
      if (matches != 1) { return zc::none; }
    }

    auto compilation = identity::CompilationConfigKey::from(
        identity::CompilationDomain::Target, targetValue.semanticProjection().clone(),
        identity::SemanticCompilerOptionsKey::from(
            root.editionYear(), languageOptionValues.useUnicode,
            languageOptionValues.allowDollarIdentifiers, languageOptionValues.supportRegexLiterals),
        zc::mv(buildProducer));
    auto targetName = identity::TargetName::fromCanonical(root.targetName());
    if (compilation == zc::none || targetName == zc::none) { return zc::none; }
    ZC_IF_SOME(compilationValue, compilation) {
      ZC_IF_SOME(targetNameValue, targetName) {
        auto crate = identity::CrateKey::from(root.packageKey().clone(), root.targetKind(),
                                              zc::mv(targetNameValue), zc::mv(compilationValue));
        if (crate == zc::none) { return zc::none; }
        ZC_IF_SOME(crateValue, crate) {
          auto finalizedRoot = FinalizedCompilationRoot::from(
              root.packageKey().clone(), zc::mv(crateValue), root.sourcePath().clone());
          if (finalizedRoot == zc::none) { return zc::none; }
          ZC_IF_SOME(value, finalizedRoot) { finalized.add(zc::mv(value)); }
        }
      }
    }
  }
  for (size_t index = 1; index < finalized.size(); ++index) {
    auto current = zc::mv(finalized[index]);
    const auto currentKey = current.crateKey().encode();
    size_t insertion = index;
    while (insertion != 0 &&
           currentKey.asPtr() < finalized[insertion - 1].crateKey().encode().asPtr()) {
      finalized[insertion] = zc::mv(finalized[insertion - 1]);
      --insertion;
    }
    finalized[insertion] = zc::mv(current);
  }
  return finalized;
}

PackageCompilationVerificationResult verifyPackageCompilationRequest(
    const NormalizedPackageCompilationRequest& request, const NormalizedWorkspace& workspace) {
  auto selectedManifest = findPackage(workspace, request.package());
  if (selectedManifest == zc::none) { return TargetSelectionIssue::UnknownWorkspacePackage; }
  ZC_IF_SOME(manifest, selectedManifest) {
    auto activation =
        FeatureResolver::expand(manifest, FeatureActivationDomain::Target,
                                request.requestedFeatures(), request.useDefaultFeatures());
    if (activation.is<FeatureIssue>()) { return TargetSelectionIssue::UnknownRootFeature; }
    const auto& expanded = activation.get<ExpandedFeatureActivation>();
    auto packageName = identity::PackageName::fromCanonical(manifest.packageName());
    auto version = identity::ResolvedVersion::fromCanonical(manifest.packageVersion());
    if (packageName == zc::none || version == zc::none) { ZC_UNREACHABLE; }
    auto source =
        identity::CanonicalPackageSource::localPath(packageDirectory(workspace, request.package()));
    zc::Vector<VerifiedCompilationRoot> roots(request.requestedTargets().size());
    for (const auto& requested : request.requestedTargets()) {
      auto selectedTarget = findTarget(manifest, requested);
      if (selectedTarget == zc::none) { return TargetSelectionIssue::UnknownTarget; }
      ZC_IF_SOME(target, selectedTarget) {
        auto targetName = identity::TargetName::fromCanonical(target.name());
        if (targetName == zc::none) { ZC_UNREACHABLE; }
        ZC_IF_SOME(packageNameValue, packageName) {
          ZC_IF_SOME(versionValue, version) {
            auto packageKey = identity::PackageKey::from(source.clone(), packageNameValue.clone(),
                                                         versionValue.clone(),
                                                         featureSet(expanded.activeFeatures()));
            ZC_IF_SOME(targetNameValue, targetName) {
              roots.add(VerifiedCompilationRoot::from(
                  zc::mv(packageKey), target.kind(), zc::mv(targetNameValue),
                  manifest.editionYear(), manifest.hasBuildScript(), target.path().clone()));
            }
          }
        }
      }
    }
    auto verified = VerifiedPackageCompilationRequest::from(
        zc::mv(roots), request.hostTarget().clone(), request.target().clone(),
        request.languageOptions(), request.lockMode());
    ZC_IF_SOME(value, verified) { return zc::mv(value); }
    ZC_UNREACHABLE;
  }
  ZC_UNREACHABLE;
}

zc::StringPtr invocationIssueDisplay(InvocationIssue issue) noexcept {
  switch (issue) {
    case InvocationIssue::ManifestNotFound:
      return "manifest-not-found"_zc;
    case InvocationIssue::InvalidManifestPath:
      return "invalid-manifest-path"_zc;
    case InvocationIssue::MissingPackageSelection:
      return "missing-package-selection"_zc;
    case InvocationIssue::DuplicatePackageSelection:
      return "duplicate-package-selection"_zc;
    case InvocationIssue::MissingTargetSelection:
      return "missing-target-selection"_zc;
    case InvocationIssue::DuplicateTargetSelection:
      return "duplicate-target-selection"_zc;
    case InvocationIssue::PositionalSourceArgument:
      return "positional-source-argument"_zc;
    case InvocationIssue::InvalidFeatureList:
      return "invalid-feature-list"_zc;
    case InvocationIssue::ConflictingLockMode:
      return "conflicting-lock-mode"_zc;
    case InvocationIssue::UnknownTargetProfile:
      return "unknown-target-profile"_zc;
    case InvocationIssue::InvalidPanicStrategy:
      return "invalid-panic-strategy"_zc;
  }
  ZC_UNREACHABLE;
}

}  // namespace zomlang::compiler::driver::package
