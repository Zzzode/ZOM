// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/ownership/ownership-event-overlay.h"

#include "zc/core/time.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/driver/compiler-session.h"
#include "zomlang/compiler/driver/package/manifest-parser.h"
#include "zomlang/compiler/driver/package/source-record.h"
#include "zomlang/compiler/ir/target-registry.h"
#include "zomlang/compiler/mir/built-mir.h"

namespace zomlang::compiler::ownership {
namespace {

namespace driver = zomlang::compiler::driver;
namespace package = driver::package;

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto result = Scalar::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid ownership fixture scalar");
}

identity::SortedFeatureSet emptyFeatures() {
  zc::Vector<identity::FeatureName> values;
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid ownership fixture feature set");
}

identity::CanonicalPackageSource packageSource() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  return identity::CanonicalPackageSource::localPath(
      identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments)));
}

identity::PackageBaseKey packageBase() {
  return identity::PackageBaseKey::from(packageSource(), scalar<identity::PackageName>("app"_zc),
                                        scalar<identity::ResolvedVersion>("1.0.0"_zc));
}

identity::PackageKey packageKey() {
  return identity::PackageKey::from(packageSource(), scalar<identity::PackageName>("app"_zc),
                                    scalar<identity::ResolvedVersion>("1.0.0"_zc), emptyFeatures());
}

identity::CanonicalRelativePath mainPath() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(scalar<identity::CanonicalPathSegment>("src"_zc));
  segments.add(scalar<identity::CanonicalPathSegment>("main.zom"_zc));
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

identity::CanonicalTargetSpecificationKey targetProjection() {
  zc::Vector<identity::TargetFeatureName> features;
  auto sorted = identity::SortedTargetFeatureSet::from(zc::mv(features));
  ZC_REQUIRE(sorted != zc::none);
  ZC_IF_SOME(values, sorted) {
    auto result = identity::CanonicalTargetSpecificationKey::from(
        scalar<identity::TargetComponentName>("x86_64"_zc),
        scalar<identity::TargetComponentName>("zom"_zc),
        scalar<identity::TargetComponentName>("none"_zc),
        scalar<identity::TargetComponentName>("unknown"_zc),
        scalar<identity::TargetComponentName>("zom-v1"_zc), 64, identity::Endianness::Little,
        zc::mv(values));
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("invalid ownership fixture target projection");
}

package::RegisteredTargetProfileName targetProfileName() {
  auto result = package::RegisteredTargetProfileName::from("host"_zc);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid ownership fixture target profile name");
}

ir::TargetRegistrySnapshot targetRegistry() {
  zc::Vector<ir::CanonicalTargetFeature> targetFeatures;
  auto targetSpec = ir::CanonicalTargetSpec::from(
      "x86_64-zom-none"_zc, "e-p:64:64"_zc, "generic"_zc, zc::mv(targetFeatures), "zom-v1"_zc,
      ir::BackendPanicStrategy::Unwind, ir::ObjectFormat::Elf);
  ZC_REQUIRE(targetSpec != zc::none);
  zc::Vector<identity::TargetFeatureName> semanticFeatures;
  zc::Vector<ir::CanonicalTargetSpec> specifications;
  ZC_IF_SOME(value, targetSpec) { specifications.add(zc::mv(value)); }
  auto profile = ir::RegisteredTargetProfileRecord::from(
      targetProfileName(), targetProjection(), zc::mv(semanticFeatures), zc::mv(specifications));
  ZC_REQUIRE(profile != zc::none);
  zc::Vector<ir::RegisteredTargetProfileRecord> profiles;
  ZC_IF_SOME(value, profile) { profiles.add(zc::mv(value)); }
  auto registry = ir::TargetRegistrySnapshot::from(targetProfileName(), zc::mv(profiles));
  ZC_IF_SOME(value, registry) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid ownership fixture target registry");
}

package::RegisteredTargetSelection targetSelection(const ir::TargetRegistrySnapshot& registry) {
  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  ZC_IF_SOME(targets, service) {
    auto result = targets.select(zc::none, package::PackagePanicStrategy::Unwind);
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("invalid ownership fixture target selection");
}

ir::VerifiedTargetSelection verifiedTargetSelection(const ir::TargetRegistrySnapshot& registry) {
  auto result = registry.verify(targetSelection(registry));
  ZC_REQUIRE(result.is<ir::VerifiedTargetSelection>());
  return zc::mv(result.get<ir::VerifiedTargetSelection>());
}

package::VerifiedPackageCompilationRequest compilationRequest(
    const ir::TargetRegistrySnapshot& registry) {
  zc::Vector<package::VerifiedCompilationRoot> roots;
  roots.add(package::VerifiedCompilationRoot::from(packageKey(), identity::CrateTargetKind::Binary,
                                                   scalar<identity::TargetName>("app"_zc), 2026,
                                                   false, mainPath()));
  auto result = package::VerifiedPackageCompilationRequest::from(
      zc::mv(roots), targetSelection(registry), targetSelection(registry),
      package::SelectedLanguageOptions{}, package::PackageLockMode::PreferLocked);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid ownership fixture compilation request");
}

class MemoryFreshDirectory final : public package::FreshSourceDirectory {
public:
  MemoryFreshDirectory() : rootValue(zc::newInMemoryDirectory(zc::nullClock())) {}
  ~MemoryFreshDirectory() noexcept override = default;
  const zc::Directory& root() const override { return *rootValue; }
  zc::Maybe<package::MaterializationIssue> finish() override { return zc::none; }

private:
  zc::Own<zc::Directory> rootValue;
};

class MemoryFreshDirectoryFactory final : public package::FreshSourceDirectoryFactory {
public:
  package::FreshSourceDirectoryResult create() override {
    zc::Own<package::FreshSourceDirectory> result = zc::heap<MemoryFreshDirectory>();
    return zc::mv(result);
  }
};

package::DigestVerifiedSourceSnapshot sourceSnapshot(zc::StringPtr sourceText) {
  auto sourceDirectory = zc::newInMemoryDirectory(zc::nullClock());
  sourceDirectory
      ->openFile(zc::Path({"src"_zc, "main.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll(sourceText);
  MemoryFreshDirectoryFactory factory;
  package::SourceDirectoryMaterializer materializer;
  auto result = materializer.materialize(*sourceDirectory, factory);
  ZC_REQUIRE(result.is<package::DigestVerifiedSourceSnapshot>());
  return zc::mv(result.get<package::DigestVerifiedSourceSnapshot>());
}

package::ResolutionOutput resolution(zc::MemoryResource& resource, zc::StringPtr sourceText) {
  package::ManifestParser parser;
  zc::Vector<identity::CanonicalRelativePath> files;
  auto inventory = package::PackageSourceInventory::from(zc::mv(files));
  ZC_REQUIRE(inventory != zc::none);
  package::NormalizedManifest normalized = [&]() {
    ZC_IF_SOME(sourceInventory, inventory) {
      zc::Vector<identity::CanonicalPathSegment> documentSegments;
      documentSegments.add(scalar<identity::CanonicalPathSegment>("Zom.toml"_zc));
      auto parsed = parser.parseWorkspaceManifest(
          identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(documentSegments)),
          "[package]\nname = \"app\"\nversion = \"1.0.0\"\nedition = \"2026\"\n"_zc,
          sourceInventory);
      ZC_REQUIRE(parsed.is<package::NormalizedManifest>());
      return zc::mv(parsed.get<package::NormalizedManifest>());
    }
    ZC_UNREACHABLE
  }();
  auto record = package::LocalPackageRecord::from(packageBase(), zc::mv(normalized),
                                                  sourceSnapshot(sourceText));
  ZC_REQUIRE(record != zc::none);
  zc::Vector<package::ResolverRelease> releases;
  ZC_IF_SOME(value, record) { releases.add(package::ResolverRelease::fromLocal(value)); }
  zc::Vector<package::ResolverRoot> roots;
  roots.add(package::ResolverRoot::from(packageBase(), emptyFeatures(), false, false));
  auto result = package::PackageResolver::resolve(resource, roots, releases);
  ZC_REQUIRE(result.is<package::ResolutionOutput>());
  return zc::mv(result.get<package::ResolutionOutput>());
}

zc::Vector<package::ResolvedPackageSourceSnapshot> resolvedSnapshots(zc::StringPtr sourceText) {
  zc::Vector<package::ResolvedPackageSourceSnapshot> snapshots;
  snapshots.add(
      package::ResolvedPackageSourceSnapshot::from(packageBase(), sourceSnapshot(sourceText)));
  return snapshots;
}

class OwnershipPipelineFixture final {
public:
  explicit OwnershipPipelineFixture(zc::StringPtr sourceText)
      : session(contextFactory, languageOptions, compilerOptions) {
    auto registry = targetRegistry();
    auto input = driver::VerifiedPackageSessionInput::from(
        compilationRequest(registry), verifiedTargetSelection(registry),
        verifiedTargetSelection(registry),
        resolution(session.getPackageResolutionMemoryResource(), sourceText),
        resolvedSnapshots(sourceText));
    ZC_REQUIRE(input != zc::none);
    ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }
    const auto roots = session.getFinalizedCompilationRoots();
    ZC_REQUIRE(roots.size() == 1);
    ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
    ZC_REQUIRE(session.parseSources());
    ZC_REQUIRE(session.bindSources());
    ZC_REQUIRE(session.checkSources());
    ZC_REQUIRE(!session.getDiagnosticEngine().hasErrors());
    ZC_REQUIRE(session.getVerifiedBuiltMirModules().size() == 1);
  }

  const mir::VerifiedBuiltMir& builtMir() const { return session.getVerifiedBuiltMirModules()[0]; }

  driver::CompilerSession& compilerSession() noexcept { return session; }

  const identity::SemanticIdentityRegistrySet& registries() const {
    auto result = session.getIdentityRegistries();
    ZC_REQUIRE(result != zc::none);
    ZC_IF_SOME(value, result) { return value; }
    ZC_UNREACHABLE
  }

private:
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  driver::CompilerSession session;
};

ZC_TEST("Ownership event overlay builder and verifier accept one scalar initializer module") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);
  const auto& builtMir = fixture.builtMir();
  const auto& registries = fixture.registries();
  ZC_REQUIRE(builtMir.functions().size() == 1);

  auto candidateResult = OwnershipEventOverlayBuilder::build(builtMir, registries);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_EXPECT(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].slots.size() != 0);

  auto verifiedResult =
      OwnershipEventOverlayVerifier::verify(zc::mv(candidate), builtMir, registries);
  ZC_REQUIRE(verifiedResult.isVerified());
  auto overlay = zc::mv(verifiedResult).takeVerified();
  ZC_EXPECT(overlay.semanticContext() == builtMir.semanticContext());
  ZC_EXPECT(overlay.module() == builtMir.module());
  ZC_EXPECT(overlay.builtRevision().digest() == builtMir.revision().digest());
  ZC_EXPECT(overlay.functions().size() == 1);
  ZC_EXPECT(overlay.functions()[0].owner == builtMir.functions()[0].owner);
  ZC_EXPECT(overlay.functions()[0].slots.size() != 0);

  const auto& slots = overlay.functions()[0].slots;
  bool foundStorageLive = false;
  bool foundAssign = false;
  bool foundReturn = false;
  bool foundSourceStage = false;
  bool foundEffectStage = false;
  bool foundCommitStage = false;
  for (const auto& slot : slots) {
    if (slot.stage == OwnershipEventStage::Source) foundSourceStage = true;
    if (slot.stage == OwnershipEventStage::Effect) foundEffectStage = true;
    if (slot.stage == OwnershipEventStage::Commit) foundCommitStage = true;
    for (auto role : slot.roles) {
      if (role == OwnershipEventRole::StorageLive) foundStorageLive = true;
      if (role == OwnershipEventRole::DestinationWrite) foundAssign = true;
      if (role == OwnershipEventRole::Operation && slot.stage == OwnershipEventStage::Effect)
        foundReturn = true;
    }
  }
  ZC_EXPECT(foundStorageLive);
  ZC_EXPECT(foundAssign);
  ZC_EXPECT(foundReturn);
  ZC_EXPECT(foundSourceStage);
  ZC_EXPECT(foundEffectStage);
  ZC_EXPECT(foundCommitStage);
}

ZC_TEST("Ownership event overlay verifier rejects a tampered function slot count") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);
  const auto& builtMir = fixture.builtMir();
  const auto& registries = fixture.registries();

  auto candidateResult = OwnershipEventOverlayBuilder::build(builtMir, registries);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  MirEventSlot extra{MirEventKey{MirEventLocation{mir::MirBlockId{}, 999}, 0},
                     OwnershipEventStage::Effect, zc::Vector<OwnershipEventRole>{}};
  extra.roles.add(OwnershipEventRole::Operation);
  candidate.functions[0].slots.add(zc::mv(extra));

  auto verifiedResult =
      OwnershipEventOverlayVerifier::verify(zc::mv(candidate), builtMir, registries);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::CanonicalCodecMismatch);
}

ZC_TEST("Ownership event overlay revision is deterministic across two builds") {
  OwnershipPipelineFixture first("let value = 0;"_zc);
  auto firstResult = OwnershipEventOverlayBuilder::build(first.builtMir(), first.registries());
  ZC_REQUIRE(firstResult.isVerified());
  auto firstCandidate = zc::mv(firstResult).takeVerified();
  auto firstVerified = OwnershipEventOverlayVerifier::verify(zc::mv(firstCandidate),
                                                             first.builtMir(), first.registries());
  ZC_REQUIRE(firstVerified.isVerified());
  auto firstOverlay = zc::mv(firstVerified).takeVerified();

  OwnershipPipelineFixture second("let value = 0;"_zc);
  auto secondResult = OwnershipEventOverlayBuilder::build(second.builtMir(), second.registries());
  ZC_REQUIRE(secondResult.isVerified());
  auto secondCandidate = zc::mv(secondResult).takeVerified();
  auto secondVerified = OwnershipEventOverlayVerifier::verify(
      zc::mv(secondCandidate), second.builtMir(), second.registries());
  ZC_REQUIRE(secondVerified.isVerified());
  auto secondOverlay = zc::mv(secondVerified).takeVerified();

  ZC_EXPECT(firstOverlay.revision().digest() == secondOverlay.revision().digest());
}

ZC_TEST("Ownership event overlay verifier rejects a tampered slot role") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);
  const auto& builtMir = fixture.builtMir();
  const auto& registries = fixture.registries();

  auto candidateResult = OwnershipEventOverlayBuilder::build(builtMir, registries);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].slots.size() != 0);
  ZC_REQUIRE(candidate.functions[0].slots[0].roles.size() != 0);
  auto originalRole = candidate.functions[0].slots[0].roles[0];
  auto newRole = originalRole == OwnershipEventRole::Operation ? OwnershipEventRole::StorageLive
                                                               : OwnershipEventRole::Operation;
  candidate.functions[0].slots[0].roles[0] = newRole;

  auto verifiedResult =
      OwnershipEventOverlayVerifier::verify(zc::mv(candidate), builtMir, registries);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::CanonicalCodecMismatch);
}

ZC_TEST("CompilerSession publishes verified ownership event overlays") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);
  const auto& session = fixture.compilerSession();
  ZC_REQUIRE(session.getVerifiedBuiltMirModules().size() == 1);
  ZC_REQUIRE(session.getVerifiedOwnershipEventOverlays().size() == 1);
  const auto& overlay = session.getVerifiedOwnershipEventOverlays()[0];
  const auto& builtMir = session.getVerifiedBuiltMirModules()[0];
  ZC_EXPECT(overlay.module() == builtMir.module());
  ZC_EXPECT(overlay.builtRevision().digest() == builtMir.revision().digest());
  ZC_EXPECT(overlay.functions().size() == 1);
  ZC_EXPECT(overlay.functions()[0].owner == builtMir.functions()[0].owner);
  ZC_EXPECT(session.getIrFailureGroups().size() == 0);
  ZC_EXPECT(session.getIrIdentityInvariantFailures().size() == 0);
}

}  // namespace
}  // namespace zomlang::compiler::ownership
