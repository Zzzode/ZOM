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

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/driver/package/build-script-plan.h"
#include "zomlang/compiler/ir/target-registry.h"

namespace zomlang::compiler::driver::package {
namespace {

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto result = Scalar::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid scalar test input");
}

identity::SortedTargetFeatureSet targetFeatures() {
  zc::Vector<identity::TargetFeatureName> values;
  values.add(scalar<identity::TargetFeatureName>("sse2"_zc));
  auto result = identity::SortedTargetFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid target feature test input");
}

identity::CanonicalTargetSpecificationKey targetProjection() {
  auto result = identity::CanonicalTargetSpecificationKey::from(
      scalar<identity::TargetComponentName>("x86_64"_zc),
      scalar<identity::TargetComponentName>("zom"_zc),
      scalar<identity::TargetComponentName>("none"_zc),
      scalar<identity::TargetComponentName>("unknown"_zc),
      scalar<identity::TargetComponentName>("zom"_zc), 64, identity::Endianness::Little,
      targetFeatures());
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid target projection test input");
}

RegisteredTargetProfileName profileName(zc::StringPtr text) {
  auto result = RegisteredTargetProfileName::from(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid profile name test input");
}

RegisteredTargetService targetService() {
  zc::Vector<ir::CanonicalTargetFeature> backendFeatures;
  auto feature = ir::CanonicalTargetFeature::from("sse2"_zc, ir::TargetFeatureState::Enabled);
  ZC_IF_SOME(value, feature) { backendFeatures.add(zc::mv(value)); }
  auto spec = ir::CanonicalTargetSpec::from(
      "x86_64-zom-none"_zc, "e-p:64:64"_zc, "generic"_zc, zc::mv(backendFeatures), "zom"_zc,
      ir::BackendPanicStrategy::Unwind, ir::ObjectFormat::Elf);
  ZC_IF_SOME(specValue, spec) {
    zc::Vector<identity::TargetFeatureName> semanticFeatures;
    semanticFeatures.add(scalar<identity::TargetFeatureName>("sse2"_zc));
    zc::Vector<ir::CanonicalTargetSpec> specifications;
    specifications.add(zc::mv(specValue));
    auto profile =
        ir::RegisteredTargetProfileRecord::from(profileName("host"_zc), targetProjection(),
                                                zc::mv(semanticFeatures), zc::mv(specifications));
    ZC_IF_SOME(profileValue, profile) {
      zc::Vector<ir::RegisteredTargetProfileRecord> profiles;
      profiles.add(zc::mv(profileValue));
      auto registry = ir::TargetRegistrySnapshot::from(profileName("host"_zc), zc::mv(profiles));
      ZC_IF_SOME(registryValue, registry) {
        auto result = registryValue.packageTargetService();
        ZC_IF_SOME(value, result) { return zc::mv(value); }
      }
    }
  }
  ZC_FAIL_REQUIRE("invalid target service test input");
}

RequestedTargetSelection libraryTarget() {
  zc::Maybe<identity::TargetName> noName;
  return RequestedTargetSelection(identity::CrateTargetKind::Library, zc::mv(noName));
}

RequestedTargetSelection namedTarget(identity::CrateTargetKind kind, zc::StringPtr name) {
  zc::Maybe<identity::TargetName> targetName = scalar<identity::TargetName>(name);
  return RequestedTargetSelection(kind, zc::mv(targetName));
}

RawPackageCompilationRequest validRaw() {
  RawPackageCompilationRequest raw;
  raw.packageSelections.add(zc::str("app"));
  raw.targetSelections.add(libraryTarget());
  return raw;
}

identity::CanonicalRelativePath relativePath(zc::StringPtr first, zc::StringPtr second) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(scalar<identity::CanonicalPathSegment>(first));
  segments.add(scalar<identity::CanonicalPathSegment>(second));
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

identity::SortedFeatureSet noPackageFeatures() {
  zc::Vector<identity::FeatureName> features;
  auto result = identity::SortedFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid empty package feature set");
}

identity::PackageKey packageKey(zc::StringPtr name) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  auto packageName = identity::PackageName::fromCanonical(name);
  auto version = identity::ResolvedVersion::fromCanonical("1.0.0"_zc);
  ZC_REQUIRE(packageName != zc::none);
  ZC_REQUIRE(version != zc::none);
  ZC_IF_SOME(packageNameValue, packageName) {
    ZC_IF_SOME(versionValue, version) {
      return identity::PackageKey::from(
          identity::CanonicalPackageSource::localPath(
              identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments))),
          zc::mv(packageNameValue), zc::mv(versionValue), noPackageFeatures());
    }
  }
  ZC_UNREACHABLE
}

identity::CrateKey crateKey(zc::StringPtr packageName) {
  auto compilation = identity::CompilationConfigKey::from(
      identity::CompilationDomain::Target, targetProjection(),
      identity::SemanticCompilerOptionsKey::from(2026, true, false, false), zc::none);
  auto targetName = identity::TargetName::fromCanonical("app"_zc);
  ZC_REQUIRE(compilation != zc::none);
  ZC_REQUIRE(targetName != zc::none);
  ZC_IF_SOME(compilationValue, compilation) {
    ZC_IF_SOME(targetNameValue, targetName) {
      auto result = identity::CrateKey::from(
          identity::CompilationUnitIdentity::userPackage(packageKey(packageName)),
          identity::CrateTargetKind::Binary, zc::mv(targetNameValue), zc::mv(compilationValue));
      ZC_IF_SOME(value, result) { return zc::mv(value); }
    }
  }
  ZC_FAIL_REQUIRE("invalid crate key fixture");
}

NormalizedWorkspace workspace() {
  zc::Vector<identity::CanonicalRelativePath> files;
  files.add(relativePath("src"_zc, "lib.zom"_zc));
  files.add(relativePath("src"_zc, "main.zom"_zc));
  auto inventory = PackageSourceInventory::from(zc::mv(files));
  ZC_IF_SOME(inventoryValue, inventory) {
    zc::Vector<WorkspaceMemberInput> members;
    auto result = normalizeWorkspace(R"toml([package]
name = "app"
version = "1.2.3"
edition = "2026"

[features]
default = ["fast"]
fast = []
)toml"_zc,
                                     inventoryValue, zc::mv(members));
    if (result.is<NormalizedWorkspace>()) { return zc::mv(result.get<NormalizedWorkspace>()); }
  }
  ZC_FAIL_REQUIRE("valid workspace test input was rejected");
}

identity::CanonicalRelativePath sourcePath(zc::StringPtr text) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  size_t start = 0;
  for (size_t index = 0; index <= text.size(); ++index) {
    if (index < text.size() && text[index] != '/') { continue; }
    const auto segmentText = zc::heapString(text.slice(start, index));
    segments.add(scalar<identity::CanonicalPathSegment>(segmentText));
    start = index + 1;
  }
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

NormalizedWorkspace workspaceFrom(zc::StringPtr source,
                                  zc::ArrayPtr<const zc::StringPtr> sourcePaths) {
  zc::Vector<identity::CanonicalRelativePath> files;
  for (const auto path : sourcePaths) { files.add(sourcePath(path)); }
  auto inventory = PackageSourceInventory::from(zc::mv(files));
  ZC_REQUIRE(inventory != zc::none);
  ZC_IF_SOME(inventoryValue, inventory) {
    zc::Vector<WorkspaceMemberInput> members;
    auto result = normalizeWorkspace(source, inventoryValue, zc::mv(members));
    if (result.is<NormalizedWorkspace>()) { return zc::mv(result.get<NormalizedWorkspace>()); }
  }
  ZC_FAIL_REQUIRE("valid reservation workspace test input was rejected");
}

NormalizedPackageCompilationRequest normalize(RawPackageCompilationRequest&& raw) {
  auto service = targetService();
  auto result = normalizePackageCompilationRequest(zc::mv(raw), service);
  ZC_REQUIRE(result.is<NormalizedPackageCompilationRequest>());
  return zc::mv(result.get<NormalizedPackageCompilationRequest>());
}

size_t offsetOf(zc::StringPtr source, zc::StringPtr needle, size_t start = 0) {
  ZC_REQUIRE(needle.size() != 0 && needle.size() <= source.size());
  for (size_t offset = start; offset + needle.size() <= source.size(); ++offset) {
    if (source.slice(offset, offset + needle.size()) == needle) { return offset; }
  }
  ZC_FAIL_REQUIRE("expected reservation fixture text was not found");
}

InvocationIssue failure(RawPackageCompilationRequest&& raw) {
  auto service = targetService();
  auto result = normalizePackageCompilationRequest(zc::mv(raw), service);
  ZC_REQUIRE(result.is<InvocationIssue>());
  return result.get<InvocationIssue>();
}

}  // namespace

ZC_TEST("Registered target profile names enforce the closed syntax") {
  ZC_EXPECT(RegisteredTargetProfileName::from("linux-x86_64"_zc) != zc::none);
  ZC_EXPECT(RegisteredTargetProfileName::from(""_zc) == zc::none);
  ZC_EXPECT(RegisteredTargetProfileName::from("Upper"_zc) == zc::none);
  ZC_EXPECT(RegisteredTargetProfileName::from("bad/profile"_zc) == zc::none);
}

ZC_TEST("Package compilation request maps every semantic command option") {
  auto raw = validRaw();
  raw.targetSelections.add(namedTarget(identity::CrateTargetKind::Binary, "cli"_zc));
  raw.featureLists.add(zc::str("logging,serde"));
  raw.useDefaultFeatures = false;
  raw.targetProfiles.add(zc::str("host"));
  raw.updateLockCount = 1;
  raw.panicCount = 1;
  raw.panicStrategy = zc::str("unwind");
  raw.languageOptions = SelectedLanguageOptions{false, true, false};

  auto service = targetService();
  auto result = normalizePackageCompilationRequest(zc::mv(raw), service);
  ZC_REQUIRE(result.is<NormalizedPackageCompilationRequest>());
  const auto& request = result.get<NormalizedPackageCompilationRequest>();
  ZC_EXPECT(request.package() == "app");
  ZC_EXPECT(request.requestedTargets().size() == 2);
  ZC_EXPECT(request.requestedFeatures().size() == 2);
  ZC_EXPECT(!request.useDefaultFeatures());
  ZC_EXPECT(request.hostTarget().profile() == "host");
  ZC_EXPECT(request.target().profile() == "host");
  ZC_EXPECT(request.target().panicStrategy() == PackagePanicStrategy::Unwind);
  ZC_EXPECT(request.lockMode() == PackageLockMode::Update);
  ZC_EXPECT(!request.languageOptions().useUnicode);
  ZC_EXPECT(request.languageOptions().allowDollarIdentifiers);
  ZC_EXPECT(!request.languageOptions().supportRegexLiterals);
  ZC_EXPECT(request.hostTarget().registryRevision() == request.target().registryRevision());
}

ZC_TEST("Package compilation request canonicalizes target and feature order") {
  auto first = validRaw();
  first.targetSelections.add(namedTarget(identity::CrateTargetKind::Test, "integration"_zc));
  first.featureLists.add(zc::str("z,a"));
  auto second = validRaw();
  second.targetSelections.clear();
  second.targetSelections.add(namedTarget(identity::CrateTargetKind::Test, "integration"_zc));
  second.targetSelections.add(libraryTarget());
  second.featureLists.add(zc::str("a,z"));
  auto service = targetService();
  auto left = normalizePackageCompilationRequest(zc::mv(first), service);
  auto right = normalizePackageCompilationRequest(zc::mv(second), service);
  ZC_REQUIRE(left.is<NormalizedPackageCompilationRequest>());
  ZC_REQUIRE(right.is<NormalizedPackageCompilationRequest>());
  ZC_EXPECT(left.get<NormalizedPackageCompilationRequest>().encode().asPtr() ==
            right.get<NormalizedPackageCompilationRequest>().encode().asPtr());
}

ZC_TEST("Package compilation request rejects every pre-request invocation issue") {
  RawPackageCompilationRequest missingPackage;
  missingPackage.targetSelections.add(libraryTarget());
  ZC_EXPECT(failure(zc::mv(missingPackage)) == InvocationIssue::MissingPackageSelection);

  auto duplicatePackage = validRaw();
  duplicatePackage.packageSelections.add(zc::str("other"));
  ZC_EXPECT(failure(zc::mv(duplicatePackage)) == InvocationIssue::DuplicatePackageSelection);

  auto missingTarget = validRaw();
  missingTarget.targetSelections.clear();
  ZC_EXPECT(failure(zc::mv(missingTarget)) == InvocationIssue::MissingTargetSelection);

  auto duplicateTarget = validRaw();
  duplicateTarget.targetSelections.add(libraryTarget());
  ZC_EXPECT(failure(zc::mv(duplicateTarget)) == InvocationIssue::DuplicateTargetSelection);

  auto positional = validRaw();
  positional.positionalArguments.add(zc::str("main.zom"));
  ZC_EXPECT(failure(zc::mv(positional)) == InvocationIssue::PositionalSourceArgument);

  auto invalidFeature = validRaw();
  invalidFeature.featureLists.add(zc::str("a,,b"));
  ZC_EXPECT(failure(zc::mv(invalidFeature)) == InvocationIssue::InvalidFeatureList);

  auto conflictingLock = validRaw();
  conflictingLock.lockedCount = 1;
  conflictingLock.updateLockCount = 1;
  ZC_EXPECT(failure(zc::mv(conflictingLock)) == InvocationIssue::ConflictingLockMode);

  auto unknownTarget = validRaw();
  unknownTarget.targetProfiles.add(zc::str("missing"));
  ZC_EXPECT(failure(zc::mv(unknownTarget)) == InvocationIssue::UnknownTargetProfile);

  auto invalidPanic = validRaw();
  invalidPanic.panicStrategy = zc::str("explode");
  invalidPanic.panicCount = 1;
  ZC_EXPECT(failure(zc::mv(invalidPanic)) == InvocationIssue::InvalidPanicStrategy);
}

ZC_TEST("Invocation issue display covers the complete closed enum") {
  for (uint8_t value = static_cast<uint8_t>(InvocationIssue::ManifestNotFound);
       value <= static_cast<uint8_t>(InvocationIssue::InvalidPanicStrategy); ++value) {
    ZC_EXPECT(invocationIssueDisplay(static_cast<InvocationIssue>(value)).size() != 0);
  }
}

ZC_TEST("Workspace verification finalizes complete package and crate keys") {
  auto raw = validRaw();
  raw.targetSelections.add(namedTarget(identity::CrateTargetKind::Binary, "app"_zc));
  auto service = targetService();
  auto normalized = normalizePackageCompilationRequest(zc::mv(raw), service);
  ZC_REQUIRE(normalized.is<NormalizedPackageCompilationRequest>());
  auto input = workspace();
  auto verified =
      verifyPackageCompilationRequest(normalized.get<NormalizedPackageCompilationRequest>(), input);
  ZC_REQUIRE(verified.is<VerifiedPackageCompilationRequest>());
  const auto& request = verified.get<VerifiedPackageCompilationRequest>();
  ZC_REQUIRE(request.roots().size() == 2);
  ZC_EXPECT(request.roots()[0].packageKey().features().size() == 2);
  zc::Vector<BuildScriptPlanNode> noNodes;
  auto plan = VerifiedBuildScriptPlan::from(zc::mv(noNodes));
  ZC_REQUIRE(plan != zc::none);
  zc::Maybe<zc::Vector<FinalizedCompilationRoot>> finalized;
  ZC_IF_SOME(value, plan) { finalized = request.finalizeRoots(value); }
  ZC_REQUIRE(finalized != zc::none);
  ZC_IF_SOME(roots, finalized) {
    ZC_REQUIRE(roots.size() == 2);
    ZC_EXPECT(roots[0].crateKey().encode().size() != 0);
    ZC_EXPECT(roots[1].crateKey().encode().size() != 0);
    ZC_EXPECT(roots[0].crateKey().encode().asPtr() != roots[1].crateKey().encode().asPtr());
  }
}

ZC_TEST("Finalized compilation roots reject a crate from another package") {
  auto matching = FinalizedCompilationRoot::from(packageKey("app"_zc), crateKey("app"_zc),
                                                 relativePath("src"_zc, "main.zom"_zc));
  ZC_EXPECT(matching != zc::none);

  auto mismatched = FinalizedCompilationRoot::from(packageKey("app"_zc), crateKey("dependency"_zc),
                                                   relativePath("src"_zc, "main.zom"_zc));
  ZC_EXPECT(mismatched == zc::none);
}

ZC_TEST("Workspace verification rejects unknown packages targets and root features") {
  auto service = targetService();
  auto input = workspace();

  auto unknownPackage = validRaw();
  unknownPackage.packageSelections[0] = zc::str("missing");
  auto normalizedPackage = normalizePackageCompilationRequest(zc::mv(unknownPackage), service);
  ZC_REQUIRE(normalizedPackage.is<NormalizedPackageCompilationRequest>());
  auto packageResult = verifyPackageCompilationRequest(
      normalizedPackage.get<NormalizedPackageCompilationRequest>(), input);
  ZC_REQUIRE(packageResult.is<TargetSelectionIssue>());
  ZC_EXPECT(packageResult.get<TargetSelectionIssue>() ==
            TargetSelectionIssue::UnknownWorkspacePackage);

  auto unknownTarget = validRaw();
  unknownTarget.targetSelections.clear();
  unknownTarget.targetSelections.add(namedTarget(identity::CrateTargetKind::Binary, "missing"_zc));
  auto normalizedTarget = normalizePackageCompilationRequest(zc::mv(unknownTarget), service);
  ZC_REQUIRE(normalizedTarget.is<NormalizedPackageCompilationRequest>());
  auto targetResult = verifyPackageCompilationRequest(
      normalizedTarget.get<NormalizedPackageCompilationRequest>(), input);
  ZC_REQUIRE(targetResult.is<TargetSelectionIssue>());
  ZC_EXPECT(targetResult.get<TargetSelectionIssue>() == TargetSelectionIssue::UnknownTarget);

  auto unknownFeature = validRaw();
  unknownFeature.featureLists.add(zc::str("missing"));
  auto normalizedFeature = normalizePackageCompilationRequest(zc::mv(unknownFeature), service);
  ZC_REQUIRE(normalizedFeature.is<NormalizedPackageCompilationRequest>());
  auto featureResult = verifyPackageCompilationRequest(
      normalizedFeature.get<NormalizedPackageCompilationRequest>(), input);
  ZC_REQUIRE(featureResult.is<TargetSelectionIssue>());
  ZC_EXPECT(featureResult.get<TargetSelectionIssue>() == TargetSelectionIssue::UnknownRootFeature);
}

ZC_TEST("Package reservation follows selection and retains the expanded package key") {
  const auto source = R"toml([package]
name = "app"
version = "1.0.0"
edition = "2026"

[[bin]]
name = "core"
path = "src/bin/core.zom"

[dependencies]
core = { package = "provider", path = "../provider" }

[features]
default = ["fast"]
fast = []
)toml"_zc;
  const zc::StringPtr files[] = {"src/lib.zom"_zc, "src/bin/core.zom"_zc};
  auto input = workspaceFrom(source, zc::arrayPtr(files));

  auto unknownFeature = validRaw();
  unknownFeature.targetSelections.clear();
  unknownFeature.targetSelections.add(namedTarget(identity::CrateTargetKind::Binary, "core"_zc));
  unknownFeature.featureLists.add(zc::str("missing"));
  auto unknownFeatureRequest = normalize(zc::mv(unknownFeature));
  auto unknownFeatureResult = verifyPackageCompilationRequest(unknownFeatureRequest, input);
  ZC_REQUIRE(unknownFeatureResult.is<TargetSelectionIssue>());
  ZC_EXPECT(unknownFeatureResult.get<TargetSelectionIssue>() ==
            TargetSelectionIssue::UnknownRootFeature);

  auto unknownTarget = validRaw();
  unknownTarget.targetSelections.clear();
  unknownTarget.targetSelections.add(namedTarget(identity::CrateTargetKind::Binary, "missing"_zc));
  auto unknownTargetRequest = normalize(zc::mv(unknownTarget));
  auto unknownTargetResult = verifyPackageCompilationRequest(unknownTargetRequest, input);
  ZC_REQUIRE(unknownTargetResult.is<TargetSelectionIssue>());
  ZC_EXPECT(unknownTargetResult.get<TargetSelectionIssue>() == TargetSelectionIssue::UnknownTarget);

  auto raw = validRaw();
  raw.targetSelections.clear();
  raw.targetSelections.add(namedTarget(identity::CrateTargetKind::Binary, "core"_zc));
  auto request = normalize(zc::mv(raw));
  auto result = verifyPackageCompilationRequest(request, input);
  ZC_REQUIRE(result.is<PackageToolchainModuleRootFailure>());
  const auto& failure = result.get<PackageToolchainModuleRootFailure>();
  ZC_EXPECT(failure.producer() == PackageToolchainModuleRootProducer::UserTargetRoot);
  ZC_REQUIRE(failure.package().features().size() == 2);
  bool hasDefault = false;
  bool hasFast = false;
  for (const auto& feature : failure.package().features()) {
    hasDefault = hasDefault || feature.text() == "default"_zc;
    hasFast = hasFast || feature.text() == "fast"_zc;
  }
  ZC_EXPECT(hasDefault);
  ZC_EXPECT(hasFast);
  ZC_REQUIRE(failure.fieldPath().components().size() == 2);
  ZC_EXPECT(failure.fieldPath().components()[0].text() == "bin"_zc);
  ZC_EXPECT(failure.fieldPath().components()[1].text() == "core"_zc);
  ZC_REQUIRE(failure.argument().path().size() == 1);
  ZC_EXPECT(failure.argument().path()[0].text() == "core"_zc);
  ZC_EXPECT(PackageToolchainModuleRootFailureVerifier::verify(failure, request, input.root(),
                                                              failure.package()));

  auto wrongPackageFailure = PackageToolchainModuleRootFailure::userTargetRoot(
      input.root().binaries()[0], packageKey("other"_zc));
  ZC_REQUIRE(wrongPackageFailure != zc::none);
  ZC_IF_SOME(value, wrongPackageFailure) {
    ZC_EXPECT(!PackageToolchainModuleRootFailureVerifier::verify(value, request, input.root(),
                                                                 failure.package()));
  }

  auto wrongProducerFailure = PackageToolchainModuleRootFailure::dependencyAlias(
      input.root().targetDependencies()[0], failure.package().clone());
  ZC_REQUIRE(wrongProducerFailure != zc::none);
  ZC_IF_SOME(value, wrongProducerFailure) {
    ZC_EXPECT(!PackageToolchainModuleRootFailureVerifier::verify(value, request, input.root(),
                                                                 failure.package()));
  }
  ZC_EXPECT(PackageToolchainModuleRootFailure::userTargetRoot(
                input.root().library(), failure.package().clone()) == zc::none);
}

ZC_TEST("Package reservation orders aliases by origin-free record before provenance") {
  const auto source = R"toml([package]
name = "app"
version = "1.0.0"
edition = "2026"

[build-dependencies]
core = { package = "zzzz", path = "../zzzz" }

[dependencies]
core = { package = "aaaa", path = "../aaaa" }

[dev-dependencies]
core = { package = "bbbb", path = "../bbbb" }
)toml"_zc;
  const zc::StringPtr files[] = {"src/lib.zom"_zc};
  auto input = workspaceFrom(source, zc::arrayPtr(files));
  auto raw = validRaw();
  auto request = normalize(zc::mv(raw));
  auto result = verifyPackageCompilationRequest(request, input);
  ZC_REQUIRE(result.is<PackageToolchainModuleRootFailure>());
  const auto& failure = result.get<PackageToolchainModuleRootFailure>();
  ZC_EXPECT(failure.producer() == PackageToolchainModuleRootProducer::DependencyAlias);
  ZC_REQUIRE(failure.fieldPath().components().size() == 2);
  ZC_EXPECT(failure.fieldPath().components()[0].text() == "dependencies"_zc);
  ZC_EXPECT(failure.fieldPath().components()[1].text() == "core"_zc);
  const auto firstAlias = offsetOf(source, "core = { package"_zc);
  const auto selectedAlias = offsetOf(source, "core = { package"_zc, firstAlias + 1);
  ZC_EXPECT(failure.provenance().primary().manifestSpan().byteStart() == selectedAlias);
  ZC_EXPECT(PackageToolchainModuleRootFailureVerifier::verify(failure, request, input.root(),
                                                              failure.package()));

  auto nonWinningFailure = PackageToolchainModuleRootFailure::dependencyAlias(
      input.root().buildDependencies()[0], failure.package().clone());
  ZC_REQUIRE(nonWinningFailure != zc::none);
  ZC_IF_SOME(value, nonWinningFailure) {
    ZC_EXPECT(!PackageToolchainModuleRootFailureVerifier::verify(value, request, input.root(),
                                                                 failure.package()));
  }
}

}  // namespace zomlang::compiler::driver::package
