// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/ownership/ownership-event-overlay.h"

#include "zc/core/encoding.h"
#include "zc/core/time.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/diagnostics/consumer/diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/core/diagnostic-engine.h"
#include "zomlang/compiler/driver/interface/borrow-evidence.h"
#include "zomlang/compiler/driver/package/manifest-parser.h"
#include "zomlang/compiler/driver/package/source-record.h"
#include "zomlang/compiler/driver/session/compiler-session.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"
#include "zomlang/compiler/identity/crypto/sha256.h"
#include "zomlang/compiler/ir/target-registry.h"
#include "zomlang/compiler/mir/built-mir.h"
#include "zomlang/compiler/ownership/facts/flow.h"
#include "zomlang/compiler/ownership/facts/init.h"
#include "zomlang/compiler/ownership/facts/loans.h"
#include "zomlang/compiler/ownership/facts/paths.h"
#include "zomlang/compiler/ownership/facts/resources.h"
#include "zomlang/compiler/ownership/ownership-diagnostic-adapter.h"
#include "zomlang/compiler/ownership/source-suppression.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/compiler/type/semantic-type-data.h"
#include "zomlang/compiler/type/semantic-type-store.h"
#include "zomlang/tests/unittests/compiler/driver/core/core-library-test-fixture.h"
#include "zomlang/tests/unittests/compiler/ownership/ownership-facts-differential-oracle.h"

namespace zomlang::compiler::ownership {
namespace {

namespace driver = zomlang::compiler::driver;
namespace package = driver::package;

identity::Sha256Digest repeatedDigest(uint8_t byte);

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
        scalar<identity::TargetComponentName>("zom"_zc), 64, identity::Endianness::Little,
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
      "x86_64-zom-none"_zc, "e-p:64:64"_zc, "generic"_zc, zc::mv(targetFeatures), "zom"_zc,
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
    driver::core_library_test::installCoreDistribution(session);
    const auto roots = session.getFinalizedCompilationRoots();
    ZC_REQUIRE(roots.size() == 1);
    ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
    ZC_REQUIRE(session.parseSources());
    ZC_REQUIRE(session.bindSources());
    ZC_REQUIRE(session.checkSources());
    ZC_REQUIRE(!session.getDiagnosticEngine().hasErrors());
    ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  }

  const mir::VerifiedBuiltMir& builtMir() const {
    return session.getOwnershipCheckedMirModules()[0].builtMir();
  }

  checker::CheckerIdentityAuthority identities() const {
    auto authority = session.materializeCheckerIdentityAuthority();
    ZC_REQUIRE(authority != zc::none);
    ZC_IF_SOME(value, authority) { return zc::mv(value); }
    ZC_UNREACHABLE
  }

  identity::RegistryBrand substitutionBrand() const {
    const auto repository = session.getCheckedFactsRepository();
    const auto leases = session.getCheckedEvidenceLeases();
    ZC_REQUIRE(repository != zc::none);
    ZC_REQUIRE(leases.size() == 1);
    ZC_IF_SOME(value, repository) {
      auto facts = value.lookup(leases[0]);
      ZC_REQUIRE(facts != zc::none);
      ZC_IF_SOME(verified, facts) { return verified.substitutionStore().issuer(); }
    }
    ZC_UNREACHABLE
  }

  driver::borrow_evidence::VerifiedBorrowEvidence cloneBorrowEvidence() const {
    const auto repository = session.getBorrowEvidenceRepository();
    ZC_REQUIRE(repository != zc::none);
    ZC_IF_SOME(value, repository) {
      const auto evidence = value.capability().lookup(builtMir().borrowEvidenceLease());
      ZC_REQUIRE(evidence.isResolved());
      return evidence.evidence().clone();
    }
    ZC_UNREACHABLE
  }

  OwnershipEventOverlayInput overlayInput() const {
    auto input = session.getOwnershipEventOverlayInput(builtMir().module());
    ZC_REQUIRE(input != zc::none);
    ZC_IF_SOME(value, input) { return zc::mv(value); }
    ZC_UNREACHABLE
  }

  driver::CompilerSession& compilerSession() noexcept { return session; }
  const driver::CompilerSession& compilerSession() const noexcept { return session; }

private:
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  driver::CompilerSession session;
};

/// Fixture that runs the session pipeline for a source the borrow checker
/// rejects at the borrow-source stage (a returned function-local borrow,
/// ZOM4061). checkSources() fails, so the committed ownership facts are
/// unavailable; the Built MIR, event overlay, and borrow-evidence repository
/// staged just before the rejection are exposed for fact reconstruction.
class RejectedBorrowPipelineFixture final {
public:
  explicit RejectedBorrowPipelineFixture(zc::StringPtr sourceText)
      : session(contextFactory, languageOptions, compilerOptions) {
    auto registry = targetRegistry();
    auto input = driver::VerifiedPackageSessionInput::from(
        compilationRequest(registry), verifiedTargetSelection(registry),
        verifiedTargetSelection(registry),
        resolution(session.getPackageResolutionMemoryResource(), sourceText),
        resolvedSnapshots(sourceText));
    ZC_REQUIRE(input != zc::none);
    ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }
    driver::core_library_test::installCoreDistribution(session);
    const auto roots = session.getFinalizedCompilationRoots();
    ZC_REQUIRE(roots.size() == 1);
    ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
    ZC_REQUIRE(session.parseSources());
    ZC_REQUIRE(session.bindSources());
    ZC_REQUIRE(!session.checkSources());
    ZC_REQUIRE(session.getDiagnosticEngine().hasErrors());
    ZC_REQUIRE(session.firstStagedBorrowSourceRejectionForTesting() != zc::none);
  }

  const mir::VerifiedBuiltMir& builtMir() const {
    ZC_IF_SOME(staged, session.firstStagedBorrowSourceRejectionForTesting()) {
      return staged.builtMir;
    }
    ZC_UNREACHABLE
  }

  const VerifiedOwnershipEventOverlay& overlay() const {
    ZC_IF_SOME(staged, session.firstStagedBorrowSourceRejectionForTesting()) {
      return staged.eventOverlay;
    }
    ZC_UNREACHABLE
  }

  driver::borrow_evidence::VerifiedBorrowEvidence cloneBorrowEvidence() const {
    ZC_IF_SOME(staged, session.firstStagedBorrowSourceRejectionForTesting()) {
      const auto evidence =
          staged.borrowEvidence.capability().lookup(builtMir().borrowEvidenceLease());
      ZC_REQUIRE(evidence.isResolved());
      return evidence.evidence().clone();
    }
    ZC_UNREACHABLE
  }

  driver::CompilerSession& compilerSession() noexcept { return session; }
  const driver::CompilerSession& compilerSession() const noexcept { return session; }

private:
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  driver::CompilerSession session;
};

ir::IrOperationResult<OwnershipEventOverlayCandidate> buildOverlay(
    const OwnershipPipelineFixture& fixture) {
  return OwnershipEventOverlayBuilder::build(fixture.overlayInput());
}

ir::IrOperationResult<VerifiedOwnershipEventOverlay> verifyOverlay(
    OwnershipEventOverlayCandidate&& candidate, const OwnershipPipelineFixture& fixture) {
  return OwnershipEventOverlayVerifier::verify(zc::mv(candidate), fixture.overlayInput());
}

const facts::VerifiedOwnershipInputs& ownershipInputs(const driver::CompilerSession& session) {
  const auto checkedMir = session.getOwnershipCheckedMirModules();
  ZC_REQUIRE(checkedMir.size() == 1);
  return checkedMir[0].facts();
}

ir::IrOperationResult<facts::VerifiedOwnershipInputs> verifyOwnershipInputs(
    const OwnershipPipelineFixture& fixture,
    const driver::borrow_evidence::VerifiedBorrowEvidenceLease& lease,
    const driver::borrow_evidence::BorrowEvidenceRepositoryCapability& capability) {
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();

  auto movePathCandidate = facts::MovePathBuilder::build(builtMir, overlay);
  ZC_REQUIRE(movePathCandidate.isVerified());
  auto movePaths =
      facts::MovePathVerifier::verify(zc::mv(movePathCandidate).takeVerified(), builtMir, overlay);
  ZC_REQUIRE(movePaths.isVerified());

  auto flowCandidate = facts::FlowBuilder::build(builtMir, overlay);
  ZC_REQUIRE(flowCandidate.isVerified());
  auto flow = facts::FlowVerifier::verify(zc::mv(flowCandidate).takeVerified(), builtMir, overlay);
  ZC_REQUIRE(flow.isVerified());

  auto initializationCandidate = facts::InitializationBuilder::build(
      builtMir, overlay, flow.verifiedValue(), movePaths.verifiedValue());
  ZC_REQUIRE(initializationCandidate.isVerified());
  auto initialization = facts::InitializationVerifier::verify(
      zc::mv(initializationCandidate).takeVerified(), builtMir, overlay, flow.verifiedValue(),
      movePaths.verifiedValue());
  ZC_REQUIRE(initialization.isVerified());

  auto loanCandidate = facts::LoanBuilder::build(movePaths.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(loanCandidate.isVerified());
  auto loans = facts::LoanVerifier::verify(zc::mv(loanCandidate).takeVerified(),
                                           movePaths.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(loans.isVerified());

  auto referenceCandidate = facts::ReferenceDefinitionBuilder::build(
      movePaths.verifiedValue(), loans.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(referenceCandidate.isVerified());
  auto references = facts::ReferenceDefinitionVerifier::verify(
      zc::mv(referenceCandidate).takeVerified(), movePaths.verifiedValue(), loans.verifiedValue(),
      builtMir, overlay);
  ZC_REQUIRE(references.isVerified());

  auto regionCandidate = facts::ReborrowRegionBuilder::build(
      flow.verifiedValue(), loans.verifiedValue(), references.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(regionCandidate.isVerified());
  auto regions = facts::ReborrowRegionVerifier::verify(
      zc::mv(regionCandidate).takeVerified(), flow.verifiedValue(), loans.verifiedValue(),
      references.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(regions.isVerified());

  auto stateCandidate = facts::ReborrowStateBuilder::build(
      references.verifiedValue(), regions.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(stateCandidate.isVerified());
  auto states = facts::ReborrowStateVerifier::verify(zc::mv(stateCandidate).takeVerified(),
                                                     references.verifiedValue(),
                                                     regions.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(states.isVerified());

  auto resourceCandidate =
      facts::OwnershipResourceBuilder::build(movePaths.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(resourceCandidate.isVerified());
  auto resources = facts::OwnershipResourceVerifier::verify(
      zc::mv(resourceCandidate).takeVerified(), movePaths.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(resources.isVerified());

  auto captureCandidate =
      facts::CaptureBuilder::build(movePaths.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(captureCandidate.isVerified());
  auto captures = facts::CaptureVerifier::verify(zc::mv(captureCandidate).takeVerified(),
                                                 movePaths.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(captures.isVerified());

  auto membershipCandidate = facts::RegionMembershipBuilder::build(
      flow.verifiedValue(), loans.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(membershipCandidate.isVerified());
  auto memberships = facts::RegionMembershipVerifier::verify(
      zc::mv(membershipCandidate).takeVerified(), flow.verifiedValue(), loans.verifiedValue(),
      builtMir, overlay);
  ZC_REQUIRE(memberships.isVerified());

  auto escapeCandidate = facts::EscapeBuilder::build(
      flow.verifiedValue(), loans.verifiedValue(), references.verifiedValue(),
      resources.verifiedValue(), captures.verifiedValue(), memberships.verifiedValue(), builtMir,
      overlay);
  ZC_REQUIRE(escapeCandidate.isVerified());
  auto escapes = facts::EscapeVerifier::verify(
      zc::mv(escapeCandidate).takeVerified(), flow.verifiedValue(), loans.verifiedValue(),
      references.verifiedValue(), resources.verifiedValue(), captures.verifiedValue(),
      memberships.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(escapes.isVerified());

  auto outlivesCandidate =
      facts::RegionOutlivesBuilder::build(memberships.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(outlivesCandidate.isVerified());
  auto outlives = facts::RegionOutlivesVerifier::verify(
      zc::mv(outlivesCandidate).takeVerified(), memberships.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(outlives.isVerified());

  auto overlayInput = fixture.overlayInput();
  return facts::OwnershipInputVerifier::verify(
      zc::mv(movePaths).takeVerified(), zc::mv(flow).takeVerified(),
      zc::mv(initialization).takeVerified(), zc::mv(loans).takeVerified(),
      zc::mv(references).takeVerified(), zc::mv(regions).takeVerified(),
      zc::mv(states).takeVerified(), zc::mv(resources).takeVerified(),
      zc::mv(escapes).takeVerified(), zc::mv(captures).takeVerified(),
      zc::mv(outlives).takeVerified(), builtMir, overlay, lease, capability,
      overlayInput.body.semanticTypes);
}

/// Fact families rebuilt from a rejected-borrow fixture's staged Built MIR and
/// event overlay. The borrow checker rejects the only sources that produce a
/// function-local loan, so these families cannot be read from committed session
/// facts and are reconstructed here from the same builders the production
/// pipeline runs.
struct RejectedBorrowInventory final {
  facts::VerifiedMovePaths movePaths;
  facts::VerifiedFlow flow;
  facts::VerifiedInitializationFacts initialization;
  facts::VerifiedLoanFacts loans;
  facts::VerifiedReferenceDefinitions references;
  facts::VerifiedReborrowRegions regions;
  facts::VerifiedReborrowStates states;
  facts::VerifiedOwnershipResourceFacts resources;
};

RejectedBorrowInventory buildRejectedBorrowInventory(const RejectedBorrowPipelineFixture& fixture) {
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = fixture.overlay();

  auto movePathCandidate = facts::MovePathBuilder::build(builtMir, overlay);
  ZC_REQUIRE(movePathCandidate.isVerified());
  auto movePaths =
      facts::MovePathVerifier::verify(zc::mv(movePathCandidate).takeVerified(), builtMir, overlay);
  ZC_REQUIRE(movePaths.isVerified());

  auto flowCandidate = facts::FlowBuilder::build(builtMir, overlay);
  ZC_REQUIRE(flowCandidate.isVerified());
  auto flow = facts::FlowVerifier::verify(zc::mv(flowCandidate).takeVerified(), builtMir, overlay);
  ZC_REQUIRE(flow.isVerified());

  auto initializationCandidate = facts::InitializationBuilder::build(
      builtMir, overlay, flow.verifiedValue(), movePaths.verifiedValue());
  ZC_REQUIRE(initializationCandidate.isVerified());
  auto initialization = facts::InitializationVerifier::verify(
      zc::mv(initializationCandidate).takeVerified(), builtMir, overlay, flow.verifiedValue(),
      movePaths.verifiedValue());
  ZC_REQUIRE(initialization.isVerified());

  auto loanCandidate = facts::LoanBuilder::build(movePaths.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(loanCandidate.isVerified());
  auto loans = facts::LoanVerifier::verify(zc::mv(loanCandidate).takeVerified(),
                                           movePaths.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(loans.isVerified());

  auto referenceCandidate = facts::ReferenceDefinitionBuilder::build(
      movePaths.verifiedValue(), loans.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(referenceCandidate.isVerified());
  auto references = facts::ReferenceDefinitionVerifier::verify(
      zc::mv(referenceCandidate).takeVerified(), movePaths.verifiedValue(), loans.verifiedValue(),
      builtMir, overlay);
  ZC_REQUIRE(references.isVerified());

  auto regionCandidate = facts::ReborrowRegionBuilder::build(
      flow.verifiedValue(), loans.verifiedValue(), references.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(regionCandidate.isVerified());
  auto regions = facts::ReborrowRegionVerifier::verify(
      zc::mv(regionCandidate).takeVerified(), flow.verifiedValue(), loans.verifiedValue(),
      references.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(regions.isVerified());

  auto stateCandidate = facts::ReborrowStateBuilder::build(
      references.verifiedValue(), regions.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(stateCandidate.isVerified());
  auto states = facts::ReborrowStateVerifier::verify(zc::mv(stateCandidate).takeVerified(),
                                                     references.verifiedValue(),
                                                     regions.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(states.isVerified());

  auto resourceCandidate =
      facts::OwnershipResourceBuilder::build(movePaths.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(resourceCandidate.isVerified());
  auto resources = facts::OwnershipResourceVerifier::verify(
      zc::mv(resourceCandidate).takeVerified(), movePaths.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(resources.isVerified());

  return RejectedBorrowInventory{
      zc::mv(movePaths).takeVerified(),      zc::mv(flow).takeVerified(),
      zc::mv(initialization).takeVerified(), zc::mv(loans).takeVerified(),
      zc::mv(references).takeVerified(),     zc::mv(regions).takeVerified(),
      zc::mv(states).takeVerified(),         zc::mv(resources).takeVerified()};
}

ZC_TEST("Ownership event overlay builder and verifier accept one scalar initializer module") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);
  const auto& builtMir = fixture.builtMir();
  ZC_REQUIRE(builtMir.functions().size() == 1);

  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].slots.size() == 7);
  ZC_REQUIRE(candidate.functions[0].sourceMap.size() == 7);
  ZC_REQUIRE(candidate.functions[0].markerUses.size() ==
             builtMir.functions()[0].locals.size() * 2 + 1);
  ZC_REQUIRE(candidate.functions[0].logicalDropPlans.size() == 1);
  const auto owner = candidate.functions[0].owner;
  const auto block = builtMir.functions()[0].blocks[0].id;
  const auto& candidateSlots = candidate.functions[0].slots;
  const auto& sourceMap = candidate.functions[0].sourceMap;
  const auto& markerUses = candidate.functions[0].markerUses;

  bool foundPositive = false;
  bool foundUnsatisfied = false;
  for (const auto& use : markerUses) {
    ZC_EXPECT(use.key.event.location.owner == owner);
    if (use.key.event.location.point.kind() == MirPointKind::BeforeStatement) {
      ZC_EXPECT(use.key.event.location.point.beforeStatementValue().block == block);
      ZC_EXPECT(use.key.event.location.point.beforeStatementValue().ordinal == 1);
      ZC_EXPECT(use.key.event.operandOrdinal == 2);
    } else {
      ZC_EXPECT(use.key.event.location.point.kind() == MirPointKind::BeforeTerminator);
      ZC_EXPECT(use.key.event.location.point.beforeTerminatorValue().block == block);
      ZC_EXPECT(use.key.event.operandOrdinal == 0);
    }
    ZC_EXPECT(use.key.subject == builtMir.functions()[0].locals[0].type);
    if (use.decision.is<OwnershipMarkerDecisionPositive>()) foundPositive = true;
    if (use.decision.is<OwnershipMarkerDecisionUnsatisfied>()) foundUnsatisfied = true;
  }
  ZC_EXPECT(foundPositive);
  ZC_EXPECT(foundUnsatisfied);
  const auto& dropPlan = candidate.functions[0].logicalDropPlans[0];
  ZC_EXPECT(dropPlan.initialization.location.owner == owner);
  ZC_EXPECT(dropPlan.initialization.location.point.kind() == MirPointKind::BeforeStatement);
  ZC_EXPECT(dropPlan.initialization.location.point.beforeStatementValue().block == block);
  ZC_EXPECT(dropPlan.initialization.location.point.beforeStatementValue().ordinal == 1);
  ZC_EXPECT(dropPlan.initialization.operandOrdinal == 2);
  ZC_EXPECT(dropPlan.root.local() == builtMir.functions()[0].locals[0].id);
  ZC_EXPECT(dropPlan.root.resultType() == builtMir.functions()[0].locals[0].type);
  ZC_EXPECT(dropPlan.components.size() == 0);

  ZC_EXPECT(candidateSlots[0].key.location.owner == owner);
  ZC_EXPECT(candidateSlots[0].key.location.point.kind() == MirPointKind::Entry);
  ZC_EXPECT(candidateSlots[0].key.operandOrdinal == 0);
  ZC_EXPECT(candidateSlots[0].stage == OwnershipEventStage::Commit);
  ZC_REQUIRE(candidateSlots[0].roles.size() == 1);
  ZC_EXPECT(candidateSlots[0].roles[0] == OwnershipEventRole::EntryRoot);

  ZC_EXPECT(candidateSlots[1].key.location.owner == owner);
  ZC_EXPECT(candidateSlots[1].key.location.point.kind() == MirPointKind::BeforeStatement);
  ZC_EXPECT(candidateSlots[1].key.location.point.beforeStatementValue().block == block);
  ZC_EXPECT(candidateSlots[1].key.location.point.beforeStatementValue().ordinal == 0);
  ZC_EXPECT(candidateSlots[1].key.operandOrdinal == 0);
  ZC_EXPECT(candidateSlots[1].stage == OwnershipEventStage::Effect);
  ZC_REQUIRE(candidateSlots[1].roles.size() == 2);
  ZC_EXPECT(candidateSlots[1].roles[0] == OwnershipEventRole::Operation);
  ZC_EXPECT(candidateSlots[1].roles[1] == OwnershipEventRole::StorageLive);

  for (size_t index = 2; index <= 4; ++index) {
    ZC_EXPECT(candidateSlots[index].key.location.owner == owner);
    ZC_EXPECT(candidateSlots[index].key.location.point.kind() == MirPointKind::BeforeStatement);
    ZC_EXPECT(candidateSlots[index].key.location.point.beforeStatementValue().block == block);
    ZC_EXPECT(candidateSlots[index].key.location.point.beforeStatementValue().ordinal == 1);
    ZC_EXPECT(candidateSlots[index].key.operandOrdinal == index - 2);
  }
  ZC_EXPECT(candidateSlots[2].stage == OwnershipEventStage::Source);
  ZC_REQUIRE(candidateSlots[2].roles.size() == 1);
  ZC_EXPECT(candidateSlots[2].roles[0] == OwnershipEventRole::ConstantOperand);
  ZC_EXPECT(candidateSlots[3].stage == OwnershipEventStage::Effect);
  ZC_REQUIRE(candidateSlots[3].roles.size() == 1);
  ZC_EXPECT(candidateSlots[3].roles[0] == OwnershipEventRole::Operation);
  ZC_EXPECT(candidateSlots[4].stage == OwnershipEventStage::Commit);
  ZC_REQUIRE(candidateSlots[4].roles.size() == 1);
  ZC_EXPECT(candidateSlots[4].roles[0] == OwnershipEventRole::DestinationWrite);

  for (size_t index = 5; index <= 6; ++index) {
    ZC_EXPECT(candidateSlots[index].key.location.owner == owner);
    ZC_EXPECT(candidateSlots[index].key.location.point.kind() == MirPointKind::BeforeTerminator);
    ZC_EXPECT(candidateSlots[index].key.location.point.beforeTerminatorValue().block == block);
    ZC_EXPECT(candidateSlots[index].key.operandOrdinal == index - 5);
  }
  ZC_EXPECT(candidateSlots[5].stage == OwnershipEventStage::Source);
  ZC_REQUIRE(candidateSlots[5].roles.size() == 2);
  ZC_EXPECT(candidateSlots[5].roles[0] == OwnershipEventRole::OperandRead);
  ZC_EXPECT(candidateSlots[5].roles[1] == OwnershipEventRole::OperandCopy);
  ZC_EXPECT(candidateSlots[6].stage == OwnershipEventStage::Effect);
  ZC_REQUIRE(candidateSlots[6].roles.size() == 1);
  ZC_EXPECT(candidateSlots[6].roles[0] == OwnershipEventRole::Operation);
  ZC_EXPECT(sourceMap[0].span.byteStart() ==
            builtMir.functions()[0].locals[0].sourceSpan.byteStart());
  ZC_EXPECT(sourceMap[0].span.byteEnd() == builtMir.functions()[0].locals[0].sourceSpan.byteEnd());
  ZC_EXPECT(sourceMap[1].span.byteStart() ==
            builtMir.functions()[0].blocks[0].statements[0].sourceSpan().byteStart());
  ZC_EXPECT(sourceMap[1].span.byteEnd() ==
            builtMir.functions()[0].blocks[0].statements[0].sourceSpan().byteEnd());
  for (size_t index = 2; index <= 4; ++index) {
    ZC_EXPECT(sourceMap[index].span.byteStart() ==
              builtMir.functions()[0].blocks[0].statements[1].sourceSpan().byteStart());
    ZC_EXPECT(sourceMap[index].span.byteEnd() ==
              builtMir.functions()[0].blocks[0].statements[1].sourceSpan().byteEnd());
  }
  for (size_t index = 5; index <= 6; ++index) {
    ZC_EXPECT(sourceMap[index].span.byteStart() ==
              builtMir.functions()[0].blocks[0].terminator.sourceSpan().byteStart());
    ZC_EXPECT(sourceMap[index].span.byteEnd() ==
              builtMir.functions()[0].blocks[0].terminator.sourceSpan().byteEnd());
  }

  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_REQUIRE(verifiedResult.isVerified());
  auto overlay = zc::mv(verifiedResult).takeVerified();
  ZC_EXPECT(overlay.semanticContext() == builtMir.semanticContext());
  ZC_EXPECT(overlay.module() == builtMir.module());
  ZC_EXPECT(overlay.builtRevision().digest() == builtMir.revision().digest());
  ZC_EXPECT(overlay.functions().size() == 1);
  ZC_EXPECT(overlay.functions()[0].owner == builtMir.functions()[0].owner);
  ZC_EXPECT(overlay.functions()[0].slots.size() == 7);
  ZC_EXPECT(overlay.functions()[0].sourceMap.size() == 7);
}

ZC_TEST("Ownership event overlay verifier rejects a tampered event source span") {
  OwnershipPipelineFixture fixture(
      "fun helper() -> i32 { return 0; }\n"
      "fun entry() -> i32 { return helper(); }"_zc);
  const auto& builtMir = fixture.builtMir();

  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 2);
  zc::Maybe<identity::DefId> callerOwner;
  for (const auto& function : builtMir.functions()) {
    if (function.blocks.size() != 2) continue;
    callerOwner = function.owner;
  }
  ZC_REQUIRE(callerOwner != zc::none);
  zc::Maybe<identity::SourceSpan> replacement;
  ZC_IF_SOME(owner, callerOwner) {
    for (const auto& function : candidate.functions) {
      if (function.owner == owner) continue;
      ZC_REQUIRE(function.sourceMap.size() != 0);
      replacement = function.sourceMap[0].span.clone();
    }
    ZC_REQUIRE(replacement != zc::none);
    for (auto& function : candidate.functions) {
      if (function.owner != owner) continue;
      ZC_REQUIRE(function.sourceMap.size() != 0);
      auto& source = function.sourceMap[0].span;
      ZC_IF_SOME(value, replacement) {
        ZC_REQUIRE(source.byteStart() != value.byteStart() || source.byteEnd() != value.byteEnd());
        source = value.clone();
      }
    }
  }

  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() == ir::IrFailureKind::InvalidFact);
}

ZC_TEST("Ownership event overlay builder rejects a cross-session MIR capability") {
  OwnershipPipelineFixture first("let value = 0;"_zc);
  OwnershipPipelineFixture second("let value = 0;"_zc);
  auto firstInput = first.overlayInput();
  auto secondInput = second.overlayInput();
  auto firstBody = first.overlayInput();
  OwnershipEventOverlayInput mismatched{firstInput.admitted, firstInput.checked, firstInput.hir,
                                        secondInput.built, zc::mv(firstBody.body)};

  auto result = OwnershipEventOverlayBuilder::build(mismatched);
  ZC_REQUIRE(result.isIrInvariantRejected());
  ZC_REQUIRE(result.invariantFailures().facts().size() == 1);
  ZC_EXPECT(result.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InputRevisionMismatch);
}

ZC_TEST("Ownership event overlay builder rejects a body outside the admitted lease") {
  OwnershipPipelineFixture first("let value = 0;"_zc);
  OwnershipPipelineFixture second("let value = 0;"_zc);
  auto firstInput = first.overlayInput();
  auto secondInput = second.overlayInput();
  OwnershipEventOverlayInput mismatched{firstInput.admitted, firstInput.checked, firstInput.hir,
                                        firstInput.built, zc::mv(secondInput.body)};

  auto result = OwnershipEventOverlayBuilder::build(mismatched);
  ZC_REQUIRE(result.isIrInvariantRejected());
  ZC_REQUIRE(result.invariantFailures().facts().size() == 1);
  ZC_EXPECT(result.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InputRevisionMismatch);
}

ZC_TEST("Ownership event overlay verifier rejects a tampered function slot count") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);

  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  const auto& first = candidate.functions[0].slots[0];
  MirEventSlot extra{
      MirEventKey{MirLocation{candidate.functions[0].owner, first.key.location.point}, 999},
      OwnershipEventStage::Effect, zc::Vector<OwnershipEventRole>{}};
  extra.roles.add(OwnershipEventRole::Operation);
  candidate.functions[0].slots.add(zc::mv(extra));

  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::CanonicalCodecMismatch);
}

ZC_TEST("Ownership event overlay verifier rejects a missing marker use") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);

  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].markerUses.size() != 0);
  candidate.functions[0].markerUses.removeLast();

  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::CanonicalCodecMismatch);
}

ZC_TEST("Ownership event overlay verifier rejects a missing logical drop plan") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);

  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].logicalDropPlans.size() == 1);
  candidate.functions[0].logicalDropPlans.removeLast();

  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::CanonicalCodecMismatch);
}

ZC_TEST("Ownership event overlay verifier rejects a spurious scalar drop component") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);

  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  auto& function = candidate.functions[0];
  ZC_REQUIRE(function.logicalDropPlans.size() == 1);
  ZC_REQUIRE(function.markerUses.size() == 3);
  auto& plan = function.logicalDropPlans[0];
  zc::Maybe<LogicalDropAction> noAction;
  plan.components.add(LogicalDropPlanComponent{plan.root.clone(), plan.root.resultType(),
                                               zc::mv(noAction), function.markerUses[0].key,
                                               function.markerUses[1].key, 0});

  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::CanonicalCodecMismatch);
}

ZC_TEST("Ownership event overlay revision is deterministic across two builds") {
  OwnershipPipelineFixture first("let value = 0;"_zc);
  auto firstResult = buildOverlay(first);
  ZC_REQUIRE(firstResult.isVerified());
  auto firstCandidate = zc::mv(firstResult).takeVerified();
  auto firstVerified = verifyOverlay(zc::mv(firstCandidate), first);
  ZC_REQUIRE(firstVerified.isVerified());
  auto firstOverlay = zc::mv(firstVerified).takeVerified();

  OwnershipPipelineFixture second("let value = 0;"_zc);
  auto secondResult = buildOverlay(second);
  ZC_REQUIRE(secondResult.isVerified());
  auto secondCandidate = zc::mv(secondResult).takeVerified();
  auto secondVerified = verifyOverlay(zc::mv(secondCandidate), second);
  ZC_REQUIRE(secondVerified.isVerified());
  auto secondOverlay = zc::mv(secondVerified).takeVerified();

  ZC_EXPECT(firstOverlay.revision().digest() == secondOverlay.revision().digest());
}

ZC_TEST("Ownership event overlay verifier rejects a tampered slot role") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);

  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].slots.size() != 0);
  ZC_REQUIRE(candidate.functions[0].slots[0].roles.size() != 0);
  auto originalRole = candidate.functions[0].slots[0].roles[0];
  auto newRole = originalRole == OwnershipEventRole::Operation ? OwnershipEventRole::StorageLive
                                                               : OwnershipEventRole::Operation;
  candidate.functions[0].slots[0].roles[0] = newRole;

  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::CanonicalCodecMismatch);
}

ZC_TEST("Ownership event overlay verifier rejects a foreign event owner") {
  OwnershipPipelineFixture fixture("let value = 0; let other = 1;"_zc);

  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 2);
  ZC_REQUIRE(candidate.functions[0].slots.size() != 0);
  ZC_REQUIRE(candidate.functions[0].owner != candidate.functions[1].owner);
  candidate.functions[0].slots[0].key.location.owner = candidate.functions[1].owner;

  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_EXPECT(!verifiedResult.isVerified());
}

ZC_TEST("Ownership event overlay verifier rejects a statement event at the terminator point") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);

  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].slots.size() != 0);
  const auto block =
      candidate.functions[0].slots[1].key.location.point.beforeStatementValue().block;
  candidate.functions[0].slots[1].key.location.point = MirPoint::beforeTerminator(block);

  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_EXPECT(!verifiedResult.isVerified());
}

ZC_TEST("Ownership event overlay verifier encodes and rejects mismatched control-flow points") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);
  const auto& builtMir = fixture.builtMir();
  const auto block = builtMir.functions()[0].blocks[0].id;

  auto entry = MirPoint::entry();
  auto afterStatement = MirPoint::afterStatement(block, 0);
  auto edge = MirPoint::edge(block, 0, block);
  auto exit = MirPoint::exit(block, MirExitKind::Return);
  ZC_EXPECT(entry.kind() == MirPointKind::Entry);
  ZC_EXPECT(afterStatement.kind() == MirPointKind::AfterStatement);
  ZC_EXPECT(afterStatement.afterStatementValue().block == block);
  ZC_EXPECT(afterStatement.afterStatementValue().ordinal == 0);
  ZC_EXPECT(edge.kind() == MirPointKind::Edge);
  ZC_EXPECT(edge.edgeValue().from == block);
  ZC_EXPECT(edge.edgeValue().edgeOrdinal == 0);
  ZC_EXPECT(edge.edgeValue().to == block);
  ZC_EXPECT(exit.kind() == MirPointKind::Exit);
  ZC_EXPECT(exit.exitValue().block == block);
  ZC_EXPECT(exit.exitValue().kind == MirExitKind::Return);

  auto expectRejected = [&](MirPoint point) {
    auto candidateResult = buildOverlay(fixture);
    ZC_REQUIRE(candidateResult.isVerified());
    auto candidate = zc::mv(candidateResult).takeVerified();
    ZC_REQUIRE(candidate.functions.size() == 1);
    ZC_REQUIRE(candidate.functions[0].slots.size() > 1);
    candidate.functions[0].slots[1].key.location.point = zc::mv(point);

    auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
    ZC_EXPECT(!verifiedResult.isVerified());
  };

  expectRejected(zc::mv(entry));
  expectRejected(zc::mv(afterStatement));
  expectRejected(zc::mv(edge));
  expectRejected(zc::mv(exit));
}

ZC_TEST("Ownership points provide structural equality and order") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);
  const auto& function = fixture.builtMir().functions()[0];
  const auto block = function.blocks[0].id;

  const auto entry = MirPoint::entry();
  const auto beforeStatement = MirPoint::beforeStatement(block, 0);
  const auto afterStatement = MirPoint::afterStatement(block, 0);
  const auto beforeTerminator = MirPoint::beforeTerminator(block);
  const auto edge = MirPoint::edge(block, 0, block);
  const auto exit = MirPoint::exit(block, MirExitKind::Return);

  ZC_EXPECT(entry == MirPoint::entry());
  ZC_EXPECT(beforeStatement == MirPoint::beforeStatement(block, 0));
  ZC_EXPECT(afterStatement == MirPoint::afterStatement(block, 0));
  ZC_EXPECT(beforeTerminator == MirPoint::beforeTerminator(block));
  ZC_EXPECT(edge == MirPoint::edge(block, 0, block));
  ZC_EXPECT(exit == MirPoint::exit(block, MirExitKind::Return));
  ZC_EXPECT(entry != beforeStatement);
  ZC_EXPECT(beforeStatement != afterStatement);
  ZC_EXPECT(afterStatement != beforeTerminator);
  ZC_EXPECT(beforeTerminator != edge);
  ZC_EXPECT(edge != exit);
  ZC_EXPECT(entry < beforeStatement);
  ZC_EXPECT(beforeStatement < afterStatement);
  ZC_EXPECT(afterStatement < beforeTerminator);
  ZC_EXPECT(beforeTerminator < edge);
  ZC_EXPECT(edge < exit);
  ZC_EXPECT(!(exit < edge));
  ZC_EXPECT(!(beforeStatement < MirPoint::beforeStatement(block, 0)));

  const MirEventKey event{MirLocation{function.owner, beforeTerminator}, 1};
  const MirEventKey equivalentEvent{MirLocation{function.owner, MirPoint::beforeTerminator(block)},
                                    1};
  const MirEventKey differentEvent{MirLocation{function.owner, MirPoint::beforeTerminator(block)},
                                   2};
  const auto cfgPoint = facts::OwnershipPoint::cfg(beforeStatement);
  const auto equivalentCfgPoint = facts::OwnershipPoint::cfg(MirPoint::beforeStatement(block, 0));
  const auto beforeEventPoint = facts::OwnershipPoint::beforeEvent(event);
  const auto equivalentBeforeEventPoint = facts::OwnershipPoint::beforeEvent(equivalentEvent);
  const auto afterEventPoint = facts::OwnershipPoint::afterEvent(event);
  ZC_EXPECT(event == equivalentEvent);
  ZC_EXPECT(event != differentEvent);
  ZC_EXPECT(cfgPoint == equivalentCfgPoint);
  ZC_EXPECT(beforeEventPoint == equivalentBeforeEventPoint);
  ZC_EXPECT(afterEventPoint != beforeEventPoint);
}

ZC_TEST("Ownership event overlay verifier rejects a gapped causal ordinal") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);

  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].slots.size() != 0);
  candidate.functions[0].slots[0].key.operandOrdinal = 1;

  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_EXPECT(!verifiedResult.isVerified());
}

ZC_TEST("Move-path verifier rejects a tampered root path type chain") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);
  const auto& builtMir = fixture.builtMir();
  const auto checkedMir = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(checkedMir.size() == 1);

  auto candidateResult = facts::MovePathBuilder::build(builtMir, checkedMir[0].eventOverlay());
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].facts.size() == 1);
  const auto local = candidate.functions[0].facts[0].key.place.local();
  const auto rootType = candidate.functions[0].facts[0].key.place.rootType();
  zc::Vector<mir::MirProjection> projections;
  candidate.functions[0].facts[0].key.place =
      mir::MirPlace(local, rootType, zc::mv(projections), identity::SemanticTypeId());

  auto verifiedResult =
      facts::MovePathVerifier::verify(zc::mv(candidate), builtMir, checkedMir[0].eventOverlay());
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Move-path verifier rejects a root path with a parent") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);
  const auto& builtMir = fixture.builtMir();
  const auto checkedMir = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(checkedMir.size() == 1);

  auto candidateResult = facts::MovePathBuilder::build(builtMir, checkedMir[0].eventOverlay());
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].facts.size() == 1);
  const auto& root = candidate.functions[0].facts[0].key;
  candidate.functions[0].facts[0].parent = facts::MovePathKey{root.owner, root.place.clone()};

  auto verifiedResult =
      facts::MovePathVerifier::verify(zc::mv(candidate), builtMir, checkedMir[0].eventOverlay());
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Move-path verifier rejects a self conflict pair") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);
  const auto& builtMir = fixture.builtMir();
  const auto checkedMir = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(checkedMir.size() == 1);

  auto candidateResult = facts::MovePathBuilder::build(builtMir, checkedMir[0].eventOverlay());
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].facts.size() == 1);
  const auto& root = candidate.functions[0].facts[0].key;
  candidate.functions[0].conflicts.add(
      facts::MovePathPair{{root.owner, root.place.clone()}, {root.owner, root.place.clone()}});

  auto verifiedResult =
      facts::MovePathVerifier::verify(zc::mv(candidate), builtMir, checkedMir[0].eventOverlay());
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Move-path verifier rejects a reversed aggregate path order") {
  OwnershipPipelineFixture fixture(
      "struct Cell { value: i32, }\n"
      "fun entry() -> i32 { let cell = Cell { value: 0 }; return cell.value; }"_zc);
  const auto& builtMir = fixture.builtMir();
  const auto checkedMir = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(checkedMir.size() == 1);

  auto candidateResult = facts::MovePathBuilder::build(builtMir, checkedMir[0].eventOverlay());
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].facts.size() == 2);
  auto first = zc::mv(candidate.functions[0].facts[0]);
  candidate.functions[0].facts[0] = zc::mv(candidate.functions[0].facts[1]);
  candidate.functions[0].facts[1] = zc::mv(first);

  auto verifiedResult =
      facts::MovePathVerifier::verify(zc::mv(candidate), builtMir, checkedMir[0].eventOverlay());
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Move-path verifier rejects a reversed aggregate conflict pair") {
  OwnershipPipelineFixture fixture(
      "struct Cell { value: i32, }\n"
      "fun entry() -> i32 { let cell = Cell { value: 0 }; return cell.value; }"_zc);
  const auto& builtMir = fixture.builtMir();
  const auto checkedMir = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(checkedMir.size() == 1);

  auto candidateResult = facts::MovePathBuilder::build(builtMir, checkedMir[0].eventOverlay());
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].conflicts.size() == 1);
  auto first = zc::mv(candidate.functions[0].conflicts[0].first);
  candidate.functions[0].conflicts[0].first = zc::mv(candidate.functions[0].conflicts[0].second);
  candidate.functions[0].conflicts[0].second = zc::mv(first);

  auto verifiedResult =
      facts::MovePathVerifier::verify(zc::mv(candidate), builtMir, checkedMir[0].eventOverlay());
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Move-path verifier rejects a missing aggregate field conflict") {
  OwnershipPipelineFixture fixture(
      "struct Cell { value: i32, }\n"
      "fun entry() -> i32 { let cell = Cell { value: 0 }; return cell.value; }"_zc);
  const auto& builtMir = fixture.builtMir();
  const auto checkedMir = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(checkedMir.size() == 1);

  auto candidateResult = facts::MovePathBuilder::build(builtMir, checkedMir[0].eventOverlay());
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].facts.size() == 2);
  ZC_REQUIRE(candidate.functions[0].conflicts.size() == 1);
  candidate.functions[0].conflicts.clear();

  auto verifiedResult =
      facts::MovePathVerifier::verify(zc::mv(candidate), builtMir, checkedMir[0].eventOverlay());
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Move-path verifier rejects a missing multi-field aggregate conflict") {
  OwnershipPipelineFixture fixture(
      "struct Pair { mut left: i32, mut right: bool, }\n"
      "fun entry() -> bool { mut pair: Pair; pair.left = 0; pair.right = true; return pair.right; }"_zc);
  const auto& builtMir = fixture.builtMir();
  const auto checkedMir = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(checkedMir.size() == 1);

  auto candidateResult = facts::MovePathBuilder::build(builtMir, checkedMir[0].eventOverlay());
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].facts.size() == 3);
  ZC_REQUIRE(candidate.functions[0].conflicts.size() == 2);
  candidate.functions[0].conflicts.removeLast();

  auto verifiedResult =
      facts::MovePathVerifier::verify(zc::mv(candidate), builtMir, checkedMir[0].eventOverlay());
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Move-path verifier rejects a tampered semantic context fingerprint") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);

  auto candidateResult = facts::MovePathBuilder::build(
      builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay());
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  candidate.contextFingerprint =
      identity::ContextFingerprint::fromCanonicalDigest(repeatedDigest(0xFF));

  auto verifiedResult = facts::MovePathVerifier::verify(
      zc::mv(candidate), builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay());
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InputRevisionMismatch);
}

ZC_TEST("Move-path verifier rejects a tampered direct call result path") {
  OwnershipPipelineFixture fixture(
      "fun helper() -> i32 { return 0; }\n"
      "fun entry() -> i32 { return helper(); }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);

  auto candidateResult = facts::MovePathBuilder::build(
      builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay());
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  bool tampered = false;
  for (const auto& function : builtMir.functions()) {
    if (function.blocks.size() != 2) continue;
    for (auto& paths : candidate.functions) {
      if (paths.owner != function.owner) continue;
      ZC_REQUIRE(paths.facts.size() == function.locals.size());
      for (auto& fact : paths.facts) {
        auto& path = fact.key.place;
        if (path.local() != function.blocks[0].terminator.callValue().destination.local()) continue;
        const auto local = path.local();
        const auto rootType = path.rootType();
        zc::Vector<mir::MirProjection> projections;
        path = mir::MirPlace(local, rootType, zc::mv(projections), identity::SemanticTypeId());
        tampered = true;
      }
    }
  }
  ZC_REQUIRE(tampered);

  auto verifiedResult = facts::MovePathVerifier::verify(
      zc::mv(candidate), builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay());
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Initialization verifier rejects a tampered local state") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);
  OwnershipPipelineFixture foreignFixture("let value = 0;"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  const auto& inputs = ownershipInputs(session);

  auto candidateResult = facts::InitializationBuilder::build(
      builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay(), inputs.flow(),
      inputs.movePaths());
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].facts.size() == 7);
  candidate.functions[0].facts[4].state = facts::InitializationState::uninitialized();

  auto verifiedResult = facts::InitializationVerifier::verify(
      zc::mv(candidate), builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay(),
      inputs.flow(), inputs.movePaths());
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);

  auto lossCandidateResult = facts::InitializationBuilder::build(
      builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay(), inputs.flow(),
      inputs.movePaths());
  ZC_REQUIRE(lossCandidateResult.isVerified());
  auto lossCandidate = zc::mv(lossCandidateResult).takeVerified();
  ZC_REQUIRE(lossCandidate.functions[0].facts[0].lossCauses.size() == 1);
  lossCandidate.functions[0].facts[0].lossCauses[0].kind = facts::InitializationLossKind::Moved;

  auto lossResult = facts::InitializationVerifier::verify(
      zc::mv(lossCandidate), builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay(),
      inputs.flow(), inputs.movePaths());
  ZC_REQUIRE(lossResult.isIrInvariantRejected());
  ZC_EXPECT(lossResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);

  auto staleCandidateResult = facts::InitializationBuilder::build(
      builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay(), inputs.flow(),
      inputs.movePaths());
  ZC_REQUIRE(staleCandidateResult.isVerified());
  auto staleCandidate = zc::mv(staleCandidateResult).takeVerified();
  staleCandidate.overlayRevision = OwnershipEventOverlayRevision();

  auto staleResult = facts::InitializationVerifier::verify(
      zc::mv(staleCandidate), builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay(),
      inputs.flow(), inputs.movePaths());
  ZC_REQUIRE(staleResult.isIrInvariantRejected());
  ZC_EXPECT(staleResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(staleResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InputRevisionMismatch);

  auto foreignFlow = facts::InitializationBuilder::build(
      builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay(),
      ownershipInputs(foreignFixture.compilerSession()).flow(), inputs.movePaths());
  ZC_REQUIRE(foreignFlow.isIrInvariantRejected());
  ZC_EXPECT(foreignFlow.invariantFailures().facts().size() == 1);
  ZC_EXPECT(foreignFlow.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InputRevisionMismatch);
}

ZC_TEST("Initialization verifier rejects a tampered field path state") {
  OwnershipPipelineFixture fixture(
      "struct Cell { value: i32, }\n"
      "fun entry() -> i32 { let cell = Cell { value: 0 }; return cell.value; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  const auto& inputs = ownershipInputs(session);

  auto candidateResult = facts::InitializationBuilder::build(
      builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay(), inputs.flow(),
      inputs.movePaths());
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  bool tampered = false;
  for (auto& fact : candidate.functions[0].facts) {
    if (fact.key.place.projections().size() != 1 ||
        fact.point.kind() != MirPointKind::AfterStatement ||
        fact.point.afterStatementValue().ordinal != 1) {
      continue;
    }
    fact.state = facts::InitializationState::uninitialized();
    tampered = true;
  }
  ZC_REQUIRE(tampered);

  auto verifiedResult = facts::InitializationVerifier::verify(
      zc::mv(candidate), builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay(),
      inputs.flow(), inputs.movePaths());
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Initialization verifier rejects a tampered semantic context fingerprint") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  const auto& inputs = ownershipInputs(session);

  auto candidateResult = facts::InitializationBuilder::build(
      builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay(), inputs.flow(),
      inputs.movePaths());
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  candidate.contextFingerprint =
      identity::ContextFingerprint::fromCanonicalDigest(repeatedDigest(0xFE));

  auto verifiedResult = facts::InitializationVerifier::verify(
      zc::mv(candidate), builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay(),
      inputs.flow(), inputs.movePaths());
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InputRevisionMismatch);
}

ZC_TEST("Initialization lattice joins three-bit states at control-flow merges") {
  using facts::InitializationLattice;
  using facts::InitializationState;
  const auto dead = InitializationState::dead();
  const auto uninitialized = InitializationState::uninitialized();
  const auto initialized = InitializationState::initialized();
  const auto mayBeInitialized = InitializationState{true, true, false};
  const auto deadJoinedWithInitialized = InitializationState{false, true, false};

  ZC_EXPECT(InitializationLattice::joinState(dead, dead) == dead);
  ZC_EXPECT(InitializationLattice::joinState(uninitialized, uninitialized) == uninitialized);
  ZC_EXPECT(InitializationLattice::joinState(initialized, initialized) == initialized);
  ZC_EXPECT(InitializationLattice::joinState(initialized, uninitialized) == mayBeInitialized);
  ZC_EXPECT(InitializationLattice::joinState(uninitialized, initialized) == mayBeInitialized);
  ZC_EXPECT(InitializationLattice::joinState(dead, initialized) == deadJoinedWithInitialized);
  ZC_EXPECT(InitializationLattice::joinState(initialized, dead) == deadJoinedWithInitialized);
  ZC_EXPECT(InitializationLattice::joinState(mayBeInitialized, uninitialized) == mayBeInitialized);
}

ZC_TEST("Initialization lattice merges distinct loss causes at control-flow merges") {
  using facts::InitializationLattice;
  using facts::InitializationLossCause;
  using facts::InitializationLossKind;
  using facts::MovePathKey;
  const auto firstEvent = MirEventKey{MirLocation{identity::DefId(), MirPoint::entry()}, 0};
  const auto secondEvent = MirEventKey{MirLocation{identity::DefId(), MirPoint::entry()}, 1};
  const auto local = ZC_REQUIRE_NONNULL(mir::MirLocalId::fromOrdinal(1));
  auto causeWith = [&](InitializationLossKind kind, const MirEventKey& event) {
    zc::Vector<InitializationLossCause> causes;
    zc::Vector<mir::MirProjection> projections;
    causes.add(InitializationLossCause{
        kind, event,
        MovePathKey{identity::DefId(),
                    mir::MirPlace(local, identity::SemanticTypeId(), zc::mv(projections),
                                  identity::SemanticTypeId())}});
    return causes;
  };
  zc::Vector<InitializationLossCause> noCauses;
  auto neverCauses = causeWith(InitializationLossKind::NeverInitialized, firstEvent);
  auto movedCauses = causeWith(InitializationLossKind::Moved, secondEvent);

  ZC_EXPECT(InitializationLattice::mergeLossCauses(noCauses.asPtr(), noCauses.asPtr()).size() == 0);

  auto oneSide = InitializationLattice::mergeLossCauses(neverCauses.asPtr(), noCauses.asPtr());
  ZC_REQUIRE(oneSide.size() == 1);
  ZC_EXPECT(oneSide[0].kind == InitializationLossKind::NeverInitialized);
  ZC_EXPECT(oneSide[0].event == firstEvent);

  auto distinct = InitializationLattice::mergeLossCauses(neverCauses.asPtr(), movedCauses.asPtr());
  ZC_REQUIRE(distinct.size() == 2);
  ZC_EXPECT(distinct[0].kind == InitializationLossKind::NeverInitialized);
  ZC_EXPECT(distinct[1].kind == InitializationLossKind::Moved);

  auto duplicated =
      InitializationLattice::mergeLossCauses(neverCauses.asPtr(), neverCauses.asPtr());
  ZC_REQUIRE(duplicated.size() == 1);
  ZC_EXPECT(duplicated[0].kind == InitializationLossKind::NeverInitialized);
}

namespace {

void addLossCause(zc::Vector<facts::InitializationLossCause>& causes,
                  facts::InitializationLossKind kind, uint32_t eventOrdinal,
                  mir::MirLocalId local) {
  zc::Vector<mir::MirProjection> projections;
  causes.add(facts::InitializationLossCause{
      kind, MirEventKey{MirLocation{identity::DefId(), MirPoint::entry()}, eventOrdinal},
      facts::MovePathKey{identity::DefId(),
                         mir::MirPlace(local, identity::SemanticTypeId(), zc::mv(projections),
                                       identity::SemanticTypeId())}});
}

bool sameTestLossCause(const facts::InitializationLossCause& left,
                       const facts::InitializationLossCause& right) {
  return left.kind == right.kind && left.event == right.event &&
         left.path.owner == right.path.owner && left.path.place.local() == right.path.place.local();
}

}  // namespace

ZC_TEST("Initialization lattice mergeLossCauses is order-independent") {
  using facts::InitializationLattice;
  using facts::InitializationLossKind;
  const auto local = ZC_REQUIRE_NONNULL(mir::MirLocalId::fromOrdinal(1));
  zc::Vector<facts::InitializationLossCause> left;
  addLossCause(left, InitializationLossKind::NeverInitialized, 0, local);
  addLossCause(left, InitializationLossKind::Moved, 1, local);
  zc::Vector<facts::InitializationLossCause> right;
  addLossCause(right, InitializationLossKind::Deinitialized, 2, local);
  addLossCause(right, InitializationLossKind::StorageEnded, 3, local);

  auto forward = InitializationLattice::mergeLossCauses(left.asPtr(), right.asPtr());
  auto reverse = InitializationLattice::mergeLossCauses(right.asPtr(), left.asPtr());
  ZC_REQUIRE(forward.size() == reverse.size());
  for (size_t index = 0; index < forward.size(); ++index) {
    ZC_EXPECT(sameTestLossCause(forward[index], reverse[index]));
  }
}

ZC_TEST("Initialization lattice mergeLossCauses deduplicates overlapping inputs") {
  using facts::InitializationLattice;
  using facts::InitializationLossKind;
  const auto local = ZC_REQUIRE_NONNULL(mir::MirLocalId::fromOrdinal(1));
  zc::Vector<facts::InitializationLossCause> moved;
  addLossCause(moved, InitializationLossKind::Moved, 0, local);
  zc::Vector<facts::InitializationLossCause> both;
  addLossCause(both, InitializationLossKind::Moved, 0, local);
  addLossCause(both, InitializationLossKind::Deinitialized, 1, local);

  auto self = InitializationLattice::mergeLossCauses(moved.asPtr(), moved.asPtr());
  ZC_REQUIRE(self.size() == 1);
  ZC_EXPECT(self[0].kind == InitializationLossKind::Moved);

  auto overlapping = InitializationLattice::mergeLossCauses(both.asPtr(), moved.asPtr());
  ZC_REQUIRE(overlapping.size() == 2);
  ZC_EXPECT(overlapping[0].kind == InitializationLossKind::Moved);
  ZC_EXPECT(overlapping[1].kind == InitializationLossKind::Deinitialized);
}

ZC_TEST("Initialization lattice mergeLossCauses publishes canonical kind-event-path order") {
  using facts::InitializationLattice;
  using facts::InitializationLossKind;
  const auto firstLocal = ZC_REQUIRE_NONNULL(mir::MirLocalId::fromOrdinal(1));
  const auto secondLocal = ZC_REQUIRE_NONNULL(mir::MirLocalId::fromOrdinal(2));
  // Scrambled inputs whose canonical order is B, C, D, A:
  //  B = NeverInitialized (smallest kind), event 0, local 1
  //  C = Moved, event 0, local 1
  //  D = Moved, event 0, local 2 (same kind+event as C, larger path)
  //  A = Moved, event 1, local 1 (same kind+path as C, larger event)
  zc::Vector<facts::InitializationLossCause> left;
  addLossCause(left, InitializationLossKind::Moved, 1, firstLocal);
  addLossCause(left, InitializationLossKind::Moved, 0, secondLocal);
  zc::Vector<facts::InitializationLossCause> right;
  addLossCause(right, InitializationLossKind::Moved, 0, firstLocal);
  addLossCause(right, InitializationLossKind::NeverInitialized, 0, firstLocal);

  auto merged = InitializationLattice::mergeLossCauses(left.asPtr(), right.asPtr());
  ZC_REQUIRE(merged.size() == 4);
  ZC_EXPECT(merged[0].kind == InitializationLossKind::NeverInitialized);
  ZC_EXPECT(merged[0].event.operandOrdinal == 0);
  ZC_EXPECT(merged[1].kind == InitializationLossKind::Moved);
  ZC_EXPECT(merged[1].event.operandOrdinal == 0);
  ZC_EXPECT(merged[1].path.place.local() == firstLocal);
  ZC_EXPECT(merged[2].kind == InitializationLossKind::Moved);
  ZC_EXPECT(merged[2].event.operandOrdinal == 0);
  ZC_EXPECT(merged[2].path.place.local() == secondLocal);
  ZC_EXPECT(merged[3].kind == InitializationLossKind::Moved);
  ZC_EXPECT(merged[3].event.operandOrdinal == 1);
  ZC_EXPECT(merged[3].path.place.local() == firstLocal);
}

ZC_TEST("Loan verifier rejects a tampered active point, issue, commit, and foreign lineage") {
  OwnershipPipelineFixture fixture(
      "fun reborrow(value: &mut i32) -> &mut i32 { return &mut *value; }"_zc);
  OwnershipPipelineFixture foreignFixture(
      "fun reborrow(value: &mut i32) -> &mut i32 { return &mut *value; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& movePaths = ownershipInputs(session).movePaths();
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);

  auto candidateResult = facts::LoanBuilder::build(
      movePaths, builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay());
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.loans.size() == 1);
  candidate.loans[0].activeFrom = facts::OwnershipPoint::beforeEvent(candidate.loans[0].issue);
  auto activePointResult =
      facts::LoanVerifier::verify(zc::mv(candidate), movePaths, builtMir,
                                  session.getOwnershipCheckedMirModules()[0].eventOverlay());
  ZC_REQUIRE(activePointResult.isIrInvariantRejected());
  ZC_EXPECT(activePointResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(activePointResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);

  auto sourceCandidateResult = facts::LoanBuilder::build(
      movePaths, builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay());
  ZC_REQUIRE(sourceCandidateResult.isVerified());
  auto sourceCandidate = zc::mv(sourceCandidateResult).takeVerified();
  sourceCandidate.loans[0].source.owner = identity::DefId();
  auto sourceResult =
      facts::LoanVerifier::verify(zc::mv(sourceCandidate), movePaths, builtMir,
                                  session.getOwnershipCheckedMirModules()[0].eventOverlay());
  ZC_REQUIRE(sourceResult.isIrInvariantRejected());
  ZC_EXPECT(sourceResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(sourceResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);

  auto issueCandidateResult = facts::LoanBuilder::build(
      movePaths, builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay());
  ZC_REQUIRE(issueCandidateResult.isVerified());
  auto issueCandidate = zc::mv(issueCandidateResult).takeVerified();
  issueCandidate.loans[0].issue.operandOrdinal = 0;
  auto issueResult =
      facts::LoanVerifier::verify(zc::mv(issueCandidate), movePaths, builtMir,
                                  session.getOwnershipCheckedMirModules()[0].eventOverlay());
  ZC_REQUIRE(issueResult.isIrInvariantRejected());
  ZC_EXPECT(issueResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(issueResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);

  auto commitCandidateResult = facts::LoanBuilder::build(
      movePaths, builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay());
  ZC_REQUIRE(commitCandidateResult.isVerified());
  auto commitCandidate = zc::mv(commitCandidateResult).takeVerified();
  commitCandidate.loans[0].commit.operandOrdinal = 1;
  auto commitResult =
      facts::LoanVerifier::verify(zc::mv(commitCandidate), movePaths, builtMir,
                                  session.getOwnershipCheckedMirModules()[0].eventOverlay());
  ZC_REQUIRE(commitResult.isIrInvariantRejected());
  ZC_EXPECT(commitResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(commitResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);

  auto foreignCandidateResult = facts::LoanBuilder::build(
      movePaths, builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay());
  ZC_REQUIRE(foreignCandidateResult.isVerified());
  auto foreignCandidate = zc::mv(foreignCandidateResult).takeVerified();

  auto foreignResult = facts::LoanVerifier::verify(
      zc::mv(foreignCandidate), ownershipInputs(foreignFixture.compilerSession()).movePaths(),
      foreignFixture.builtMir(),
      foreignFixture.compilerSession().getOwnershipCheckedMirModules()[0].eventOverlay());
  ZC_REQUIRE(foreignResult.isIrInvariantRejected());
  ZC_EXPECT(foreignResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(foreignResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InputRevisionMismatch);
}

ZC_TEST("Ownership input verifier rejects facts from a foreign analysis snapshot") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);
  OwnershipPipelineFixture foreignFixture("let value = 0;"_zc);
  const auto& session = fixture.compilerSession();
  auto result =
      verifyOwnershipInputs(fixture, foreignFixture.builtMir().borrowEvidenceLease(),
                            ZC_REQUIRE_NONNULL(session.getBorrowEvidenceRepository()).capability());
  ZC_REQUIRE(result.isIrInvariantRejected());
  ZC_EXPECT(result.invariantFailures().facts().size() == 1);
  ZC_EXPECT(result.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InputRevisionMismatch);
}

ZC_TEST("Ownership input verifier rejects a foreign borrow evidence capability") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);
  OwnershipPipelineFixture foreignFixture("let value = 0;"_zc);

  auto result = verifyOwnershipInputs(
      fixture, fixture.builtMir().borrowEvidenceLease(),
      ZC_REQUIRE_NONNULL(foreignFixture.compilerSession().getBorrowEvidenceRepository())
          .capability());
  ZC_REQUIRE(result.isIrInvariantRejected());
  ZC_EXPECT(result.invariantFailures().facts().size() == 1);
  ZC_EXPECT(result.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InputRevisionMismatch);
}

ZC_TEST("Ownership input verifier rejects an equivalent evidence pair from another repository") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);
  auto repository = driver::borrow_evidence::BorrowEvidenceRepository::create(
      fixture.compilerSession().getSemanticContextBrand(), fixture.substitutionBrand(), 1);
  ZC_REQUIRE(repository != zc::none);
  ZC_IF_SOME(value, repository) {
    auto adoption = value.adopt(fixture.cloneBorrowEvidence(), fixture.identities());
    ZC_REQUIRE(adoption.is<driver::borrow_evidence::VerifiedBorrowEvidenceLease>());
    const auto capability = value.capability();
    const auto& lease = adoption.get<driver::borrow_evidence::VerifiedBorrowEvidenceLease>();
    ZC_REQUIRE(capability.lookup(lease).isResolved());

    auto result = verifyOwnershipInputs(fixture, lease, capability);
    ZC_REQUIRE(result.isIrInvariantRejected());
    ZC_EXPECT(result.invariantFailures().facts().size() == 1);
    ZC_EXPECT(result.invariantFailures().facts()[0].kind() ==
              ir::IrFailureKind::InputRevisionMismatch);
  }
}

ZC_TEST("Ownership input snapshots retain live borrow evidence") {
  OwnershipPipelineFixture fixture("fun entry(value: i32) -> i32 { return value; }"_zc);

  const auto& inputs = ownershipInputs(fixture.compilerSession());
  ZC_EXPECT(inputs.hasLiveBorrowEvidence());
}

ZC_TEST("Resource verifier rejects a missing logical resource function") {
  OwnershipPipelineFixture fixture(
      "struct Cell { value: i32, }\n"
      "fun entry() -> i32 { let cell = Cell { value: 0 }; return cell.value; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();
  const auto& inputs = ownershipInputs(session);

  auto candidateResult =
      facts::OwnershipResourceBuilder::build(inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].facts.size() == 0);
  candidate.functions.removeLast();

  auto verifiedResult = facts::OwnershipResourceVerifier::verify(
      zc::mv(candidate), inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Resource verifier rejects a spurious logical resource") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();
  const auto& inputs = ownershipInputs(session);

  auto candidateResult =
      facts::OwnershipResourceBuilder::build(inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].facts.size() == 0);
  ZC_REQUIRE(overlay.functions().size() == 1);
  ZC_REQUIRE(overlay.functions()[0].logicalDropPlans.size() == 1);
  const auto& plan = overlay.functions()[0].logicalDropPlans[0];
  candidate.functions[0].facts.add(facts::OwnershipResourceFact{
      facts::DropResourceSubject{
          plan.initialization, facts::MovePathKey{candidate.functions[0].owner, plan.root.clone()},
          plan.root.resultType()},
      facts::DropRequirement::Logical,
      zc::none,
      0,
  });

  auto verifiedResult = facts::OwnershipResourceVerifier::verify(
      zc::mv(candidate), inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Ownership resources retain a linear nominal aggregate") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Linear};\n"
      "struct Cell { value: i32, }\n"
      "unsafe impl Linear for Cell;\n"
      "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& resources = ownershipInputs(session).resources();

  ZC_REQUIRE(resources.functions().size() == 1);
  const auto& function = resources.functions()[0];
  ZC_REQUIRE(function.facts.size() == 1);
  const auto& fact = function.facts[0];
  ZC_EXPECT(fact.subject.introduction.location.owner == function.owner);
  ZC_EXPECT(fact.subject.origin.owner == function.owner);
  ZC_EXPECT(fact.subject.origin.place.projections().size() == 0);
  ZC_EXPECT(fact.requirement == facts::DropRequirement::Linear);
  ZC_EXPECT(fact.dropAction == zc::none);
  ZC_EXPECT(fact.declarationOrdinal == 0);
}

ZC_TEST("Ownership resources retain a logical nominal aggregate") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Copy};\n"
      "struct Cell { value: i32, }\n"
      "impl !Copy for Cell;\n"
      "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc);
  const auto& resources = ownershipInputs(fixture.compilerSession()).resources();

  ZC_REQUIRE(resources.functions().size() == 1);
  const auto& function = resources.functions()[0];
  ZC_REQUIRE(function.facts.size() == 1);
  const auto& fact = function.facts[0];
  ZC_EXPECT(fact.subject.introduction.location.owner == function.owner);
  ZC_EXPECT(fact.subject.origin.owner == function.owner);
  ZC_EXPECT(fact.subject.origin.place.projections().size() == 0);
  ZC_EXPECT(fact.requirement == facts::DropRequirement::Logical);
  ZC_EXPECT(fact.dropAction == zc::none);
  ZC_EXPECT(fact.declarationOrdinal == 0);
}

ZC_TEST("Ownership resources retain a noncopy linear nominal aggregate") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Copy, Linear};\n"
      "struct Cell { value: i32, }\n"
      "impl !Copy for Cell;\n"
      "unsafe impl Linear for Cell;\n"
      "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc);
  const auto& resources = ownershipInputs(fixture.compilerSession()).resources();

  ZC_REQUIRE(resources.functions().size() == 1);
  const auto& function = resources.functions()[0];
  ZC_REQUIRE(function.facts.size() == 1);
  const auto& fact = function.facts[0];
  ZC_EXPECT(fact.requirement == facts::DropRequirement::LinearLogical);
  ZC_EXPECT(fact.dropAction == zc::none);
}

ZC_TEST("Resource verifier rejects a tampered linear resource") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Linear};\n"
      "struct Cell { value: i32, }\n"
      "unsafe impl Linear for Cell;\n"
      "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();
  const auto& inputs = ownershipInputs(session);

  auto candidateResult =
      facts::OwnershipResourceBuilder::build(inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].facts.size() == 1);
  candidate.functions[0].facts[0].requirement = facts::DropRequirement::Logical;

  auto verifiedResult = facts::OwnershipResourceVerifier::verify(
      zc::mv(candidate), inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Resource verifier rejects a tampered resource action") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Linear};\n"
      "struct Cell { value: i32, }\n"
      "unsafe impl Linear for Cell;\n"
      "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();
  const auto& inputs = ownershipInputs(session);

  auto candidateResult =
      facts::OwnershipResourceBuilder::build(inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].facts.size() == 1);
  candidate.functions[0].facts[0].dropAction = LogicalDropAction{
      LogicalDropBuiltinAction{candidate.functions[0].facts[0].subject.originType}};

  auto verifiedResult = facts::OwnershipResourceVerifier::verify(
      zc::mv(candidate), inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Resource verifier rejects a tampered resource subject") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Linear};\n"
      "struct Cell { value: i32, }\n"
      "unsafe impl Linear for Cell;\n"
      "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();
  const auto& inputs = ownershipInputs(session);

  auto candidateResult =
      facts::OwnershipResourceBuilder::build(inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].facts.size() == 1);
  ++candidate.functions[0].facts[0].subject.introduction.operandOrdinal;

  auto verifiedResult = facts::OwnershipResourceVerifier::verify(
      zc::mv(candidate), inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Ownership resources retain a linear direct-call result") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Linear};\n"
      "struct Cell { value: i32, }\n"
      "unsafe impl Linear for Cell;\n"
      "fun helper() -> Cell { let cell = Cell { value: 0 }; return cell; }\n"
      "fun entry() -> Cell { return helper(); }"_zc);
  const auto& resources = ownershipInputs(fixture.compilerSession()).resources();

  bool foundNormalEdgeResource = false;
  for (const auto& function : resources.functions()) {
    for (const auto& fact : function.facts) {
      if (fact.subject.introduction.location.point.kind() != MirPointKind::Edge) continue;
      ZC_EXPECT(fact.subject.introduction.location.owner == function.owner);
      ZC_EXPECT(fact.subject.origin.owner == function.owner);
      ZC_EXPECT(fact.subject.origin.place.projections().size() == 0);
      ZC_EXPECT(fact.requirement == facts::DropRequirement::Linear);
      ZC_EXPECT(fact.dropAction == zc::none);
      foundNormalEdgeResource = true;
    }
  }
  ZC_EXPECT(foundNormalEdgeResource);
}

ZC_TEST("Ownership drop plans retain a closed linear component") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Linear};\n"
      "struct Cell { value: i32, }\n"
      "unsafe impl Linear for Cell;\n"
      "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc);
  const auto& resources = ownershipInputs(fixture.compilerSession()).resources();

  ZC_REQUIRE(resources.functions().size() == 1);
  const auto& function = resources.functions()[0];
  ZC_REQUIRE(function.facts.size() == 1);
  ZC_REQUIRE(function.dropPlans.size() == 1);
  const auto& plan = function.dropPlans[0];
  ZC_EXPECT(plan.mode == facts::DropPlanMode::Closed);
  ZC_EXPECT(plan.subject.introduction == function.facts[0].subject.introduction);
  ZC_EXPECT(plan.subject.origin.owner == function.facts[0].subject.origin.owner);
  ZC_EXPECT(plan.subject.originType == function.facts[0].subject.originType);
  ZC_REQUIRE(plan.components.size() == 1);
  ZC_EXPECT(plan.components[0].factOrdinal == 0);
  ZC_EXPECT(plan.components[0].action == zc::none);
}

ZC_TEST("Ownership drop plans retain a closed logical component") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Copy};\n"
      "struct Cell { value: i32, }\n"
      "impl !Copy for Cell;\n"
      "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc);
  const auto& resources = ownershipInputs(fixture.compilerSession()).resources();

  ZC_REQUIRE(resources.functions().size() == 1);
  const auto& function = resources.functions()[0];
  ZC_REQUIRE(function.facts.size() == 1);
  ZC_REQUIRE(function.dropPlans.size() == 1);
  const auto& plan = function.dropPlans[0];
  ZC_EXPECT(plan.mode == facts::DropPlanMode::Closed);
  ZC_REQUIRE(plan.components.size() == 1);
  ZC_EXPECT(plan.components[0].factOrdinal == 0);
  ZC_EXPECT(plan.components[0].action == zc::none);
  ZC_EXPECT(function.facts[0].requirement == facts::DropRequirement::Logical);
}

ZC_TEST("Ownership drop plans retain a moved resource component") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Copy};\n"
      "struct Cell { value: i32, }\n"
      "impl !Copy for Cell;\n"
      "fun entry() -> Cell { let first = Cell { value: 0 }; let second = first; return second; }"_zc);
  const auto& resources = ownershipInputs(fixture.compilerSession()).resources();

  ZC_REQUIRE(resources.functions().size() == 1);
  const auto& function = resources.functions()[0];
  ZC_REQUIRE(function.facts.size() == 1);
  ZC_REQUIRE(function.transfers.size() == 1);
  ZC_REQUIRE(function.dropPlans.size() == 2);
  for (const auto& plan : function.dropPlans) {
    ZC_EXPECT(plan.mode == facts::DropPlanMode::Closed);
    ZC_REQUIRE(plan.components.size() == 1);
    ZC_EXPECT(plan.components[0].factOrdinal == 0);
    ZC_EXPECT(plan.components[0].action == zc::none);
    ZC_EXPECT(plan.subject.origin.place.local() == function.facts[0].subject.origin.place.local());
  }
}

ZC_TEST("Ownership resources retain no cast route for a same-type move") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Copy};\n"
      "struct Cell { value: i32, }\n"
      "impl !Copy for Cell;\n"
      "fun entry() -> Cell { let first = Cell { value: 0 }; let second = first; return second; }"_zc);
  const auto& resources = ownershipInputs(fixture.compilerSession()).resources();

  ZC_REQUIRE(resources.functions().size() == 1);
  const auto& function = resources.functions()[0];
  ZC_REQUIRE(function.facts.size() == 1);
  ZC_REQUIRE(function.transfers.size() == 1);
  ZC_EXPECT(function.castRoutes.size() == 0);
}

ZC_TEST("Resource verifier rejects a spurious cast route") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Copy};\n"
      "struct Cell { value: i32, }\n"
      "impl !Copy for Cell;\n"
      "fun entry() -> Cell { let first = Cell { value: 0 }; let second = first; return second; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();
  const auto& inputs = ownershipInputs(session);

  auto candidateResult =
      facts::OwnershipResourceBuilder::build(inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].castRoutes.size() == 0);
  ZC_REQUIRE(candidate.functions[0].transfers.size() == 1);
  ZC_REQUIRE(candidate.functions[0].facts.size() == 1);
  const auto& transfer = candidate.functions[0].transfers[0];
  const auto& fact = candidate.functions[0].facts[0];
  candidate.functions[0].castRoutes.add(facts::CastResourceRoute{
      fact.subject.clone(), facts::MovePathKey{transfer.from.owner, transfer.from.place.clone()},
      facts::MovePathKey{transfer.to.owner, transfer.to.place.clone()}, transfer.event});

  auto verifiedResult = facts::OwnershipResourceVerifier::verify(
      zc::mv(candidate), inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Resource verifier rejects a tampered cast route subject") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Copy};\n"
      "struct Cell { value: i32, }\n"
      "impl !Copy for Cell;\n"
      "fun entry() -> Cell { let first = Cell { value: 0 }; let second = first; return second; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();
  const auto& inputs = ownershipInputs(session);

  auto candidateResult =
      facts::OwnershipResourceBuilder::build(inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].castRoutes.size() == 0);
  ZC_REQUIRE(candidate.functions[0].transfers.size() == 1);
  ZC_REQUIRE(candidate.functions[0].facts.size() == 1);
  const auto& transfer = candidate.functions[0].transfers[0];
  const auto& fact = candidate.functions[0].facts[0];
  auto route = facts::CastResourceRoute{
      fact.subject.clone(), facts::MovePathKey{transfer.from.owner, transfer.from.place.clone()},
      facts::MovePathKey{transfer.to.owner, transfer.to.place.clone()}, transfer.event};
  route.subject.introduction.operandOrdinal = 999;
  candidate.functions[0].castRoutes.add(zc::mv(route));

  auto verifiedResult = facts::OwnershipResourceVerifier::verify(
      zc::mv(candidate), inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Linear resource produces one obligation with return consumption") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Copy, Linear};\n"
      "struct Cell { value: i32, }\n"
      "impl !Copy for Cell;\n"
      "unsafe impl Linear for Cell;\n"
      "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc);
  const auto& resources = ownershipInputs(fixture.compilerSession()).resources();

  ZC_REQUIRE(resources.functions().size() == 1);
  const auto& function = resources.functions()[0];
  ZC_REQUIRE(function.linearObligations.size() == 1);
  const auto& obligation = function.linearObligations[0];
  ZC_EXPECT(obligation.transfers.size() == 0);
  ZC_REQUIRE(obligation.consumptions.size() == 1);
  ZC_EXPECT(obligation.consumptions[0].kind == facts::LinearConsumptionKind::Return);
  ZC_REQUIRE(function.linearCarriers.size() == 1);
  ZC_EXPECT(function.linearCarriers[0].incoming.size() == 0);
  ZC_REQUIRE(function.linearSccs.size() == 1);
  ZC_EXPECT(function.linearSccs[0].carriers.size() == 1);
}

ZC_TEST("Linear resource tracks transfer and return consumption across carriers") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Copy, Linear};\n"
      "struct Cell { value: i32, }\n"
      "impl !Copy for Cell;\n"
      "unsafe impl Linear for Cell;\n"
      "fun entry() -> Cell { let first = Cell { value: 0 }; let second = first; return second; }"_zc);
  const auto& resources = ownershipInputs(fixture.compilerSession()).resources();

  ZC_REQUIRE(resources.functions().size() == 1);
  const auto& function = resources.functions()[0];
  ZC_REQUIRE(function.linearObligations.size() == 1);
  const auto& obligation = function.linearObligations[0];
  ZC_REQUIRE(obligation.transfers.size() == 1);
  ZC_REQUIRE(obligation.consumptions.size() == 1);
  ZC_EXPECT(obligation.consumptions[0].kind == facts::LinearConsumptionKind::Return);
  ZC_REQUIRE(function.linearCarriers.size() == 2);
  ZC_EXPECT(function.linearCarriers[0].incoming.size() == 0);
  ZC_REQUIRE(function.linearCarriers[1].incoming.size() == 1);
  ZC_EXPECT(function.linearCarriers[1].incoming[0].transfer.event == obligation.transfers[0].event);
  ZC_REQUIRE(function.linearSccs.size() == 2);
  ZC_EXPECT(function.linearSccs[0].carriers.size() == 1);
  ZC_EXPECT(function.linearSccs[1].carriers.size() == 1);
}

ZC_TEST("Resource verifier rejects a tampered linear carrier") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Copy, Linear};\n"
      "struct Cell { value: i32, }\n"
      "impl !Copy for Cell;\n"
      "unsafe impl Linear for Cell;\n"
      "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();
  const auto& inputs = ownershipInputs(session);

  auto candidateResult =
      facts::OwnershipResourceBuilder::build(inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].linearCarriers.size() == 1);
  candidate.functions[0].linearCarriers[0].key.creation.operandOrdinal = 999;

  auto verifiedResult = facts::OwnershipResourceVerifier::verify(
      zc::mv(candidate), inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Same-type move emits no cast-carrier roles") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Copy};\n"
      "struct Cell { value: i32, }\n"
      "impl !Copy for Cell;\n"
      "fun entry() -> Cell { let first = Cell { value: 0 }; let second = first; return second; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();

  ZC_REQUIRE(overlay.functions().size() == 1);
  for (const auto& slot : overlay.functions()[0].slots) {
    for (const auto role : slot.roles) {
      ZC_EXPECT(role != OwnershipEventRole::CastCarrierInitialize);
      ZC_EXPECT(role != OwnershipEventRole::CastCarrierTransfer);
      ZC_EXPECT(role != OwnershipEventRole::CastCarrierDrop);
    }
  }
  ZC_EXPECT(overlay.functions()[0].castResourcePlans.size() == 0);
}

ZC_TEST("Resource verifier rejects a tampered drop plan mode") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Linear};\n"
      "struct Cell { value: i32, }\n"
      "unsafe impl Linear for Cell;\n"
      "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();
  const auto& inputs = ownershipInputs(session);

  auto candidateResult =
      facts::OwnershipResourceBuilder::build(inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].dropPlans.size() == 1);
  candidate.functions[0].dropPlans[0].mode = facts::DropPlanMode::Open;

  auto verifiedResult = facts::OwnershipResourceVerifier::verify(
      zc::mv(candidate), inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Resource verifier rejects a spurious drop plan") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();
  const auto& inputs = ownershipInputs(session);

  auto candidateResult =
      facts::OwnershipResourceBuilder::build(inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].facts.size() == 0);
  ZC_REQUIRE(candidate.functions[0].dropPlans.size() == 0);
  ZC_REQUIRE(overlay.functions().size() == 1);
  ZC_REQUIRE(overlay.functions()[0].logicalDropPlans.size() == 1);
  const auto& overlayPlan = overlay.functions()[0].logicalDropPlans[0];
  zc::Vector<facts::DropPlanComponent> components;
  components.add(facts::DropPlanComponent{0, zc::none});
  candidate.functions[0].dropPlans.add(facts::DropPlan{
      facts::DropResourceSubject{
          overlayPlan.initialization,
          facts::MovePathKey{candidate.functions[0].owner, overlayPlan.root.clone()},
          overlayPlan.root.resultType()},
      facts::DropPlanMode::Closed, zc::mv(components)});

  auto verifiedResult = facts::OwnershipResourceVerifier::verify(
      zc::mv(candidate), inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Resource verifier rejects a tampered drop plan component ordinal") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Linear};\n"
      "struct Cell { value: i32, }\n"
      "unsafe impl Linear for Cell;\n"
      "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();
  const auto& inputs = ownershipInputs(session);

  auto candidateResult =
      facts::OwnershipResourceBuilder::build(inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].dropPlans.size() == 1);
  ZC_REQUIRE(candidate.functions[0].dropPlans[0].components.size() == 1);
  candidate.functions[0].dropPlans[0].components[0].factOrdinal = 1;

  auto verifiedResult = facts::OwnershipResourceVerifier::verify(
      zc::mv(candidate), inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Flow verifier rejects a tampered direct-call continuation") {
  OwnershipPipelineFixture fixture(
      "fun helper() -> i32 { return 0; }\n"
      "fun entry() -> i32 { return helper(); }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();

  auto candidateResult = facts::FlowBuilder::build(builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  bool tampered = false;
  for (auto& function : candidate.functions) {
    for (auto& edge : function.edges) {
      if (edge.from.kind() != facts::OwnershipPointKind::Cfg ||
          edge.from.cfgValue().point.kind() != MirPointKind::Edge) {
        continue;
      }
      edge.to = facts::OwnershipPoint::cfg(MirPoint::entry());
      tampered = true;
      break;
    }
    if (tampered) break;
  }
  ZC_REQUIRE(tampered);

  auto result = facts::FlowVerifier::verify(zc::mv(candidate), builtMir, overlay);
  ZC_REQUIRE(result.isIrInvariantRejected());
  ZC_EXPECT(result.invariantFailures().facts().size() == 1);
  ZC_EXPECT(result.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Flow inventory connects direct-call continuation cutpoints") {
  OwnershipPipelineFixture fixture(
      "fun helper() -> i32 { return 0; }\n"
      "fun entry() -> i32 { return helper(); }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& flow = ownershipInputs(session).flow();
  bool foundContinuation = false;
  bool foundCommitEntry = false;
  bool foundCommitExit = false;
  bool foundTargetTransition = false;
  for (const auto& function : builtMir.functions()) {
    if (function.blocks.size() != 2) continue;
    const auto& call = function.blocks[0].terminator.callValue();
    for (const auto& flowFunction : flow.functions()) {
      if (flowFunction.owner != function.owner) continue;
      for (const auto& point : flowFunction.points) {
        if (point.kind() != facts::OwnershipPointKind::Cfg ||
            point.cfgValue().point.kind() != MirPointKind::Edge) {
          continue;
        }
        const auto& edge = point.cfgValue().point.edgeValue();
        if (edge.from == function.blocks[0].id && edge.edgeOrdinal == 0 &&
            edge.to == call.normalTarget) {
          foundContinuation = true;
        }
      }
      for (const auto& edge : flowFunction.edges) {
        if (edge.from.kind() == facts::OwnershipPointKind::Cfg &&
            edge.from.cfgValue().point.kind() == MirPointKind::Edge &&
            edge.to.kind() == facts::OwnershipPointKind::BeforeEvent) {
          const auto& continuation = edge.from.cfgValue().point.edgeValue();
          const auto& event = edge.to.beforeEventValue().event;
          if (continuation.from == function.blocks[0].id && continuation.edgeOrdinal == 0 &&
              continuation.to == call.normalTarget && event.location.owner == function.owner &&
              event.location.point.kind() == MirPointKind::Edge && event.operandOrdinal == 0) {
            foundCommitEntry = true;
          }
        }
        if (edge.from.kind() == facts::OwnershipPointKind::BeforeEvent &&
            edge.to.kind() == facts::OwnershipPointKind::AfterEvent) {
          const auto& before = edge.from.beforeEventValue().event;
          const auto& after = edge.to.afterEventValue().event;
          if (before.location.owner == function.owner && before.operandOrdinal == 0 &&
              before.location.point.kind() == MirPointKind::Edge &&
              before.location.point.edgeValue().from == function.blocks[0].id &&
              before.location.point.edgeValue().edgeOrdinal == 0 &&
              before.location.point.edgeValue().to == call.normalTarget &&
              after.location.owner == before.location.owner &&
              after.location.point.kind() == MirPointKind::Edge && after.operandOrdinal == 0 &&
              after.location.point.edgeValue().from == function.blocks[0].id &&
              after.location.point.edgeValue().edgeOrdinal == 0 &&
              after.location.point.edgeValue().to == call.normalTarget) {
            foundCommitExit = true;
          }
        }
        if (edge.from.kind() == facts::OwnershipPointKind::AfterEvent &&
            edge.to.kind() == facts::OwnershipPointKind::Cfg &&
            edge.to.cfgValue().point.kind() == MirPointKind::BeforeStatement) {
          const auto& event = edge.from.afterEventValue().event;
          const auto& target = edge.to.cfgValue().point.beforeStatementValue();
          if (event.location.owner == function.owner &&
              event.location.point.kind() == MirPointKind::Edge &&
              event.location.point.edgeValue().from == function.blocks[0].id &&
              event.location.point.edgeValue().edgeOrdinal == 0 &&
              event.location.point.edgeValue().to == call.normalTarget &&
              event.operandOrdinal == 0 && target.block == call.normalTarget &&
              target.ordinal == 0) {
            foundTargetTransition = true;
          }
        }
      }
    }
  }
  ZC_EXPECT(foundContinuation);
  ZC_EXPECT(foundCommitEntry);
  ZC_EXPECT(foundCommitExit);
  ZC_EXPECT(foundTargetTransition);
}

ZC_TEST("Move paths retain a parameter dereference after its root") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  const auto& builtMir = fixture.builtMir();
  const auto& paths = ownershipInputs(fixture.compilerSession()).movePaths();
  ZC_REQUIRE(builtMir.functions().size() == 1);
  const auto& function = builtMir.functions()[0];
  ZC_REQUIRE(function.locals.size() == 2);
  const auto parameter = function.locals[0].id;
  ZC_REQUIRE(function.locals[0].kind == mir::MirLocalKind::Parameter);
  ZC_REQUIRE(paths.functions().size() == 1);
  const auto& facts = paths.functions()[0].facts;
  zc::Maybe<size_t> rootIndex;
  zc::Maybe<size_t> dereferenceIndex;
  for (size_t index = 0; index < facts.size(); ++index) {
    const auto& place = facts[index].key.place;
    if (place.local() != parameter) continue;
    if (place.projections().size() == 0) rootIndex = index;
    if (place.projections().size() == 1 &&
        place.projections()[0].kind() == mir::MirProjectionKind::Dereference) {
      dereferenceIndex = index;
    }
  }
  ZC_REQUIRE(rootIndex != zc::none);
  ZC_REQUIRE(dereferenceIndex != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(rootIndex) < ZC_REQUIRE_NONNULL(dereferenceIndex));
  const auto& root = facts[ZC_REQUIRE_NONNULL(rootIndex)];
  const auto& dereference = facts[ZC_REQUIRE_NONNULL(dereferenceIndex)];
  ZC_REQUIRE(dereference.parent != zc::none);
  ZC_IF_SOME(parent, dereference.parent) {
    ZC_EXPECT(parent.owner == root.key.owner);
    ZC_EXPECT(parent.place.local() == root.key.place.local());
    ZC_EXPECT(parent.place.projections().size() == 0);
  }
  ZC_EXPECT(paths.conflicts(root.key, dereference.key));
  ZC_EXPECT(paths.conflicts(dereference.key, root.key));
}

namespace {

identity::SemanticTypeId conflictPlaceType() { return identity::SemanticTypeId(); }

mir::MirPlace conflictPlace(uint32_t local, zc::Vector<mir::MirProjection>&& projections) {
  return mir::MirPlace(ZC_REQUIRE_NONNULL(mir::MirLocalId::fromOrdinal(local)), conflictPlaceType(),
                       zc::mv(projections), conflictPlaceType());
}

mir::MirPlace conflictPlace(uint32_t local, mir::MirProjection&& projection) {
  zc::Vector<mir::MirProjection> projections;
  projections.add(zc::mv(projection));
  return conflictPlace(local, zc::mv(projections));
}

mir::MirPlace conflictPlace(uint32_t local, mir::MirProjection&& first,
                            mir::MirProjection&& second) {
  zc::Vector<mir::MirProjection> projections;
  projections.add(zc::mv(first));
  projections.add(zc::mv(second));
  return conflictPlace(local, zc::mv(projections));
}

mir::MirProjection conflictField(identity::DefId field) {
  return mir::MirProjection::field(field, conflictPlaceType(), conflictPlaceType());
}

mir::MirProjection conflictIndex(uint32_t index) {
  return mir::MirProjection::index(ZC_REQUIRE_NONNULL(mir::MirLocalId::fromOrdinal(index)),
                                   conflictPlaceType(), conflictPlaceType());
}

mir::MirProjection conflictDereference() {
  return mir::MirProjection::dereference(conflictPlaceType(), conflictPlaceType());
}

mir::MirProjection conflictDowncast(identity::DefId variant) {
  return mir::MirProjection::downcast(variant, conflictPlaceType(), conflictPlaceType());
}

zc::Maybe<mir::MirProjection> conflictSubslice(uint32_t first, uint32_t pastLast) {
  return mir::MirProjection::subslice(first, pastLast, conflictPlaceType(), conflictPlaceType());
}

}  // namespace

ZC_TEST("Move-path places on distinct locals never conflict") {
  auto firstRoot = conflictPlace(1, zc::Vector<mir::MirProjection>{});
  auto secondRoot = conflictPlace(2, zc::Vector<mir::MirProjection>{});
  ZC_EXPECT(!facts::placesConflict(firstRoot, secondRoot));
  ZC_EXPECT(!facts::placesConflict(secondRoot, firstRoot));
  auto firstField = conflictPlace(1, conflictField(identity::DefId()));
  auto secondField = conflictPlace(2, conflictField(identity::DefId()));
  ZC_EXPECT(!facts::placesConflict(firstField, secondField));
  ZC_EXPECT(!facts::placesConflict(secondField, firstField));
}

ZC_TEST("Move-path places conflict with their projection prefixes") {
  auto root = conflictPlace(1, zc::Vector<mir::MirProjection>{});
  auto field = conflictPlace(1, conflictField(identity::DefId()));
  ZC_EXPECT(facts::placesConflict(root, field));
  ZC_EXPECT(facts::placesConflict(field, root));
  auto nested = conflictPlace(1, conflictField(identity::DefId()), conflictIndex(2));
  ZC_EXPECT(facts::placesConflict(root, nested));
  ZC_EXPECT(facts::placesConflict(nested, root));
  ZC_EXPECT(facts::placesConflict(field, nested));
  ZC_EXPECT(facts::placesConflict(nested, field));
}

ZC_TEST("Move-path sibling fields and distinct downcast variants do not conflict") {
  OwnershipPipelineFixture fixture(
      "struct Pair { mut left: i32, mut right: i32, }\n"
      "fun entry() -> i32 { mut pair: Pair; pair.left = 0; pair.right = 0; return pair.left; }"_zc);
  const auto& paths = ownershipInputs(fixture.compilerSession()).movePaths();
  ZC_REQUIRE(paths.functions().size() == 1);
  zc::Maybe<identity::DefId> firstField;
  zc::Maybe<identity::DefId> secondField;
  for (const auto& fact : paths.functions()[0].facts) {
    const auto& place = fact.key.place;
    if (place.projections().size() != 1 ||
        place.projections()[0].kind() != mir::MirProjectionKind::Field) {
      continue;
    }
    if (firstField == zc::none)
      firstField = place.projections()[0].fieldValue().field;
    else
      secondField = place.projections()[0].fieldValue().field;
  }
  ZC_REQUIRE(firstField != zc::none);
  ZC_REQUIRE(secondField != zc::none);
  const auto left = ZC_REQUIRE_NONNULL(firstField);
  const auto right = ZC_REQUIRE_NONNULL(secondField);
  ZC_REQUIRE(left != right);

  auto leftPlace = conflictPlace(1, conflictField(left));
  auto rightPlace = conflictPlace(1, conflictField(right));
  ZC_EXPECT(!facts::placesConflict(leftPlace, rightPlace));
  ZC_EXPECT(!facts::placesConflict(rightPlace, leftPlace));
  auto sameField = conflictPlace(1, conflictField(left));
  ZC_EXPECT(facts::placesConflict(leftPlace, sameField));
  // The shared field extends the prefix, so a deeper sibling divergence still separates.
  auto leftLeft = conflictPlace(1, conflictField(left), conflictField(left));
  auto leftRight = conflictPlace(1, conflictField(left), conflictField(right));
  ZC_EXPECT(!facts::placesConflict(leftLeft, leftRight));
  ZC_EXPECT(!facts::placesConflict(leftRight, leftLeft));

  auto firstVariant = conflictPlace(1, conflictDowncast(left));
  auto secondVariant = conflictPlace(1, conflictDowncast(right));
  ZC_EXPECT(!facts::placesConflict(firstVariant, secondVariant));
  ZC_EXPECT(!facts::placesConflict(secondVariant, firstVariant));
  auto sameVariant = conflictPlace(1, conflictDowncast(left));
  ZC_EXPECT(facts::placesConflict(firstVariant, sameVariant));
}

ZC_TEST("Move-path index projections conflict with every projection kind") {
  auto firstIndex = conflictPlace(1, conflictIndex(2));
  auto secondIndex = conflictPlace(1, conflictIndex(3));
  ZC_EXPECT(facts::placesConflict(firstIndex, secondIndex));
  ZC_EXPECT(facts::placesConflict(secondIndex, firstIndex));
  ZC_EXPECT(facts::placesConflict(firstIndex, conflictPlace(1, conflictField(identity::DefId()))));
  ZC_EXPECT(facts::placesConflict(firstIndex, conflictPlace(1, conflictDereference())));
  ZC_EXPECT(
      facts::placesConflict(firstIndex, conflictPlace(1, conflictDowncast(identity::DefId()))));
  auto slice = ZC_REQUIRE_NONNULL(conflictSubslice(0, 4));
  ZC_EXPECT(facts::placesConflict(firstIndex, conflictPlace(1, zc::mv(slice))));
}

ZC_TEST("Move-path dereference projections conflict with every projection kind") {
  auto firstDereference = conflictPlace(1, conflictDereference());
  auto secondDereference = conflictPlace(1, conflictDereference());
  ZC_EXPECT(facts::placesConflict(firstDereference, secondDereference));
  ZC_EXPECT(
      facts::placesConflict(firstDereference, conflictPlace(1, conflictField(identity::DefId()))));
  ZC_EXPECT(facts::placesConflict(firstDereference, conflictPlace(1, conflictIndex(2))));
  ZC_EXPECT(facts::placesConflict(firstDereference,
                                  conflictPlace(1, conflictDowncast(identity::DefId()))));
  auto slice = ZC_REQUIRE_NONNULL(conflictSubslice(0, 4));
  ZC_EXPECT(facts::placesConflict(firstDereference, conflictPlace(1, zc::mv(slice))));
}

ZC_TEST("Move-path subslices conflict exactly on overlapping ranges") {
  auto whole = conflictPlace(1, ZC_REQUIRE_NONNULL(conflictSubslice(0, 4)));
  auto identical = conflictPlace(1, ZC_REQUIRE_NONNULL(conflictSubslice(0, 4)));
  ZC_EXPECT(facts::placesConflict(whole, identical));
  auto overlapping = conflictPlace(1, ZC_REQUIRE_NONNULL(conflictSubslice(2, 6)));
  ZC_EXPECT(facts::placesConflict(whole, overlapping));
  ZC_EXPECT(facts::placesConflict(overlapping, whole));
  auto touching = conflictPlace(1, ZC_REQUIRE_NONNULL(conflictSubslice(4, 6)));
  ZC_EXPECT(!facts::placesConflict(whole, touching));
  ZC_EXPECT(!facts::placesConflict(touching, whole));
  auto separated = conflictPlace(1, ZC_REQUIRE_NONNULL(conflictSubslice(5, 7)));
  ZC_EXPECT(!facts::placesConflict(whole, separated));
  ZC_EXPECT(!facts::placesConflict(separated, whole));
  auto empty = conflictPlace(1, ZC_REQUIRE_NONNULL(conflictSubslice(2, 2)));
  ZC_EXPECT(!facts::placesConflict(whole, empty));
  ZC_EXPECT(!facts::placesConflict(empty, whole));
  ZC_EXPECT(facts::placesConflict(empty, conflictPlace(1, conflictField(identity::DefId()))));
  ZC_EXPECT(facts::placesConflict(empty, conflictPlace(1, conflictIndex(2))));
  ZC_EXPECT(facts::placesConflict(empty, conflictPlace(1, conflictDereference())));
  ZC_EXPECT(facts::placesConflict(empty, conflictPlace(1, conflictDowncast(identity::DefId()))));
}

ZC_TEST("Move-path mixed-kind projection pairs conflict") {
  auto field = conflictPlace(1, conflictField(identity::DefId()));
  auto downcast = conflictPlace(1, conflictDowncast(identity::DefId()));
  auto slice = conflictPlace(1, ZC_REQUIRE_NONNULL(conflictSubslice(0, 4)));
  ZC_EXPECT(facts::placesConflict(field, downcast));
  ZC_EXPECT(facts::placesConflict(downcast, field));
  ZC_EXPECT(facts::placesConflict(field, slice));
  ZC_EXPECT(facts::placesConflict(slice, field));
  ZC_EXPECT(facts::placesConflict(downcast, slice));
  ZC_EXPECT(facts::placesConflict(slice, downcast));
}

ZC_TEST("Move-path index and subslice divergence at depth resolves through shared suffixes") {
  // Shared field prefix, index divergence at depth 1: indices may alias.
  auto firstIndexed = conflictPlace(1, conflictField(identity::DefId()), conflictIndex(2));
  auto secondIndexed = conflictPlace(1, conflictField(identity::DefId()), conflictIndex(3));
  ZC_EXPECT(facts::placesConflict(firstIndexed, secondIndexed));
  ZC_EXPECT(facts::placesConflict(secondIndexed, firstIndexed));
  // Shared field prefix, disjoint subslices at depth 1 separate the suffixes.
  auto firstDisjoint = ZC_REQUIRE_NONNULL(conflictSubslice(0, 2));
  auto secondDisjoint = ZC_REQUIRE_NONNULL(conflictSubslice(2, 4));
  auto firstSliced = conflictPlace(1, conflictField(identity::DefId()), zc::mv(firstDisjoint));
  auto secondSliced = conflictPlace(1, conflictField(identity::DefId()), zc::mv(secondDisjoint));
  ZC_EXPECT(!facts::placesConflict(firstSliced, secondSliced));
  ZC_EXPECT(!facts::placesConflict(secondSliced, firstSliced));
  // Shared field prefix, overlapping subslices at depth 1 conflict.
  auto firstOverlap = ZC_REQUIRE_NONNULL(conflictSubslice(0, 3));
  auto secondOverlap = ZC_REQUIRE_NONNULL(conflictSubslice(2, 5));
  auto firstOverlapping = conflictPlace(1, conflictField(identity::DefId()), zc::mv(firstOverlap));
  auto secondOverlapping =
      conflictPlace(1, conflictField(identity::DefId()), zc::mv(secondOverlap));
  ZC_EXPECT(facts::placesConflict(firstOverlapping, secondOverlapping));
  ZC_EXPECT(facts::placesConflict(secondOverlapping, firstOverlapping));
  // Subslice divergence at depth 1 with a shared field suffix stays disjoint.
  auto firstSuffix = ZC_REQUIRE_NONNULL(conflictSubslice(0, 2));
  auto secondSuffix = ZC_REQUIRE_NONNULL(conflictSubslice(2, 4));
  auto firstSuffixed = conflictPlace(1, zc::mv(firstSuffix), conflictField(identity::DefId()));
  auto secondSuffixed = conflictPlace(1, zc::mv(secondSuffix), conflictField(identity::DefId()));
  ZC_EXPECT(!facts::placesConflict(firstSuffixed, secondSuffixed));
  ZC_EXPECT(!facts::placesConflict(secondSuffixed, firstSuffixed));
  // A projection prefix conflicts with its deeper index descendant.
  auto field = conflictPlace(1, conflictField(identity::DefId()));
  ZC_EXPECT(facts::placesConflict(field, firstIndexed));
  ZC_EXPECT(facts::placesConflict(firstIndexed, field));
}

ZC_TEST("Move-path dereference chains conflict with their projection prefixes") {
  auto root = conflictPlace(1, zc::Vector<mir::MirProjection>{});
  auto deref = conflictPlace(1, conflictDereference());
  auto doubleDeref = conflictPlace(1, conflictDereference(), conflictDereference());
  ZC_EXPECT(facts::placesConflict(root, deref));
  ZC_EXPECT(facts::placesConflict(deref, root));
  ZC_EXPECT(facts::placesConflict(deref, doubleDeref));
  ZC_EXPECT(facts::placesConflict(doubleDeref, deref));
  ZC_EXPECT(facts::placesConflict(root, doubleDeref));
  ZC_EXPECT(facts::placesConflict(doubleDeref, root));
  ZC_EXPECT(facts::placesConflict(deref, conflictPlace(1, conflictDereference())));
}

ZC_TEST("Move-path dereference and deep field chains separate sibling fields at depth") {
  OwnershipPipelineFixture fixture(
      "struct Pair { mut left: i32, mut right: i32, }\n"
      "fun entry() -> i32 { mut pair: Pair; pair.left = 0; pair.right = 0; return pair.left; }"_zc);
  const auto& paths = ownershipInputs(fixture.compilerSession()).movePaths();
  ZC_REQUIRE(paths.functions().size() == 1);
  zc::Maybe<identity::DefId> firstField;
  zc::Maybe<identity::DefId> secondField;
  for (const auto& fact : paths.functions()[0].facts) {
    const auto& place = fact.key.place;
    if (place.projections().size() != 1 ||
        place.projections()[0].kind() != mir::MirProjectionKind::Field) {
      continue;
    }
    if (firstField == zc::none)
      firstField = place.projections()[0].fieldValue().field;
    else
      secondField = place.projections()[0].fieldValue().field;
  }
  ZC_REQUIRE(firstField != zc::none);
  ZC_REQUIRE(secondField != zc::none);
  const auto left = ZC_REQUIRE_NONNULL(firstField);
  const auto right = ZC_REQUIRE_NONNULL(secondField);
  ZC_REQUIRE(left != right);

  // Shared dereference prefix, sibling fields at depth 1: disjoint.
  auto derefLeft = conflictPlace(1, conflictDereference(), conflictField(left));
  auto derefRight = conflictPlace(1, conflictDereference(), conflictField(right));
  ZC_EXPECT(!facts::placesConflict(derefLeft, derefRight));
  ZC_EXPECT(!facts::placesConflict(derefRight, derefLeft));
  // Shared field prefix, sibling fields at depth 2: disjoint.
  auto leftLeft = conflictPlace(1, conflictField(left), conflictField(left));
  auto leftRight = conflictPlace(1, conflictField(left), conflictField(right));
  ZC_EXPECT(!facts::placesConflict(leftLeft, leftRight));
  ZC_EXPECT(!facts::placesConflict(leftRight, leftLeft));
  // Sibling fields at depth 1 separate deeper projections.
  auto rightLeft = conflictPlace(1, conflictField(right), conflictField(left));
  ZC_EXPECT(!facts::placesConflict(leftRight, rightLeft));
  ZC_EXPECT(!facts::placesConflict(rightLeft, leftRight));
  // A three-projection chain conflicts with its root and immediate prefix.
  zc::Vector<mir::MirProjection> chain;
  chain.add(conflictField(left));
  chain.add(conflictField(right));
  chain.add(conflictField(left));
  auto deepChain = conflictPlace(1, zc::mv(chain));
  auto root = conflictPlace(1, zc::Vector<mir::MirProjection>{});
  ZC_EXPECT(facts::placesConflict(root, deepChain));
  ZC_EXPECT(facts::placesConflict(deepChain, root));
  ZC_EXPECT(facts::placesConflict(leftRight, deepChain));
  ZC_EXPECT(facts::placesConflict(deepChain, leftRight));
}

ZC_TEST("Reference definition verifier rejects tampered definition inputs") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();
  const auto& movePaths = ownershipInputs(session).movePaths();
  const auto& loans = ownershipInputs(session).loans();

  auto candidateResult =
      facts::ReferenceDefinitionBuilder::build(movePaths, loans, builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.definitions.size() == 1);
  candidate.definitions[0].introduction.operandOrdinal = 1;

  auto verifiedResult = facts::ReferenceDefinitionVerifier::verify(zc::mv(candidate), movePaths,
                                                                   loans, builtMir, overlay);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);

  auto originCandidateResult =
      facts::ReferenceDefinitionBuilder::build(movePaths, loans, builtMir, overlay);
  ZC_REQUIRE(originCandidateResult.isVerified());
  auto originCandidate = zc::mv(originCandidateResult).takeVerified();
  originCandidate.definitions[0].origin.entry.operandOrdinal = 1;
  auto originResult = facts::ReferenceDefinitionVerifier::verify(zc::mv(originCandidate), movePaths,
                                                                 loans, builtMir, overlay);
  ZC_REQUIRE(originResult.isIrInvariantRejected());
  ZC_EXPECT(originResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(originResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);

  auto activationCandidateResult =
      facts::ReferenceDefinitionBuilder::build(movePaths, loans, builtMir, overlay);
  ZC_REQUIRE(activationCandidateResult.isVerified());
  auto activationCandidate = zc::mv(activationCandidateResult).takeVerified();
  activationCandidate.definitions[0].origin.activation = facts::OwnershipPoint::beforeEvent(
      activationCandidate.definitions[0].origin.activation.afterEventValue().event);
  auto activationResult = facts::ReferenceDefinitionVerifier::verify(
      zc::mv(activationCandidate), movePaths, loans, builtMir, overlay);
  ZC_REQUIRE(activationResult.isIrInvariantRejected());
  ZC_EXPECT(activationResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(activationResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);

  auto rootCandidateResult =
      facts::ReferenceDefinitionBuilder::build(movePaths, loans, builtMir, overlay);
  ZC_REQUIRE(rootCandidateResult.isVerified());
  auto rootCandidate = zc::mv(rootCandidateResult).takeVerified();
  rootCandidate.definitions[0].origin.detail = facts::ParameterReferenceOrigin{1};
  auto rootResult = facts::ReferenceDefinitionVerifier::verify(zc::mv(rootCandidate), movePaths,
                                                               loans, builtMir, overlay);
  ZC_REQUIRE(rootResult.isIrInvariantRejected());
  ZC_EXPECT(rootResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(rootResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);

  auto referentCandidateResult =
      facts::ReferenceDefinitionBuilder::build(movePaths, loans, builtMir, overlay);
  ZC_REQUIRE(referentCandidateResult.isVerified());
  auto referentCandidate = zc::mv(referentCandidateResult).takeVerified();
  referentCandidate.definitions[0].origin.referent.owner = identity::DefId();
  auto referentResult = facts::ReferenceDefinitionVerifier::verify(
      zc::mv(referentCandidate), movePaths, loans, builtMir, overlay);
  ZC_REQUIRE(referentResult.isIrInvariantRejected());
  ZC_EXPECT(referentResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(referentResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);

  auto returnCandidateResult =
      facts::ReferenceDefinitionBuilder::build(movePaths, loans, builtMir, overlay);
  ZC_REQUIRE(returnCandidateResult.isVerified());
  auto returnCandidate = zc::mv(returnCandidateResult).takeVerified();
  returnCandidate.definitions[0].returned.operandOrdinal = 1;
  auto returnResult = facts::ReferenceDefinitionVerifier::verify(zc::mv(returnCandidate), movePaths,
                                                                 loans, builtMir, overlay);
  ZC_REQUIRE(returnResult.isIrInvariantRejected());
  ZC_EXPECT(returnResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(returnResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);

  auto destinationCandidateResult =
      facts::ReferenceDefinitionBuilder::build(movePaths, loans, builtMir, overlay);
  ZC_REQUIRE(destinationCandidateResult.isVerified());
  auto destinationCandidate = zc::mv(destinationCandidateResult).takeVerified();
  destinationCandidate.definitions[0].destination.owner = identity::DefId();
  auto destinationResult = facts::ReferenceDefinitionVerifier::verify(
      zc::mv(destinationCandidate), movePaths, loans, builtMir, overlay);
  ZC_REQUIRE(destinationResult.isIrInvariantRejected());
  ZC_EXPECT(destinationResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(destinationResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);

  auto livenessCandidateResult =
      facts::ReferenceDefinitionBuilder::build(movePaths, loans, builtMir, overlay);
  ZC_REQUIRE(livenessCandidateResult.isVerified());
  auto livenessCandidate = zc::mv(livenessCandidateResult).takeVerified();
  livenessCandidate.definitions[0].livePoints.beforeReturn =
      facts::OwnershipPoint::afterEvent(livenessCandidate.definitions[0].returned);
  auto livenessResult = facts::ReferenceDefinitionVerifier::verify(
      zc::mv(livenessCandidate), movePaths, loans, builtMir, overlay);
  ZC_REQUIRE(livenessResult.isIrInvariantRejected());
  ZC_EXPECT(livenessResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(livenessResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Reference definition verifier rejects a forged local alias origin") {
  OwnershipPipelineFixture fixture(
      "fun reborrow(value: &i32) -> &i32 { let local = value; return &*local; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();
  const auto& inputs = ownershipInputs(session);
  ZC_REQUIRE(builtMir.functions().size() == 1);
  const auto& function = builtMir.functions()[0];
  ZC_REQUIRE(function.locals.size() == 3);

  auto candidateResult = facts::ReferenceDefinitionBuilder::build(
      inputs.movePaths(), inputs.loans(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.definitions.size() == 1);
  auto& referent = candidate.definitions[0].origin.referent.place;
  ZC_EXPECT(referent.local() == function.locals[1].id);
  zc::Vector<mir::MirProjection> projections;
  for (const auto& projection : referent.projections()) { projections.add(projection.clone()); }
  referent = mir::MirPlace(function.locals[0].id, function.locals[0].type, zc::mv(projections),
                           referent.resultType());

  auto verifiedResult = facts::ReferenceDefinitionVerifier::verify(
      zc::mv(candidate), inputs.movePaths(), inputs.loans(), builtMir, overlay);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Parameter reborrow region verifier rejects tampered members") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  OwnershipPipelineFixture foreignFixture(
      "fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& inputs = ownershipInputs(session);
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();

  auto candidateResult = facts::ReborrowRegionBuilder::build(
      inputs.flow(), inputs.loans(), inputs.references(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.regions.size() == 1);
  ZC_REQUIRE(candidate.regions[0].members.size() == 6);
  candidate.regions[0].members[0] = facts::OwnershipPoint::beforeEvent(candidate.regions[0].loan);

  auto verifiedResult = facts::ReborrowRegionVerifier::verify(
      zc::mv(candidate), inputs.flow(), inputs.loans(), inputs.references(), builtMir, overlay);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);

  auto foreignFlow =
      facts::ReborrowRegionBuilder::build(ownershipInputs(foreignFixture.compilerSession()).flow(),
                                          inputs.loans(), inputs.references(), builtMir, overlay);
  ZC_REQUIRE(foreignFlow.isIrInvariantRejected());
  ZC_EXPECT(foreignFlow.invariantFailures().facts().size() == 1);
  ZC_EXPECT(foreignFlow.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InputRevisionMismatch);
}

ZC_TEST("Parameter reborrow reference-state verifier rejects tampered point") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& inputs = ownershipInputs(session);
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();

  auto candidateResult =
      facts::ReborrowStateBuilder::build(inputs.references(), inputs.regions(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.states.size() == 5);
  candidate.states[0].point = facts::OwnershipPoint::beforeEvent(candidate.states[0].loan);

  auto verifiedResult = facts::ReborrowStateVerifier::verify(zc::mv(candidate), inputs.references(),
                                                             inputs.regions(), builtMir, overlay);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Local borrow reference definitions derive a StorageLive origin") {
  RejectedBorrowPipelineFixture fixture(
      "fun borrow_local() -> &i32 { let value: i32 = 0; return &value; }"_zc);
  const auto inventory = buildRejectedBorrowInventory(fixture);

  ZC_REQUIRE(inventory.references.definitions().size() == 1);
  const auto& definition = inventory.references.definitions()[0];
  ZC_EXPECT(definition.origin.detail.is<facts::LocalReferenceOrigin>());
  ZC_EXPECT(definition.origin.entry.location.point.kind() == MirPointKind::BeforeStatement);
  ZC_EXPECT(definition.origin.entry.operandOrdinal == 0);

  ZC_REQUIRE(inventory.regions.regions().size() == 1);
  const auto& region = inventory.regions.regions()[0];
  ZC_EXPECT(region.origin.is<facts::LocalReferenceOrigin>());
  ZC_EXPECT(region.members.size() == 6);

  ZC_REQUIRE(inventory.states.states().size() == 5);
  for (const auto& state : inventory.states.states()) {
    ZC_EXPECT(state.origin.is<facts::LocalReferenceOrigin>());
  }
}

ZC_TEST("Local borrow reference verifier rejects a forged parameter origin") {
  RejectedBorrowPipelineFixture fixture(
      "fun borrow_local() -> &i32 { let value: i32 = 0; return &value; }"_zc);
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = fixture.overlay();

  auto movePathCandidate = facts::MovePathBuilder::build(builtMir, overlay);
  ZC_REQUIRE(movePathCandidate.isVerified());
  auto movePaths =
      facts::MovePathVerifier::verify(zc::mv(movePathCandidate).takeVerified(), builtMir, overlay);
  ZC_REQUIRE(movePaths.isVerified());
  auto loanCandidate = facts::LoanBuilder::build(movePaths.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(loanCandidate.isVerified());
  auto loans = facts::LoanVerifier::verify(zc::mv(loanCandidate).takeVerified(),
                                           movePaths.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(loans.isVerified());

  auto candidateResult = facts::ReferenceDefinitionBuilder::build(
      movePaths.verifiedValue(), loans.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.definitions.size() == 1);
  candidate.definitions[0].origin.detail = facts::ParameterReferenceOrigin{0};

  auto verifiedResult = facts::ReferenceDefinitionVerifier::verify(
      zc::mv(candidate), movePaths.verifiedValue(), loans.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Local borrow loan facts record a shared borrow kind") {
  RejectedBorrowPipelineFixture fixture(
      "fun borrow_local() -> &i32 { let value: i32 = 0; return &value; }"_zc);
  const auto inventory = buildRejectedBorrowInventory(fixture);

  ZC_REQUIRE(inventory.loans.loans().size() == 1);
  const auto& loan = inventory.loans.loans()[0];
  ZC_EXPECT(loan.kind == mir::MirBorrowKind::Shared);
  ZC_EXPECT(loan.source.place.projections().size() == 0);
  ZC_EXPECT(loan.destination.place.projections().size() == 0);
}

ZC_TEST("Mutable local borrow reference definitions derive a StorageLive origin") {
  RejectedBorrowPipelineFixture fixture(
      "fun borrow_local_mut() -> &mut i32 { mut value: i32 = 0; return &mut value; }"_zc);
  const auto inventory = buildRejectedBorrowInventory(fixture);

  ZC_REQUIRE(inventory.loans.loans().size() == 1);
  const auto& loan = inventory.loans.loans()[0];
  ZC_EXPECT(loan.kind == mir::MirBorrowKind::Mutable);

  ZC_REQUIRE(inventory.references.definitions().size() == 1);
  const auto& definition = inventory.references.definitions()[0];
  ZC_EXPECT(definition.origin.detail.is<facts::LocalReferenceOrigin>());
  ZC_EXPECT(definition.origin.entry.location.point.kind() == MirPointKind::BeforeStatement);
  ZC_EXPECT(definition.origin.entry.operandOrdinal == 0);

  ZC_REQUIRE(inventory.regions.regions().size() == 1);
  const auto& region = inventory.regions.regions()[0];
  ZC_EXPECT(region.origin.is<facts::LocalReferenceOrigin>());
  ZC_EXPECT(region.members.size() == 6);

  ZC_REQUIRE(inventory.states.states().size() == 5);
  for (const auto& state : inventory.states.states()) {
    ZC_EXPECT(state.origin.is<facts::LocalReferenceOrigin>());
  }
}

ZC_TEST("Initialization source verifier accepts matching inputs and rejects foreign lineage") {
  OwnershipPipelineFixture first("let value = 0;"_zc);
  OwnershipPipelineFixture second("let value = 1;"_zc);
  const auto& firstSession = first.compilerSession();
  const auto& secondSession = second.compilerSession();
  ZC_REQUIRE(firstSession.getOwnershipCheckedMirModules().size() == 1);
  const auto& firstInputs = ownershipInputs(firstSession);
  ZC_REQUIRE(secondSession.getOwnershipCheckedMirModules().size() == 1);
  const auto& secondInputs = ownershipInputs(secondSession);

  auto accepted = facts::InitializationSourceVerifier::verify(
      first.builtMir(), firstSession.getOwnershipCheckedMirModules()[0].eventOverlay(),
      firstInputs.initialization());
  ZC_EXPECT(accepted.isVerified());

  auto foreignFacts = facts::InitializationSourceVerifier::verify(
      first.builtMir(), firstSession.getOwnershipCheckedMirModules()[0].eventOverlay(),
      secondInputs.initialization());
  ZC_REQUIRE(foreignFacts.isIrInvariantRejected());
  auto factFailures = zc::mv(foreignFacts).takeInvariantFailures();
  ZC_REQUIRE(factFailures.facts().size() == 1);
  ZC_EXPECT(factFailures.facts()[0].kind() == ir::IrFailureKind::InputRevisionMismatch);

  auto foreignOverlay = facts::InitializationSourceVerifier::verify(
      first.builtMir(), secondSession.getOwnershipCheckedMirModules()[0].eventOverlay(),
      firstInputs.initialization());
  ZC_REQUIRE(foreignOverlay.isIrInvariantRejected());
  auto overlayFailures = zc::mv(foreignOverlay).takeInvariantFailures();
  ZC_REQUIRE(overlayFailures.facts().size() == 1);
  ZC_EXPECT(overlayFailures.facts()[0].kind() == ir::IrFailureKind::InputRevisionMismatch);
}

ZC_TEST("Initialization verifier rejects a tampered direct call result state") {
  OwnershipPipelineFixture fixture(
      "fun helper() -> i32 { return 0; }\n"
      "fun entry() -> i32 { return helper(); }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  const auto& inputs = ownershipInputs(session);

  auto candidateResult = facts::InitializationBuilder::build(
      builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay(), inputs.flow(),
      inputs.movePaths());
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  bool tampered = false;
  for (const auto& function : builtMir.functions()) {
    if (function.blocks.size() != 2) continue;
    const auto& call = function.blocks[0].terminator.callValue();
    for (auto& functionFacts : candidate.functions) {
      if (functionFacts.owner != function.owner) continue;
      for (auto& fact : functionFacts.facts) {
        if (fact.point.kind() != MirPointKind::Edge ||
            fact.point.edgeValue().from != function.blocks[0].id ||
            fact.point.edgeValue().edgeOrdinal != 0 ||
            fact.point.edgeValue().to != call.normalTarget ||
            fact.key.place.local() != call.destination.local()) {
          continue;
        }
        ZC_REQUIRE(fact.state == facts::InitializationState::initialized());
        fact.state = facts::InitializationState::uninitialized();
        tampered = true;
      }
    }
  }
  ZC_REQUIRE(tampered);

  auto verifiedResult = facts::InitializationVerifier::verify(
      zc::mv(candidate), builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay(),
      inputs.flow(), inputs.movePaths());
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Initialization verifier rejects a tampered direct call storage end cause") {
  OwnershipPipelineFixture fixture(
      "fun helper() -> i32 { return 0; }\n"
      "fun entry() -> i32 { return helper(); }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  const auto& inputs = ownershipInputs(session);

  auto candidateResult = facts::InitializationBuilder::build(
      builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay(), inputs.flow(),
      inputs.movePaths());
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  bool tampered = false;
  for (const auto& function : builtMir.functions()) {
    if (function.blocks.size() != 2) continue;
    const auto& continuation = function.blocks[1];
    ZC_REQUIRE(continuation.statements.size() == 3);
    ZC_REQUIRE(continuation.statements[2].kind() == mir::MirStatementKind::StorageDead);
    const auto temporary = continuation.statements[2].storageLocal();
    for (auto& functionFacts : candidate.functions) {
      if (functionFacts.owner != function.owner) continue;
      for (auto& fact : functionFacts.facts) {
        if (fact.point.kind() != MirPointKind::AfterStatement ||
            fact.point.afterStatementValue().block != continuation.id ||
            fact.point.afterStatementValue().ordinal != 2 || fact.key.place.local() != temporary) {
          continue;
        }
        ZC_REQUIRE(fact.state == facts::InitializationState::dead());
        ZC_REQUIRE(fact.lossCauses.size() == 1);
        ZC_REQUIRE(fact.lossCauses[0].kind == facts::InitializationLossKind::StorageEnded);
        fact.lossCauses[0].kind = facts::InitializationLossKind::Moved;
        tampered = true;
      }
    }
  }
  ZC_REQUIRE(tampered);

  auto verifiedResult = facts::InitializationVerifier::verify(
      zc::mv(candidate), builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay(),
      inputs.flow(), inputs.movePaths());
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("CompilerSession publishes verified ownership event overlays") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);
  const auto& session = fixture.compilerSession();
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  const auto& inputs = ownershipInputs(session);
  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();
  const auto& builtMir = session.getOwnershipCheckedMirModules()[0].builtMir();
  const auto& movePaths = inputs.movePaths();
  const auto& initialization = inputs.initialization();
  const auto& loans = inputs.loans();
  ZC_EXPECT(overlay.module() == builtMir.module());
  ZC_EXPECT(overlay.builtRevision().digest() == builtMir.revision().digest());
  ZC_EXPECT(overlay.functions().size() == 1);
  ZC_EXPECT(overlay.functions()[0].owner == builtMir.functions()[0].owner);
  ZC_EXPECT(movePaths.module() == builtMir.module());
  ZC_EXPECT(movePaths.builtRevision().digest() == builtMir.revision().digest());
  ZC_EXPECT(movePaths.overlayRevision().digest() == overlay.revision().digest());
  ZC_REQUIRE(movePaths.functions().size() == builtMir.functions().size());
  ZC_REQUIRE(movePaths.functions()[0].facts.size() == builtMir.functions()[0].locals.size());
  for (size_t index = 0; index < movePaths.functions()[0].facts.size(); ++index) {
    const auto& path = movePaths.functions()[0].facts[index].key.place;
    const auto& local = builtMir.functions()[0].locals[index];
    ZC_EXPECT(path.local() == local.id);
    ZC_EXPECT(path.rootType() == local.type);
    ZC_EXPECT(path.resultType() == local.type);
    ZC_EXPECT(path.projections().size() == 0);
  }
  ZC_EXPECT(initialization.module() == builtMir.module());
  ZC_EXPECT(initialization.builtRevision().digest() == builtMir.revision().digest());
  ZC_EXPECT(initialization.overlayRevision().digest() == overlay.revision().digest());
  ZC_EXPECT(loans.module() == builtMir.module());
  ZC_EXPECT(loans.builtRevision().digest() == builtMir.revision().digest());
  ZC_EXPECT(loans.overlayRevision().digest() == overlay.revision().digest());
  ZC_EXPECT(loans.borrowEvidenceRevision().digest() == builtMir.borrowEvidenceRevision().digest());
  ZC_EXPECT(loans.loans().size() == 0);
  ZC_REQUIRE(initialization.functions().size() == 1);
  const auto& states = initialization.functions()[0].facts;
  ZC_REQUIRE(states.size() == 7);
  ZC_EXPECT(states[0].point.kind() == MirPointKind::Entry);
  ZC_EXPECT(states[0].state == facts::InitializationState::dead());
  ZC_EXPECT(states[1].point.kind() == MirPointKind::BeforeStatement);
  ZC_EXPECT(states[1].state == facts::InitializationState::dead());
  ZC_EXPECT(states[2].point.kind() == MirPointKind::AfterStatement);
  ZC_EXPECT(states[2].state == facts::InitializationState::uninitialized());
  ZC_EXPECT(states[3].point.kind() == MirPointKind::BeforeStatement);
  ZC_EXPECT(states[3].state == facts::InitializationState::uninitialized());
  ZC_EXPECT(states[4].point.kind() == MirPointKind::AfterStatement);
  ZC_EXPECT(states[4].state == facts::InitializationState::initialized());
  ZC_EXPECT(states[5].point.kind() == MirPointKind::BeforeTerminator);
  ZC_EXPECT(states[5].state == facts::InitializationState::initialized());
  ZC_EXPECT(states[6].point.kind() == MirPointKind::Exit);
  ZC_EXPECT(states[6].state == facts::InitializationState::initialized());
  ZC_REQUIRE(states[0].lossCauses.size() == 1);
  ZC_EXPECT(states[0].lossCauses[0].kind == facts::InitializationLossKind::NeverInitialized);
  ZC_EXPECT(states[0].lossCauses[0].event.location.point.kind() == MirPointKind::Entry);
  ZC_REQUIRE(states[2].lossCauses.size() == 1);
  ZC_EXPECT(states[2].lossCauses[0].kind == facts::InitializationLossKind::NeverInitialized);
  ZC_EXPECT(states[2].lossCauses[0].event.location.point.kind() == MirPointKind::BeforeStatement);
  ZC_EXPECT(states[2].lossCauses[0].event.location.point.beforeStatementValue().ordinal == 0);
  ZC_EXPECT(states[4].lossCauses.size() == 0);
  ZC_EXPECT(states[6].lossCauses.size() == 0);
  ZC_EXPECT(session.getIrFailureGroups().size() == 0);
  ZC_EXPECT(session.getIrIdentityInvariantFailures().size() == 0);
}

ZC_TEST("CompilerSession publishes verified ownership inputs for a returned function local") {
  OwnershipPipelineFixture fixture("fun entry() -> i32 { let value = 0; return value; }"_zc);
  const auto& session = fixture.compilerSession();
  ZC_REQUIRE(session.getVerifiedHirModules().size() == 1);
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  const auto& inputs = ownershipInputs(session);

  const auto& hir = session.getVerifiedHirModules()[0];
  ZC_REQUIRE(hir.functions().size() == 1);
  ZC_REQUIRE(hir.locals().size() == 1);
  ZC_REQUIRE(hir.localReferences().size() == 1);
  ZC_EXPECT(hir.locals()[0].local == hir.localReferences()[0].local);
  ZC_EXPECT(hir.localReferences()[0].category == hir::HirValueCategory::Place);

  const auto& builtMir = session.getOwnershipCheckedMirModules()[0].builtMir();
  ZC_REQUIRE(builtMir.functions().size() == 1);
  const auto& function = builtMir.functions()[0];
  ZC_EXPECT(function.kind == mir::MirFunctionKind::Function);
  ZC_REQUIRE(function.locals.size() == 1);
  ZC_EXPECT(function.locals[0].kind == mir::MirLocalKind::UserLocal);
  ZC_EXPECT(function.locals[0].type == hir.locals()[0].type);
  ZC_REQUIRE(function.blocks.size() == 1);
  ZC_REQUIRE(function.blocks[0].statements.size() == 2);
  ZC_EXPECT(function.blocks[0].statements[0].kind() == mir::MirStatementKind::StorageLive);
  ZC_EXPECT(function.blocks[0].statements[1].kind() == mir::MirStatementKind::Assign);
  ZC_EXPECT(function.blocks[0].terminator.kind() == mir::MirTerminatorKind::Return);
  ZC_REQUIRE(function.blocks[0].terminator.returnValue().value != zc::none);
  ZC_IF_SOME(value, function.blocks[0].terminator.returnValue().value) {
    ZC_EXPECT(value.kind() == mir::MirOperandKind::Copy);
    ZC_EXPECT(value.place().local() == function.locals[0].id);
  }

  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();
  ZC_REQUIRE(overlay.functions().size() == 1);
  ZC_EXPECT(overlay.functions()[0].owner == function.owner);
  ZC_EXPECT(overlay.functions()[0].logicalDropPlans.size() == 1);
  ZC_EXPECT(overlay.functions()[0].logicalDropPlans[0].root.local() == function.locals[0].id);

  const auto& paths = inputs.movePaths();
  ZC_REQUIRE(paths.functions().size() == 1);
  ZC_REQUIRE(paths.functions()[0].facts.size() == 1);
  ZC_EXPECT(paths.functions()[0].facts[0].key.place.local() == function.locals[0].id);

  const auto& initialization = inputs.initialization();
  ZC_REQUIRE(initialization.functions().size() == 1);
  const auto& initializationFacts = initialization.functions()[0].facts;
  ZC_REQUIRE(initializationFacts.size() == 7);
  ZC_EXPECT(initializationFacts[0].state == facts::InitializationState::dead());
  ZC_EXPECT(initializationFacts[2].state == facts::InitializationState::uninitialized());
  ZC_EXPECT(initializationFacts[4].state == facts::InitializationState::initialized());
  ZC_EXPECT(initializationFacts[6].state == facts::InitializationState::initialized());
  ZC_EXPECT(initializationFacts[6].lossCauses.size() == 0);
  ZC_EXPECT(session.getIrFailureGroups().size() == 0);
  ZC_EXPECT(session.getIrIdentityInvariantFailures().size() == 0);
}

ZC_TEST("Ownership facts lower a noncopy aggregate local return as a move") {
  OwnershipPipelineFixture fixture(
      "struct Cell { value: i32, }\n"
      "import core::marker::{Copy};\n"
      "impl !Copy for Cell;\n"
      "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  const auto& inputs = ownershipInputs(session);
  ZC_REQUIRE(builtMir.functions().size() == 1);
  const auto& function = builtMir.functions()[0];
  ZC_REQUIRE(function.blocks.size() == 1);
  const auto& block = function.blocks[0];
  ZC_REQUIRE(block.terminator.kind() == mir::MirTerminatorKind::Return);
  ZC_REQUIRE(block.terminator.returnValue().value != zc::none);
  ZC_IF_SOME(value, block.terminator.returnValue().value) {
    ZC_EXPECT(value.kind() == mir::MirOperandKind::Move);
    ZC_EXPECT(value.place().local() == function.locals[0].id);
    ZC_EXPECT(value.place().projections().size() == 0);
  }
  ZC_REQUIRE(inputs.movePaths().functions().size() == 1);
  ZC_REQUIRE(inputs.movePaths().functions()[0].facts.size() == 1);
  ZC_EXPECT(inputs.movePaths().functions()[0].facts[0].key.place.local() == function.locals[0].id);
  ZC_REQUIRE(inputs.initialization().functions().size() == 1);
  const auto& initialization = inputs.initialization().functions()[0].facts;
  ZC_REQUIRE(initialization.size() == 7);
  ZC_EXPECT(initialization[4].state == facts::InitializationState::initialized());
  ZC_EXPECT(initialization[6].state == facts::InitializationState::uninitialized());
  ZC_REQUIRE(initialization[6].lossCauses.size() == 1);
  ZC_EXPECT(initialization[6].lossCauses[0].kind == facts::InitializationLossKind::Moved);

  const auto& resources = inputs.resources();
  ZC_REQUIRE(resources.functions().size() == 1);
  ZC_REQUIRE(resources.functions()[0].facts.size() == 1);
  const auto& resource = resources.functions()[0].facts[0];
  ZC_EXPECT(resource.subject.introduction.location.owner == function.owner);
  ZC_EXPECT(resource.subject.introduction.location.point.kind() == MirPointKind::BeforeStatement);
  ZC_EXPECT(resource.subject.origin.owner == function.owner);
  ZC_EXPECT(resource.subject.origin.place.local() == function.locals[0].id);
  ZC_EXPECT(resource.subject.origin.place.projections().size() == 0);
  ZC_EXPECT(resource.subject.originType == function.locals[0].type);
  ZC_EXPECT(resource.requirement == facts::DropRequirement::Logical);
  ZC_EXPECT(resource.dropAction == zc::none);
}

ZC_TEST("Ownership facts preserve sequential scalar local copies without resources") {
  OwnershipPipelineFixture fixture(
      "fun entry() -> i32 { let first = 1; let second = first; return second; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& inputs = ownershipInputs(session);
  ZC_REQUIRE(builtMir.functions().size() == 1);
  const auto& function = builtMir.functions()[0];
  ZC_REQUIRE(function.locals.size() == 2);
  ZC_REQUIRE(function.blocks.size() == 1);
  const auto& block = function.blocks[0];
  ZC_REQUIRE(block.statements.size() == 4);
  ZC_EXPECT(block.statements[3].kind() == mir::MirStatementKind::Assign);
  const auto& copy = block.statements[3].assignmentValue();
  ZC_EXPECT(copy.initialization == mir::MirInitializationKind::Initialize);
  ZC_EXPECT(copy.destination.local() == function.locals[1].id);
  ZC_EXPECT(copy.value.kind() == mir::MirRvalueKind::Use);
  ZC_EXPECT(copy.value.useValue().operand.kind() == mir::MirOperandKind::Copy);
  ZC_EXPECT(copy.value.useValue().operand.place().local() == function.locals[0].id);

  const auto& paths = inputs.movePaths();
  ZC_REQUIRE(paths.functions().size() == 1);
  ZC_REQUIRE(paths.functions()[0].facts.size() == 2);
  ZC_EXPECT(paths.functions()[0].facts[0].key.place.local() == function.locals[0].id);
  ZC_EXPECT(paths.functions()[0].facts[1].key.place.local() == function.locals[1].id);

  const auto& resources = inputs.resources();
  ZC_REQUIRE(resources.functions().size() == 1);
  ZC_EXPECT(resources.functions()[0].facts.size() == 0);
  ZC_EXPECT(resources.functions()[0].transfers.size() == 0);
  ZC_EXPECT(session.getIrFailureGroups().size() == 0);
  ZC_EXPECT(session.getIrIdentityInvariantFailures().size() == 0);
}

ZC_TEST("Ownership resources preserve a moved sequential aggregate local") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Copy};\n"
      "struct Cell { value: i32, }\n"
      "impl !Copy for Cell;\n"
      "fun entry() -> Cell { let first = Cell { value: 0 }; let second = first; return second; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& inputs = ownershipInputs(session);

  ZC_REQUIRE(builtMir.functions().size() == 1);
  const auto& function = builtMir.functions()[0];
  ZC_REQUIRE(function.locals.size() == 2);
  ZC_REQUIRE(function.blocks.size() == 1);
  ZC_REQUIRE(function.blocks[0].statements.size() == 4);
  const auto& transfer = function.blocks[0].statements[3].assignmentValue();
  ZC_EXPECT(transfer.value.kind() == mir::MirRvalueKind::Use);
  ZC_EXPECT(transfer.value.useValue().operand.kind() == mir::MirOperandKind::Move);
  ZC_EXPECT(transfer.value.useValue().operand.place().local() == function.locals[0].id);
  ZC_EXPECT(transfer.destination.local() == function.locals[1].id);

  const auto& resources = inputs.resources();
  ZC_REQUIRE(resources.functions().size() == 1);
  const auto& resourceFunction = resources.functions()[0];
  ZC_REQUIRE(resourceFunction.facts.size() == 1);
  ZC_REQUIRE(resourceFunction.transfers.size() == 1);
  ZC_EXPECT(resourceFunction.facts[0].subject.origin.place.local() == function.locals[0].id);
  ZC_EXPECT(resourceFunction.transfers[0].from.place.local() == function.locals[0].id);
  ZC_EXPECT(resourceFunction.transfers[0].to.place.local() == function.locals[1].id);
  ZC_EXPECT(resourceFunction.transfers[0].event.location.point.kind() ==
            MirPointKind::BeforeStatement);
  ZC_EXPECT(resourceFunction.transfers[0].event.operandOrdinal == 0);
  ZC_EXPECT(session.getIrFailureGroups().size() == 0);
  ZC_EXPECT(session.getIrIdentityInvariantFailures().size() == 0);
}

ZC_TEST("Resource verifier rejects a missing sequential aggregate transfer") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Copy};\n"
      "struct Cell { value: i32, }\n"
      "impl !Copy for Cell;\n"
      "fun entry() -> Cell { let first = Cell { value: 0 }; let second = first; return second; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();
  const auto& inputs = ownershipInputs(session);

  auto candidateResult =
      facts::OwnershipResourceBuilder::build(inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].transfers.size() == 1);
  candidate.functions[0].transfers.removeLast();

  auto verifiedResult = facts::OwnershipResourceVerifier::verify(
      zc::mv(candidate), inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Ownership resources retain a moved parameter subject") {
  OwnershipPipelineFixture fixture(
      "struct Cell { value: i32, }\n"
      "import core::marker::{Copy};\n"
      "impl !Copy for Cell;\n"
      "fun entry(value: Cell) -> Cell { let cell = value; return cell; }"_zc);
  const auto& builtMir = fixture.builtMir();
  const auto& inputs = ownershipInputs(fixture.compilerSession());
  ZC_REQUIRE(builtMir.functions().size() == 1);
  const auto& function = builtMir.functions()[0];
  ZC_REQUIRE(function.locals.size() == 2);
  ZC_REQUIRE(function.blocks.size() == 1);
  ZC_REQUIRE(function.blocks[0].statements.size() == 2);
  const auto& assignment = function.blocks[0].statements[1].assignmentValue();
  ZC_EXPECT(assignment.value.useValue().operand.kind() == mir::MirOperandKind::Move);
  ZC_EXPECT(assignment.value.useValue().operand.place().local() == function.locals[0].id);
  ZC_EXPECT(assignment.destination.local() == function.locals[1].id);

  const auto& resources = inputs.resources();
  ZC_REQUIRE(resources.functions().size() == 1);
  const auto& resourceFunction = resources.functions()[0];
  ZC_REQUIRE(resourceFunction.facts.size() == 1);
  const auto& resource = resourceFunction.facts[0];
  ZC_EXPECT(resource.subject.introduction.location.point.kind() == MirPointKind::Entry);
  ZC_EXPECT(resource.subject.introduction.operandOrdinal == 0);
  ZC_EXPECT(resource.subject.origin.place.local() == function.locals[0].id);
  ZC_EXPECT(resource.subject.originType == function.locals[0].type);
  ZC_EXPECT(resource.requirement == facts::DropRequirement::Logical);
  ZC_REQUIRE(resourceFunction.transfers.size() == 1);
  const auto& transfer = resourceFunction.transfers[0];
  ZC_EXPECT(transfer.from.place.local() == function.locals[0].id);
  ZC_EXPECT(transfer.to.place.local() == function.locals[1].id);
  ZC_EXPECT(transfer.event.location.point.kind() == MirPointKind::BeforeStatement);
  ZC_EXPECT(transfer.event.operandOrdinal == 0);
}

ZC_TEST("Ownership resources transfer and verify a moved direct-call result") {
  OwnershipPipelineFixture fixture(
      "struct Cell { value: i32, }\n"
      "import core::marker::{Copy};\n"
      "impl !Copy for Cell;\n"
      "fun helper() -> Cell { let cell = Cell { value: 0 }; return cell; }\n"
      "fun entry() -> Cell { return helper(); }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();
  const auto& inputs = ownershipInputs(session);
  const auto& resources = inputs.resources();

  bool foundTransfer = false;
  for (const auto& resourceFunction : resources.functions()) {
    if (resourceFunction.transfers.size() == 0) continue;
    ZC_REQUIRE(resourceFunction.facts.size() == 1);
    const auto& transfer = resourceFunction.transfers[0];
    ZC_EXPECT(resourceFunction.facts[0].subject.origin.owner == transfer.from.owner);
    ZC_EXPECT(resourceFunction.facts[0].subject.origin.place.local() ==
              transfer.from.place.local());
    ZC_EXPECT(transfer.from.place.local() != transfer.to.place.local());
    foundTransfer = true;
  }
  ZC_EXPECT(foundTransfer);

  auto missingCandidateResult =
      facts::OwnershipResourceBuilder::build(inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(missingCandidateResult.isVerified());
  auto missingCandidate = zc::mv(missingCandidateResult).takeVerified();
  bool removed = false;
  for (auto& function : missingCandidate.functions) {
    if (function.transfers.size() == 0) continue;
    function.transfers.removeLast();
    removed = true;
    break;
  }
  ZC_REQUIRE(removed);

  auto missingVerifiedResult = facts::OwnershipResourceVerifier::verify(
      zc::mv(missingCandidate), inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(missingVerifiedResult.isIrInvariantRejected());
  ZC_EXPECT(missingVerifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(missingVerifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);

  auto tamperedCandidateResult =
      facts::OwnershipResourceBuilder::build(inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(tamperedCandidateResult.isVerified());
  auto tamperedCandidate = zc::mv(tamperedCandidateResult).takeVerified();
  bool tampered = false;
  for (auto& function : tamperedCandidate.functions) {
    if (function.transfers.size() == 0) continue;
    ++function.transfers[0].event.operandOrdinal;
    tampered = true;
    break;
  }
  ZC_REQUIRE(tampered);

  auto tamperedVerifiedResult = facts::OwnershipResourceVerifier::verify(
      zc::mv(tamperedCandidate), inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(tamperedVerifiedResult.isIrInvariantRejected());
  ZC_EXPECT(tamperedVerifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(tamperedVerifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Ownership resources record exact drop transfer paths for a call result") {
  OwnershipPipelineFixture fixture(
      "struct Cell { value: i32, }\n"
      "import core::marker::{Copy};\n"
      "impl !Copy for Cell;\n"
      "fun helper() -> Cell { let cell = Cell { value: 0 }; return cell; }\n"
      "fun entry() -> Cell { return helper(); }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& inputs = ownershipInputs(session);
  ZC_REQUIRE(builtMir.functions().size() == 2);
  const auto& resources = inputs.resources();
  ZC_REQUIRE(resources.functions().size() == 2);

  const facts::OwnershipResourceFunction* entry = nullptr;
  for (const auto& function : resources.functions()) {
    if (function.transfers.size() != 0) entry = &function;
  }
  ZC_REQUIRE(entry != nullptr);
  ZC_REQUIRE(entry->transfers.size() == 1);
  const auto& transfer = entry->transfers[0];
  ZC_EXPECT(transfer.from.owner == transfer.to.owner);
  ZC_EXPECT(transfer.from.place.local() != transfer.to.place.local());
  ZC_EXPECT(transfer.from.place.projections().size() == 0);
  ZC_EXPECT(transfer.to.place.projections().size() == 0);
  ZC_EXPECT(transfer.event.location.point.kind() == MirPointKind::BeforeStatement);
  ZC_EXPECT(transfer.event.operandOrdinal == 0);
  ZC_EXPECT(session.getIrFailureGroups().size() == 0);
}

ZC_TEST("Resource verifier rejects a tampered parameter move transfer") {
  OwnershipPipelineFixture fixture(
      "struct Cell { value: i32, }\n"
      "import core::marker::{Copy};\n"
      "impl !Copy for Cell;\n"
      "fun entry(value: Cell) -> Cell { let cell = value; return cell; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();
  const auto& inputs = ownershipInputs(session);

  auto candidateResult =
      facts::OwnershipResourceBuilder::build(inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].transfers.size() == 1);
  ++candidate.functions[0].transfers[0].event.operandOrdinal;

  auto verifiedResult = facts::OwnershipResourceVerifier::verify(
      zc::mv(candidate), inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Resource verifier rejects a missing parameter move transfer") {
  OwnershipPipelineFixture fixture(
      "struct Cell { value: i32, }\n"
      "import core::marker::{Copy};\n"
      "impl !Copy for Cell;\n"
      "fun entry(value: Cell) -> Cell { let cell = value; return cell; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();
  const auto& inputs = ownershipInputs(session);

  auto candidateResult =
      facts::OwnershipResourceBuilder::build(inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].transfers.size() == 1);
  candidate.functions[0].transfers.removeLast();

  auto verifiedResult = facts::OwnershipResourceVerifier::verify(
      zc::mv(candidate), inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Ownership facts preserve a sibling aggregate field after an overwrite") {
  OwnershipPipelineFixture fixture(
      "struct Pair { mut left: i32, right: bool, }\n"
      "fun entry() -> bool { mut pair = Pair { left: 0, right: true }; pair.left = 2; return pair.right; }"_zc);
  const auto& session = fixture.compilerSession();
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  const auto& inputs = ownershipInputs(session);
  const auto& function = fixture.builtMir().functions()[0];
  ZC_REQUIRE(function.blocks.size() == 1);
  const auto& block = function.blocks[0];
  ZC_REQUIRE(block.statements.size() == 3);
  ZC_REQUIRE(block.terminator.returnValue().value != zc::none);
  ZC_IF_SOME(returnOperand, block.terminator.returnValue().value) {
    ZC_EXPECT(
        block.statements[2].assignmentValue().destination.projections()[0].fieldValue().field !=
        returnOperand.place().projections()[0].fieldValue().field);
    ZC_EXPECT(block.statements[2].assignmentValue().destination.resultType() !=
              returnOperand.place().resultType());
    ZC_EXPECT(returnOperand.kind() == mir::MirOperandKind::Copy);

    const auto& paths = inputs.movePaths();
    ZC_REQUIRE(paths.functions().size() == 1);
    const auto& pathFacts = paths.functions()[0].facts;
    ZC_REQUIRE(pathFacts.size() == 3);
    ZC_EXPECT(paths.conflicts(pathFacts[0].key, pathFacts[1].key));
    ZC_EXPECT(paths.conflicts(pathFacts[0].key, pathFacts[2].key));
    ZC_EXPECT(!paths.conflicts(pathFacts[1].key, pathFacts[2].key));

    const auto& initialization = inputs.initialization().functions()[0].facts;
    bool foundReturnedField = false;
    for (const auto& fact : initialization) {
      if (fact.point.kind() != MirPointKind::BeforeTerminator ||
          fact.key.place.projections().size() != 1 ||
          fact.key.place.projections()[0].fieldValue().field !=
              returnOperand.place().projections()[0].fieldValue().field) {
        continue;
      }
      ZC_EXPECT(fact.state == facts::InitializationState::initialized());
      ZC_EXPECT(fact.lossCauses.size() == 0);
      foundReturnedField = true;
    }
    ZC_EXPECT(foundReturnedField);
  }
}

ZC_TEST("Ownership facts preserve consecutive aggregate field overwrites") {
  OwnershipPipelineFixture fixture(
      "struct Pair { mut left: i32, mut right: bool, }\n"
      "fun entry() -> bool { mut pair = Pair { left: 0, right: true }; pair.left = 2; pair.right = false; return pair.right; }"_zc);
  const auto& session = fixture.compilerSession();
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  const auto& inputs = ownershipInputs(session);
  const auto& function = fixture.builtMir().functions()[0];
  ZC_REQUIRE(function.blocks.size() == 1);
  const auto& block = function.blocks[0];
  ZC_REQUIRE(block.statements.size() == 4);
  ZC_REQUIRE(block.terminator.returnValue().value != zc::none);
  ZC_IF_SOME(returnOperand, block.terminator.returnValue().value) {
    const auto firstField =
        block.statements[2].assignmentValue().destination.projections()[0].fieldValue().field;
    const auto secondField =
        block.statements[3].assignmentValue().destination.projections()[0].fieldValue().field;
    ZC_EXPECT(firstField != secondField);
    ZC_EXPECT(secondField == returnOperand.place().projections()[0].fieldValue().field);

    const auto& initialization = inputs.initialization().functions()[0].facts;
    bool foundReturnedField = false;
    for (const auto& fact : initialization) {
      if (fact.point.kind() != MirPointKind::BeforeTerminator ||
          fact.key.place.projections().size() != 1 ||
          fact.key.place.projections()[0].fieldValue().field != secondField) {
        continue;
      }
      ZC_EXPECT(fact.state == facts::InitializationState::initialized());
      ZC_EXPECT(fact.lossCauses.size() == 0);
      foundReturnedField = true;
    }
    ZC_EXPECT(foundReturnedField);
  }
}

ZC_TEST("Ownership facts initialize an uninitialized aggregate field") {
  OwnershipPipelineFixture fixture(
      "struct Cell { mut value: i32, }\n"
      "fun entry() -> i32 { mut cell: Cell; cell.value = 0; return cell.value; }"_zc);
  const auto& session = fixture.compilerSession();
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  const auto& inputs = ownershipInputs(session);
  const auto& function = fixture.builtMir().functions()[0];
  ZC_REQUIRE(function.blocks.size() == 1);
  const auto& block = function.blocks[0];
  ZC_REQUIRE(block.statements.size() == 2);
  ZC_EXPECT(block.statements[1].assignmentValue().initialization ==
            mir::MirInitializationKind::Initialize);
  ZC_REQUIRE(block.terminator.returnValue().value != zc::none);
  ZC_IF_SOME(returnOperand, block.terminator.returnValue().value) {
    ZC_EXPECT(
        block.statements[1].assignmentValue().destination.projections()[0].fieldValue().field ==
        returnOperand.place().projections()[0].fieldValue().field);

    const auto& initialization = inputs.initialization().functions()[0].facts;
    bool foundRoot = false;
    bool foundField = false;
    for (const auto& fact : initialization) {
      if (fact.point.kind() != MirPointKind::BeforeTerminator) continue;
      if (fact.key.place.projections().size() == 0) {
        ZC_EXPECT(fact.state == facts::InitializationState::uninitialized());
        ZC_REQUIRE(fact.lossCauses.size() == 1);
        ZC_EXPECT(fact.lossCauses[0].kind == facts::InitializationLossKind::NeverInitialized);
        foundRoot = true;
        continue;
      }
      if (fact.key.place.projections().size() != 1 ||
          fact.key.place.projections()[0].fieldValue().field !=
              returnOperand.place().projections()[0].fieldValue().field) {
        continue;
      }
      ZC_EXPECT(fact.state == facts::InitializationState::initialized());
      ZC_EXPECT(fact.lossCauses.size() == 0);
      foundField = true;
    }
    ZC_EXPECT(foundRoot);
    ZC_EXPECT(foundField);
  }
}

ZC_TEST("Ownership facts initialize distinct fields of an uninitialized aggregate") {
  OwnershipPipelineFixture fixture(
      "struct Pair { mut left: i32, mut right: bool, }\n"
      "fun entry() -> bool { mut pair: Pair; pair.left = 0; pair.right = true; return pair.right; }"_zc);
  const auto& session = fixture.compilerSession();
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  const auto& inputs = ownershipInputs(session);
  const auto& block = fixture.builtMir().functions()[0].blocks[0];
  ZC_REQUIRE(block.statements.size() == 3);
  ZC_EXPECT(block.statements[1].assignmentValue().initialization ==
            mir::MirInitializationKind::Initialize);
  ZC_EXPECT(block.statements[2].assignmentValue().initialization ==
            mir::MirInitializationKind::Initialize);
  const auto firstField =
      block.statements[1].assignmentValue().destination.projections()[0].fieldValue().field;
  const auto secondField =
      block.statements[2].assignmentValue().destination.projections()[0].fieldValue().field;
  ZC_EXPECT(firstField != secondField);

  const auto& movePaths = inputs.movePaths();
  ZC_REQUIRE(movePaths.functions().size() == 1);
  const auto& pathFacts = movePaths.functions()[0].facts;
  ZC_REQUIRE(pathFacts.size() == 3);
  ZC_REQUIRE(movePaths.functions()[0].conflicts.size() == 2);
  zc::Maybe<const facts::MovePathKey&> root;
  zc::Maybe<const facts::MovePathKey&> firstPath;
  zc::Maybe<const facts::MovePathKey&> secondPath;
  for (const auto& path : pathFacts) {
    if (path.key.place.projections().size() == 0) {
      root = path.key;
      continue;
    }
    const auto field = path.key.place.projections()[0].fieldValue().field;
    if (field == firstField) firstPath = path.key;
    if (field == secondField) secondPath = path.key;
  }
  ZC_REQUIRE(root != zc::none);
  ZC_REQUIRE(firstPath != zc::none);
  ZC_REQUIRE(secondPath != zc::none);
  ZC_EXPECT(movePaths.conflicts(ZC_REQUIRE_NONNULL(root), ZC_REQUIRE_NONNULL(firstPath)));
  ZC_EXPECT(movePaths.conflicts(ZC_REQUIRE_NONNULL(root), ZC_REQUIRE_NONNULL(secondPath)));
  ZC_EXPECT(!movePaths.conflicts(ZC_REQUIRE_NONNULL(firstPath), ZC_REQUIRE_NONNULL(secondPath)));

  const auto& initialization = inputs.initialization().functions()[0].facts;
  bool foundRoot = false;
  bool foundFirstField = false;
  bool foundSecondField = false;
  for (const auto& fact : initialization) {
    if (fact.point.kind() != MirPointKind::BeforeTerminator) { continue; }
    if (fact.key.place.projections().size() == 0) {
      ZC_EXPECT(fact.state == facts::InitializationState::uninitialized());
      ZC_REQUIRE(fact.lossCauses.size() == 1);
      ZC_EXPECT(fact.lossCauses[0].kind == facts::InitializationLossKind::NeverInitialized);
      foundRoot = true;
      continue;
    }
    if (fact.key.place.projections().size() != 1) continue;
    const auto field = fact.key.place.projections()[0].fieldValue().field;
    if (field == firstField) {
      ZC_EXPECT(fact.state == facts::InitializationState::initialized());
      foundFirstField = true;
    }
    if (field == secondField) {
      ZC_EXPECT(fact.state == facts::InitializationState::initialized());
      foundSecondField = true;
    }
  }
  ZC_EXPECT(foundRoot);
  ZC_EXPECT(foundFirstField);
  ZC_EXPECT(foundSecondField);
}

ZC_TEST("Ownership facts overwrite an initialized field of an uninitialized aggregate") {
  OwnershipPipelineFixture fixture(
      "struct Pair { mut left: i32, mut right: bool, }\n"
      "fun entry() -> i32 { mut pair: Pair; pair.left = 0; pair.right = true; pair.left = 2; return pair.left; }"_zc);
  const auto& session = fixture.compilerSession();
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  const auto& inputs = ownershipInputs(session);
  const auto& block = fixture.builtMir().functions()[0].blocks[0];
  ZC_REQUIRE(block.statements.size() == 4);
  ZC_EXPECT(block.statements[1].assignmentValue().initialization ==
            mir::MirInitializationKind::Initialize);
  ZC_EXPECT(block.statements[2].assignmentValue().initialization ==
            mir::MirInitializationKind::Initialize);
  ZC_EXPECT(block.statements[3].assignmentValue().initialization ==
            mir::MirInitializationKind::Overwrite);
  const auto firstField =
      block.statements[1].assignmentValue().destination.projections()[0].fieldValue().field;
  const auto secondField =
      block.statements[2].assignmentValue().destination.projections()[0].fieldValue().field;
  ZC_EXPECT(firstField != secondField);
  ZC_EXPECT(block.statements[3].assignmentValue().destination.projections()[0].fieldValue().field ==
            firstField);

  const auto& initialization = inputs.initialization().functions()[0].facts;
  bool foundFirstField = false;
  bool foundSecondField = false;
  for (const auto& fact : initialization) {
    if (fact.point.kind() != MirPointKind::BeforeTerminator ||
        fact.key.place.projections().size() != 1) {
      continue;
    }
    const auto field = fact.key.place.projections()[0].fieldValue().field;
    if (field == firstField) {
      ZC_EXPECT(fact.state == facts::InitializationState::initialized());
      foundFirstField = true;
    }
    if (field == secondField) {
      ZC_EXPECT(fact.state == facts::InitializationState::initialized());
      foundSecondField = true;
    }
  }
  ZC_EXPECT(foundFirstField);
  ZC_EXPECT(foundSecondField);
}

ZC_TEST("Initialization verifier rejects a tampered partially initialized field state") {
  OwnershipPipelineFixture fixture(
      "struct Cell { mut value: i32, }\n"
      "fun entry() -> i32 { mut cell: Cell; cell.value = 0; return cell.value; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  const auto& inputs = ownershipInputs(session);
  const auto& block = builtMir.functions()[0].blocks[0];
  ZC_REQUIRE(block.terminator.returnValue().value != zc::none);
  ZC_IF_SOME(returnOperand, block.terminator.returnValue().value) {
    auto candidateResult = facts::InitializationBuilder::build(
        builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay(), inputs.flow(),
        inputs.movePaths());
    ZC_REQUIRE(candidateResult.isVerified());
    auto candidate = zc::mv(candidateResult).takeVerified();
    bool tampered = false;
    for (auto& fact : candidate.functions[0].facts) {
      if (fact.point.kind() != MirPointKind::BeforeTerminator ||
          fact.key.place.projections().size() != 1 ||
          fact.key.place.projections()[0].fieldValue().field !=
              returnOperand.place().projections()[0].fieldValue().field) {
        continue;
      }
      fact.state = facts::InitializationState::uninitialized();
      tampered = true;
    }
    ZC_REQUIRE(tampered);

    auto verifiedResult = facts::InitializationVerifier::verify(
        zc::mv(candidate), builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay(),
        inputs.flow(), inputs.movePaths());
    ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
    ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
    ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
              ir::IrFailureKind::InvalidOwnershipProof);
  }
}

ZC_TEST("Initialization verifier rejects a tampered sibling field state") {
  OwnershipPipelineFixture fixture(
      "struct Pair { mut left: i32, right: bool, }\n"
      "fun entry() -> bool { mut pair = Pair { left: 0, right: true }; pair.left = 2; return pair.right; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  const auto& inputs = ownershipInputs(session);
  const auto& block = builtMir.functions()[0].blocks[0];
  ZC_REQUIRE(block.terminator.returnValue().value != zc::none);
  ZC_IF_SOME(returnOperand, block.terminator.returnValue().value) {
    auto candidateResult = facts::InitializationBuilder::build(
        builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay(), inputs.flow(),
        inputs.movePaths());
    ZC_REQUIRE(candidateResult.isVerified());
    auto candidate = zc::mv(candidateResult).takeVerified();
    bool tampered = false;
    for (auto& fact : candidate.functions[0].facts) {
      if (fact.point.kind() != MirPointKind::BeforeTerminator ||
          fact.key.place.projections().size() != 1 ||
          fact.key.place.projections()[0].fieldValue().field !=
              returnOperand.place().projections()[0].fieldValue().field) {
        continue;
      }
      fact.state = facts::InitializationState::uninitialized();
      tampered = true;
    }
    ZC_REQUIRE(tampered);

    auto verifiedResult = facts::InitializationVerifier::verify(
        zc::mv(candidate), builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay(),
        inputs.flow(), inputs.movePaths());
    ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
    ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
    ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
              ir::IrFailureKind::InvalidOwnershipProof);
  }
}

ZC_TEST("Ownership facts record the causal path in a root move loss cause") {
  OwnershipPipelineFixture fixture(
      "struct Cell { value: i32, }\n"
      "import core::marker::{Copy};\n"
      "impl !Copy for Cell;\n"
      "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc);
  const auto& session = fixture.compilerSession();
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  const auto& inputs = ownershipInputs(session);
  const auto& initialization = inputs.initialization().functions()[0].facts;
  ZC_REQUIRE(initialization.size() == 7);
  ZC_EXPECT(initialization[6].state == facts::InitializationState::uninitialized());
  ZC_REQUIRE(initialization[6].lossCauses.size() == 1);
  ZC_EXPECT(initialization[6].lossCauses[0].kind == facts::InitializationLossKind::Moved);
  // A root move records the root itself as the causal path, with no projections.
  ZC_EXPECT(initialization[6].lossCauses[0].path.place.projections().size() == 0);
  ZC_EXPECT(initialization[6].lossCauses[0].path.place.local() ==
            initialization[6].key.place.local());
}

ZC_TEST("Ownership facts record per-path causal paths for partial initialization") {
  OwnershipPipelineFixture fixture(
      "struct Pair { mut left: i32, mut right: bool, }\n"
      "fun entry() -> bool { mut pair: Pair; pair.left = 0; pair.right = true; return pair.right; }"_zc);
  const auto& session = fixture.compilerSession();
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  const auto& inputs = ownershipInputs(session);
  const auto& initialization = inputs.initialization().functions()[0].facts;
  // At function entry, every dead path carries its own NeverInitialized causal path, so a
  // partially initialized aggregate keeps per-path loss causes distinct before StorageLive
  // replaces them with the storage root.
  bool foundRoot = false;
  bool foundField = false;
  for (const auto& fact : initialization) {
    if (fact.point.kind() != MirPointKind::Entry) continue;
    if (fact.key.place.projections().size() == 0) {
      ZC_EXPECT(fact.state == facts::InitializationState::dead());
      ZC_REQUIRE(fact.lossCauses.size() == 1);
      ZC_EXPECT(fact.lossCauses[0].kind == facts::InitializationLossKind::NeverInitialized);
      ZC_EXPECT(fact.lossCauses[0].path.place.projections().size() == 0);
      ZC_EXPECT(fact.lossCauses[0].path.place.local() == fact.key.place.local());
      foundRoot = true;
    } else {
      ZC_EXPECT(fact.state == facts::InitializationState::dead());
      ZC_REQUIRE(fact.lossCauses.size() == 1);
      ZC_EXPECT(fact.lossCauses[0].kind == facts::InitializationLossKind::NeverInitialized);
      ZC_EXPECT(fact.lossCauses[0].path.place.projections().size() == 1);
      ZC_EXPECT(fact.lossCauses[0].path.place.local() == fact.key.place.local());
      foundField = true;
    }
  }
  ZC_EXPECT(foundRoot);
  ZC_EXPECT(foundField);
}

ZC_TEST("Initialization verifier rejects a tampered loss cause causal path") {
  OwnershipPipelineFixture fixture(
      "struct Pair { mut left: i32, mut right: bool, }\n"
      "fun entry() -> bool { mut pair: Pair; pair.left = 0; pair.right = true; return pair.right; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  const auto& inputs = ownershipInputs(session);

  auto candidateResult = facts::InitializationBuilder::build(
      builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay(), inputs.flow(),
      inputs.movePaths());
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();

  // Find the uninitialized field's causal path at the after-first-write point.
  zc::Maybe<facts::MovePathKey> fieldPath;
  for (const auto& fact : candidate.functions[0].facts) {
    if (fact.point.kind() != MirPointKind::AfterStatement ||
        fact.point.afterStatementValue().ordinal != 0 || fact.key.place.projections().size() != 1 ||
        fact.lossCauses.size() != 1 ||
        fact.lossCauses[0].kind != facts::InitializationLossKind::NeverInitialized) {
      continue;
    }
    fieldPath = facts::MovePathKey{fact.key.owner, fact.key.place.clone()};
  }
  ZC_REQUIRE(fieldPath != zc::none);

  bool tampered = false;
  for (auto& fact : candidate.functions[0].facts) {
    if (fact.point.kind() != MirPointKind::AfterStatement ||
        fact.point.afterStatementValue().ordinal != 0 || fact.key.place.projections().size() != 0 ||
        fact.lossCauses.size() != 1 ||
        fact.lossCauses[0].kind != facts::InitializationLossKind::NeverInitialized) {
      continue;
    }
    // Point the root's causal path at the uninitialized field instead of the root itself.
    fact.lossCauses[0].path = zc::mv(ZC_ASSERT_NONNULL(fieldPath));
    tampered = true;
  }
  ZC_REQUIRE(tampered);

  auto verifiedResult = facts::InitializationVerifier::verify(
      zc::mv(candidate), builtMir, session.getOwnershipCheckedMirModules()[0].eventOverlay(),
      inputs.flow(), inputs.movePaths());
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("CompilerSession publishes a mutable local overwrite through ownership facts") {
  OwnershipPipelineFixture fixture(
      "fun entry() -> i32 { mut value = 0; value = 1; return value; }"_zc);
  const auto& session = fixture.compilerSession();
  ZC_REQUIRE(session.getVerifiedHirModules().size() == 1);
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  const auto& inputs = ownershipInputs(session);

  const auto& hir = session.getVerifiedHirModules()[0];
  ZC_REQUIRE(hir.locals().size() == 1);
  ZC_REQUIRE(hir.localWrites().size() == 1);
  ZC_REQUIRE(hir.localReferences().size() == 1);
  ZC_EXPECT(hir.localWrites()[0].kind == hir::HirLocalWriteKind::Overwrite);
  ZC_EXPECT(hir.localWrites()[0].local == hir.locals()[0].local);
  ZC_EXPECT(hir.localWrites()[0].local == hir.localReferences()[0].local);

  const auto& function = session.getOwnershipCheckedMirModules()[0].builtMir().functions()[0];
  ZC_REQUIRE(function.blocks.size() == 1);
  ZC_REQUIRE(function.blocks[0].statements.size() == 3);
  ZC_EXPECT(function.blocks[0].statements[0].kind() == mir::MirStatementKind::StorageLive);
  ZC_EXPECT(function.blocks[0].statements[1].kind() == mir::MirStatementKind::Assign);
  ZC_EXPECT(function.blocks[0].statements[2].kind() == mir::MirStatementKind::Assign);
  ZC_EXPECT(function.blocks[0].statements[1].assignmentValue().initialization ==
            mir::MirInitializationKind::Initialize);
  ZC_EXPECT(function.blocks[0].statements[2].assignmentValue().initialization ==
            mir::MirInitializationKind::Overwrite);

  const auto& initialization = inputs.initialization().functions()[0].facts;
  bool foundInitializedAfterOverwrite = false;
  for (const auto& fact : initialization) {
    if (fact.point.kind() != MirPointKind::AfterStatement ||
        fact.point.afterStatementValue().ordinal != 2) {
      continue;
    }
    ZC_EXPECT(fact.state == facts::InitializationState::initialized());
    ZC_EXPECT(fact.lossCauses.size() == 0);
    foundInitializedAfterOverwrite = true;
  }
  ZC_EXPECT(foundInitializedAfterOverwrite);
  ZC_EXPECT(session.getIrFailureGroups().size() == 0);
  ZC_EXPECT(session.getIrIdentityInvariantFailures().size() == 0);
}

ZC_TEST("CompilerSession initializes a mutable annotated local through ownership facts") {
  OwnershipPipelineFixture fixture(
      "fun entry() -> i32 { mut value: i32; value = 1; return value; }"_zc);
  const auto& session = fixture.compilerSession();
  ZC_REQUIRE(session.getVerifiedHirModules().size() == 1);
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  const auto& inputs = ownershipInputs(session);

  const auto& hir = session.getVerifiedHirModules()[0];
  ZC_REQUIRE(hir.locals().size() == 1);
  ZC_REQUIRE(hir.localWrites().size() == 1);
  ZC_EXPECT(hir.locals()[0].initializer == zc::none);
  ZC_EXPECT(hir.localWrites()[0].kind == hir::HirLocalWriteKind::Initialize);
  ZC_EXPECT(hir.localWrites()[0].local == hir.locals()[0].local);

  const auto& function = session.getOwnershipCheckedMirModules()[0].builtMir().functions()[0];
  ZC_REQUIRE(function.blocks.size() == 1);
  ZC_REQUIRE(function.blocks[0].statements.size() == 2);
  ZC_EXPECT(function.blocks[0].statements[0].kind() == mir::MirStatementKind::StorageLive);
  ZC_EXPECT(function.blocks[0].statements[1].kind() == mir::MirStatementKind::Assign);
  ZC_EXPECT(function.blocks[0].statements[1].assignmentValue().initialization ==
            mir::MirInitializationKind::Initialize);

  const auto& initialization = inputs.initialization().functions()[0].facts;
  bool foundInitializedAfterWrite = false;
  for (const auto& fact : initialization) {
    if (fact.point.kind() != MirPointKind::AfterStatement ||
        fact.point.afterStatementValue().ordinal != 1) {
      continue;
    }
    ZC_EXPECT(fact.state == facts::InitializationState::initialized());
    ZC_EXPECT(fact.lossCauses.size() == 0);
    foundInitializedAfterWrite = true;
  }
  ZC_EXPECT(foundInitializedAfterWrite);
  ZC_EXPECT(session.getIrFailureGroups().size() == 0);
  ZC_EXPECT(session.getIrIdentityInvariantFailures().size() == 0);
}

ZC_TEST("CompilerSession preserves consecutive mutable local writes through ownership facts") {
  OwnershipPipelineFixture fixture(
      "fun entry() -> i32 { mut value = 0; value = 1; value = 2; return value; }"_zc);
  const auto& session = fixture.compilerSession();
  ZC_REQUIRE(session.getVerifiedHirModules().size() == 1);
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  const auto& inputs = ownershipInputs(session);

  const auto& hir = session.getVerifiedHirModules()[0];
  ZC_REQUIRE(hir.locals().size() == 1);
  ZC_REQUIRE(hir.localWrites().size() == 2);
  ZC_EXPECT(hir.localWrites()[0].kind == hir::HirLocalWriteKind::Overwrite);
  ZC_EXPECT(hir.localWrites()[1].kind == hir::HirLocalWriteKind::Overwrite);
  ZC_EXPECT(hir.localWrites()[0].local == hir.locals()[0].local);
  ZC_EXPECT(hir.localWrites()[1].local == hir.locals()[0].local);

  const auto& function = session.getOwnershipCheckedMirModules()[0].builtMir().functions()[0];
  ZC_REQUIRE(function.blocks.size() == 1);
  ZC_REQUIRE(function.blocks[0].statements.size() == 4);
  ZC_EXPECT(function.blocks[0].statements[0].kind() == mir::MirStatementKind::StorageLive);
  for (size_t index = 1; index < function.blocks[0].statements.size(); ++index) {
    ZC_EXPECT(function.blocks[0].statements[index].kind() == mir::MirStatementKind::Assign);
  }
  ZC_EXPECT(function.blocks[0].statements[1].assignmentValue().initialization ==
            mir::MirInitializationKind::Initialize);
  ZC_EXPECT(function.blocks[0].statements[2].assignmentValue().initialization ==
            mir::MirInitializationKind::Overwrite);
  ZC_EXPECT(function.blocks[0].statements[3].assignmentValue().initialization ==
            mir::MirInitializationKind::Overwrite);

  const auto& initialization = inputs.initialization().functions()[0].facts;
  bool foundInitializedAfterFinalWrite = false;
  for (const auto& fact : initialization) {
    if (fact.point.kind() != MirPointKind::AfterStatement ||
        fact.point.afterStatementValue().ordinal != 3) {
      continue;
    }
    ZC_EXPECT(fact.state == facts::InitializationState::initialized());
    ZC_EXPECT(fact.lossCauses.size() == 0);
    foundInitializedAfterFinalWrite = true;
  }
  ZC_EXPECT(foundInitializedAfterFinalWrite);
  ZC_EXPECT(session.getIrFailureGroups().size() == 0);
  ZC_EXPECT(session.getIrIdentityInvariantFailures().size() == 0);
}

ZC_TEST("CompilerSession initializes then overwrites an annotated local through ownership facts") {
  OwnershipPipelineFixture fixture(
      "fun entry() -> i32 { mut value: i32; value = 1; value = 2; return value; }"_zc);
  const auto& session = fixture.compilerSession();
  ZC_REQUIRE(session.getVerifiedHirModules().size() == 1);
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  const auto& inputs = ownershipInputs(session);

  const auto& hir = session.getVerifiedHirModules()[0];
  ZC_REQUIRE(hir.locals().size() == 1);
  ZC_REQUIRE(hir.localWrites().size() == 2);
  ZC_EXPECT(hir.localWrites()[0].kind == hir::HirLocalWriteKind::Initialize);
  ZC_EXPECT(hir.localWrites()[1].kind == hir::HirLocalWriteKind::Overwrite);

  const auto& function = session.getOwnershipCheckedMirModules()[0].builtMir().functions()[0];
  ZC_REQUIRE(function.blocks.size() == 1);
  ZC_REQUIRE(function.blocks[0].statements.size() == 3);
  ZC_EXPECT(function.blocks[0].statements[0].kind() == mir::MirStatementKind::StorageLive);
  ZC_EXPECT(function.blocks[0].statements[1].assignmentValue().initialization ==
            mir::MirInitializationKind::Initialize);
  ZC_EXPECT(function.blocks[0].statements[2].assignmentValue().initialization ==
            mir::MirInitializationKind::Overwrite);

  const auto& initialization = inputs.initialization().functions()[0].facts;
  bool foundInitializedAfterOverwrite = false;
  for (const auto& fact : initialization) {
    if (fact.point.kind() != MirPointKind::AfterStatement ||
        fact.point.afterStatementValue().ordinal != 2) {
      continue;
    }
    ZC_EXPECT(fact.state == facts::InitializationState::initialized());
    ZC_EXPECT(fact.lossCauses.size() == 0);
    foundInitializedAfterOverwrite = true;
  }
  ZC_EXPECT(foundInitializedAfterOverwrite);
  ZC_EXPECT(session.getIrFailureGroups().size() == 0);
  ZC_EXPECT(session.getIrIdentityInvariantFailures().size() == 0);
}

ZC_TEST("CompilerSession retains following functions after consecutive local writes") {
  OwnershipPipelineFixture fixture(
      "fun helper() -> i32 { mut value = 0; value = 1; value = 2; return value; }\n"
      "fun entry() -> i32 { return 3; }"_zc);
  const auto& session = fixture.compilerSession();
  ZC_REQUIRE(session.getVerifiedHirModules().size() == 1);
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  const auto& hir = session.getVerifiedHirModules()[0];
  const auto& mir = session.getOwnershipCheckedMirModules()[0].builtMir();
  ZC_REQUIRE(hir.functions().size() == 2);
  ZC_REQUIRE(hir.localWrites().size() == 2);
  ZC_REQUIRE(hir.returns().size() == 2);
  ZC_EXPECT(hir.functions()[0].node.ordinal() < hir.functions()[1].node.ordinal());
  ZC_EXPECT(hir.returns()[0].node.ordinal() < hir.functions()[1].node.ordinal());
  ZC_REQUIRE(mir.functions().size() == 2);
  bool foundHelper = false;
  bool foundEntry = false;
  for (const auto& function : mir.functions()) {
    if (function.owner == hir.functions()[0].definition) foundHelper = true;
    if (function.owner == hir.functions()[1].definition) foundEntry = true;
  }
  ZC_EXPECT(foundHelper);
  ZC_EXPECT(foundEntry);
  ZC_EXPECT(session.getIrFailureGroups().size() == 0);
  ZC_EXPECT(session.getIrIdentityInvariantFailures().size() == 0);
}

ZC_TEST("Ownership facts retain a parameter initialized function local") {
  OwnershipPipelineFixture fixture(
      "fun entry(value: i32) -> i32 { let copy = value; return copy; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& hir = session.getVerifiedHirModules()[0];
  const auto& builtMir = session.getOwnershipCheckedMirModules()[0].builtMir();
  ZC_REQUIRE(hir.functions().size() == 1);
  ZC_REQUIRE(hir.parameterReferences().size() == 1);
  ZC_REQUIRE(hir.locals().size() == 1);
  ZC_REQUIRE(builtMir.functions().size() == 1);
  const auto& function = builtMir.functions()[0];
  ZC_REQUIRE(function.locals.size() == 2);
  ZC_EXPECT(function.locals[0].kind == mir::MirLocalKind::Parameter);
  ZC_EXPECT(function.locals[1].kind == mir::MirLocalKind::UserLocal);
  ZC_REQUIRE(function.blocks.size() == 1);
  ZC_REQUIRE(function.blocks[0].statements.size() == 2);
  const auto& assignment = function.blocks[0].statements[1].assignmentValue();
  ZC_EXPECT(assignment.value.useValue().operand.kind() == mir::MirOperandKind::Copy);
  ZC_EXPECT(assignment.value.useValue().operand.place().local() == function.locals[0].id);
  ZC_EXPECT(assignment.destination.local() == function.locals[1].id);
  const auto& inputs = ownershipInputs(session);
  ZC_REQUIRE(inputs.movePaths().functions()[0].facts.size() == 2);
  ZC_REQUIRE(inputs.initialization().functions().size() == 1);
  ZC_EXPECT(session.getIrFailureGroups().size() == 0);
  ZC_EXPECT(session.getIrIdentityInvariantFailures().size() == 0);
}

ZC_TEST("Ownership event overlay records mutable receiver activation on normal call edge") {
  OwnershipPipelineFixture fixture(
      "struct Cell { value: i32, mutating fun read(this, amount: i32) -> i32; }\n"
      "fun entry() -> i32 { mut cell = Cell { value: 0 }; return cell.read(1); }"_zc);
  const auto& session = fixture.compilerSession();
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  const auto& function = session.getOwnershipCheckedMirModules()[0].builtMir().functions()[0];
  ZC_REQUIRE(function.locals.size() == 3);
  ZC_REQUIRE(function.blocks.size() == 2);
  const auto& call = function.blocks[0].terminator.callValue();
  ZC_EXPECT(call.effect.kind() == mir::MirCallEffectKind::ActivateMutableReceiver);
  ZC_EXPECT(call.effect.activatedMutableReceiver() == function.locals[1].id);
  ZC_REQUIRE(call.arguments.size() == 2);
  ZC_EXPECT(call.arguments[1].kind() == mir::MirOperandKind::Constant);

  bool foundActivation = false;
  for (const auto& functionOverlay :
       session.getOwnershipCheckedMirModules()[0].eventOverlay().functions()) {
    if (functionOverlay.owner != function.owner) continue;
    for (const auto& slot : functionOverlay.slots) {
      if (slot.key.location.point.kind() != MirPointKind::Edge ||
          slot.key.location.point.edgeValue().from != function.blocks[0].id ||
          slot.key.location.point.edgeValue().edgeOrdinal != 0 ||
          slot.key.location.point.edgeValue().to != call.normalTarget ||
          slot.key.operandOrdinal != 1) {
        continue;
      }
      ZC_EXPECT(slot.stage == OwnershipEventStage::Commit);
      ZC_REQUIRE(slot.roles.size() == 1);
      ZC_EXPECT(slot.roles[0] == OwnershipEventRole::BorrowActivation);
      foundActivation = true;
    }
  }
  ZC_EXPECT(foundActivation);

  bool foundDeferredActivation = false;
  for (const auto& functionOverlay :
       session.getOwnershipCheckedMirModules()[0].eventOverlay().functions()) {
    if (functionOverlay.owner != function.owner) continue;
    ZC_REQUIRE(functionOverlay.deferredActivations.size() == 1);
    const auto& fact = functionOverlay.deferredActivations[0];
    const MirEventKey expectedIssue{
        MirLocation{function.owner, MirPoint::beforeStatement(function.blocks[0].id, 3)}, 1};
    const MirEventKey expectedReceiverSource{
        MirLocation{function.owner, MirPoint::beforeTerminator(function.blocks[0].id)}, 0};
    const MirEventKey expectedActivation{
        MirLocation{function.owner, MirPoint::edge(function.blocks[0].id, 0, call.normalTarget)},
        1};
    ZC_EXPECT(fact.loan.issue == expectedIssue);
    ZC_EXPECT(fact.receiverSource == expectedReceiverSource);
    ZC_EXPECT(fact.activation == expectedActivation);
    ZC_EXPECT(fact.receiverMode == checker::checked::ReceiverMode::Mutable);
    ZC_EXPECT(fact.adjustmentSource == function.locals[0].type);
    ZC_EXPECT(fact.adjustmentDestination == function.locals[1].type);
    ZC_REQUIRE(fact.adjustmentSteps.size() == 1);
    ZC_EXPECT(fact.adjustmentSteps[0] == checker::checked::ReceiverAdjustmentStep::BorrowMutable);
    foundDeferredActivation = true;
  }
  ZC_EXPECT(foundDeferredActivation);

  const auto& inputs = ownershipInputs(session);
  ZC_REQUIRE(inputs.loans().loans().size() == 1);
  ZC_EXPECT(
      inputs.loans().loans()[0].activeFrom ==
      facts::OwnershipPoint::afterEvent(MirEventKey{
          MirLocation{function.owner, MirPoint::edge(function.blocks[0].id, 0, call.normalTarget)},
          1}));
  ZC_EXPECT(inputs.references().definitions().size() == 0);

  auto candidateResult = facts::LoanBuilder::build(
      inputs.movePaths(), session.getOwnershipCheckedMirModules()[0].builtMir(),
      session.getOwnershipCheckedMirModules()[0].eventOverlay());
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.loans.size() == 1);
  candidate.loans[0].activeFrom = facts::OwnershipPoint::afterEvent(candidate.loans[0].issue);
  auto verification = facts::LoanVerifier::verify(
      zc::mv(candidate), inputs.movePaths(), session.getOwnershipCheckedMirModules()[0].builtMir(),
      session.getOwnershipCheckedMirModules()[0].eventOverlay());
  ZC_REQUIRE(verification.isIrInvariantRejected());
  ZC_EXPECT(verification.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verification.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Ownership event overlay verifier rejects a tampered deferred activation") {
  OwnershipPipelineFixture fixture(
      "struct Cell { value: i32, mutating fun read(this, amount: i32) -> i32; }\n"
      "fun entry() -> i32 { mut cell = Cell { value: 0 }; return cell.read(1); }"_zc);
  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  bool tampered = false;
  for (auto& function : candidate.functions) {
    if (function.deferredActivations.size() != 1) continue;
    function.deferredActivations[0].activation.operandOrdinal = 0;
    tampered = true;
  }
  ZC_REQUIRE(tampered);

  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::CanonicalCodecMismatch);
}

ZC_TEST("Ownership event overlay models direct call operation and result commit") {
  OwnershipPipelineFixture fixture(
      "fun helper() -> i32 { return 0; }\n"
      "fun entry() -> i32 { return helper(); }"_zc);
  const auto& session = fixture.compilerSession();
  ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  const auto& builtMir = session.getOwnershipCheckedMirModules()[0].builtMir();
  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();
  zc::Maybe<const mir::MirFunction&> caller;
  for (const auto& function : builtMir.functions()) {
    if (function.blocks.size() != 2) continue;
    ZC_REQUIRE(caller == zc::none);
    caller = function;
  }
  ZC_REQUIRE(caller != zc::none);
  ZC_IF_SOME(function, caller) {
    const auto& call = function.blocks[0].terminator.callValue();
    const auto temporary = call.destination.local();
    const auto result = function.locals[1].id;
    bool foundOperation = false;
    bool foundCommit = false;
    bool foundStorageDead = false;
    size_t callMarkerUses = 0;
    bool foundPositive = false;
    bool foundUnsatisfied = false;
    for (const auto& functionOverlay : overlay.functions()) {
      if (functionOverlay.owner != function.owner) continue;
      for (const auto& slot : functionOverlay.slots) {
        if (slot.key.location.point.kind() == MirPointKind::BeforeTerminator &&
            slot.key.location.point.beforeTerminatorValue().block == function.blocks[0].id) {
          ZC_EXPECT(slot.key.operandOrdinal == 0);
          ZC_EXPECT(slot.stage == OwnershipEventStage::Effect);
          ZC_REQUIRE(slot.roles.size() == 1);
          ZC_EXPECT(slot.roles[0] == OwnershipEventRole::Operation);
          foundOperation = true;
          continue;
        }
        if (slot.key.location.point.kind() == MirPointKind::BeforeStatement &&
            slot.key.location.point.beforeStatementValue().block == function.blocks[1].id &&
            slot.key.location.point.beforeStatementValue().ordinal == 2) {
          ZC_EXPECT(slot.key.operandOrdinal == 0);
          ZC_EXPECT(slot.stage == OwnershipEventStage::Effect);
          ZC_REQUIRE(slot.roles.size() == 2);
          ZC_EXPECT(slot.roles[0] == OwnershipEventRole::Operation);
          ZC_EXPECT(slot.roles[1] == OwnershipEventRole::StorageDead);
          foundStorageDead = true;
          continue;
        }
        if (slot.key.location.point.kind() != MirPointKind::Edge ||
            slot.key.location.point.edgeValue().from != function.blocks[0].id ||
            slot.key.location.point.edgeValue().edgeOrdinal != 0 ||
            slot.key.location.point.edgeValue().to != call.normalTarget) {
          continue;
        }
        ZC_EXPECT(slot.stage == OwnershipEventStage::Commit);
        ZC_REQUIRE(slot.roles.size() == 1);
        ZC_EXPECT(slot.roles[0] == OwnershipEventRole::DestinationWrite);
        foundCommit = true;
      }
      for (const auto& use : functionOverlay.markerUses) {
        if (use.key.event.location.point.kind() != MirPointKind::Edge ||
            use.key.event.location.point.edgeValue().from != function.blocks[0].id ||
            use.key.event.location.point.edgeValue().edgeOrdinal != 0 ||
            use.key.event.location.point.edgeValue().to != call.normalTarget) {
          continue;
        }
        ZC_EXPECT(use.key.event.operandOrdinal == 0);
        ZC_EXPECT(use.key.subject == call.destination.resultType());
        ++callMarkerUses;
        if (use.decision.is<OwnershipMarkerDecisionPositive>()) foundPositive = true;
        if (use.decision.is<OwnershipMarkerDecisionUnsatisfied>()) foundUnsatisfied = true;
      }
    }
    ZC_EXPECT(foundOperation);
    ZC_EXPECT(foundCommit);
    ZC_EXPECT(foundStorageDead);
    ZC_EXPECT(callMarkerUses == 2);
    ZC_EXPECT(foundPositive);
    ZC_EXPECT(foundUnsatisfied);
    zc::Maybe<const facts::InitializationFunction&> initialization;
    for (const auto& candidate : ownershipInputs(session).initialization().functions()) {
      if (candidate.owner == function.owner) initialization = candidate;
    }
    ZC_REQUIRE(initialization != zc::none);
    ZC_IF_SOME(value, initialization) {
      ZC_REQUIRE(value.facts.size() == 26);
      bool foundMovedTemporary = false;
      bool foundInitializedResult = false;
      bool foundDeadTemporary = false;
      bool foundReturnedResult = false;
      for (const auto& fact : value.facts) {
        if (fact.point.kind() == MirPointKind::AfterStatement &&
            fact.point.afterStatementValue().block == function.blocks[1].id &&
            fact.point.afterStatementValue().ordinal == 1 && fact.key.place.local() == temporary) {
          ZC_EXPECT(fact.state == facts::InitializationState::uninitialized());
          ZC_REQUIRE(fact.lossCauses.size() == 1);
          ZC_EXPECT(fact.lossCauses[0].kind == facts::InitializationLossKind::Moved);
          ZC_EXPECT(fact.lossCauses[0].event.location.point.kind() ==
                    MirPointKind::BeforeStatement);
          ZC_EXPECT(fact.lossCauses[0].event.location.point.beforeStatementValue().ordinal == 1);
          foundMovedTemporary = true;
        }
        if (fact.point.kind() == MirPointKind::AfterStatement &&
            fact.point.afterStatementValue().block == function.blocks[1].id &&
            fact.point.afterStatementValue().ordinal == 1 && fact.key.place.local() == result) {
          ZC_EXPECT(fact.state == facts::InitializationState::initialized());
          ZC_EXPECT(fact.lossCauses.size() == 0);
          foundInitializedResult = true;
        }
        if (fact.point.kind() == MirPointKind::AfterStatement &&
            fact.point.afterStatementValue().block == function.blocks[1].id &&
            fact.point.afterStatementValue().ordinal == 2 && fact.key.place.local() == temporary) {
          ZC_EXPECT(fact.state == facts::InitializationState::dead());
          ZC_REQUIRE(fact.lossCauses.size() == 1);
          ZC_EXPECT(fact.lossCauses[0].kind == facts::InitializationLossKind::StorageEnded);
          ZC_EXPECT(fact.lossCauses[0].event.location.point.kind() ==
                    MirPointKind::BeforeStatement);
          ZC_EXPECT(fact.lossCauses[0].event.location.point.beforeStatementValue().ordinal == 2);
          foundDeadTemporary = true;
        }
        if (fact.point.kind() == MirPointKind::Exit &&
            fact.point.exitValue().block == function.blocks[1].id &&
            fact.key.place.local() == result) {
          ZC_EXPECT(fact.state == facts::InitializationState::initialized());
          ZC_EXPECT(fact.lossCauses.size() == 0);
          foundReturnedResult = true;
        }
      }
      ZC_EXPECT(foundMovedTemporary);
      ZC_EXPECT(foundInitializedResult);
      ZC_EXPECT(foundDeadTemporary);
      ZC_EXPECT(foundReturnedResult);
    }
  }
}

ZC_TEST("Ownership event overlay models scalar direct call argument sources") {
  OwnershipPipelineFixture fixture(
      "fun helper(value: i32) -> i32 { return 0; }\n"
      "fun entry() -> i32 { return helper(7); }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto checkedMir = session.getOwnershipCheckedMirModules();
  ZC_REQUIRE(checkedMir.size() == 1);
  zc::Maybe<const mir::MirFunction&> caller;
  for (const auto& function : builtMir.functions()) {
    if (function.blocks.size() == 2 &&
        function.blocks[0].terminator.kind() == mir::MirTerminatorKind::Call) {
      caller = function;
    }
  }
  ZC_REQUIRE(caller != zc::none);
  ZC_IF_SOME(function, caller) {
    const auto& call = function.blocks[0].terminator.callValue();
    ZC_REQUIRE(call.arguments.size() == 1);
    ZC_EXPECT(call.arguments[0].kind() == mir::MirOperandKind::Constant);
    bool foundArgumentSource = false;
    bool foundCallEffect = false;
    for (const auto& functionOverlay : checkedMir[0].eventOverlay().functions()) {
      if (functionOverlay.owner != function.owner) continue;
      for (const auto& slot : functionOverlay.slots) {
        if (slot.key.location.point.kind() != MirPointKind::BeforeTerminator ||
            slot.key.location.point.beforeTerminatorValue().block != function.blocks[0].id) {
          continue;
        }
        if (slot.key.operandOrdinal == 0) {
          ZC_EXPECT(slot.stage == OwnershipEventStage::Source);
          ZC_REQUIRE(slot.roles.size() == 1);
          ZC_EXPECT(slot.roles[0] == OwnershipEventRole::ConstantOperand);
          foundArgumentSource = true;
        }
        if (slot.key.operandOrdinal == 1) {
          ZC_EXPECT(slot.stage == OwnershipEventStage::Effect);
          ZC_REQUIRE(slot.roles.size() == 1);
          ZC_EXPECT(slot.roles[0] == OwnershipEventRole::Operation);
          foundCallEffect = true;
        }
      }
    }
    ZC_EXPECT(foundArgumentSource);
    ZC_EXPECT(foundCallEffect);
  }
}

ZC_TEST("Ownership event overlay publishes source roles for copy, reborrow, and return") {
  OwnershipPipelineFixture copyFixture(
      "fun entry(value: i32) -> i32 { let copy = value; return copy; }"_zc);
  const auto& copyMir = copyFixture.builtMir();
  const auto& copyOverlay =
      copyFixture.compilerSession().getOwnershipCheckedMirModules()[0].eventOverlay();
  ZC_REQUIRE(copyMir.functions().size() == 1);
  const auto& copyFunction = copyMir.functions()[0];
  ZC_REQUIRE(copyFunction.blocks.size() == 1);
  ZC_REQUIRE(copyFunction.blocks[0].statements.size() == 2);

  bool foundAssignmentSource = false;
  bool foundReturnSource = false;
  for (const auto& functionOverlay : copyOverlay.functions()) {
    if (functionOverlay.owner != copyFunction.owner) continue;
    for (const auto& slot : functionOverlay.slots) {
      if (slot.key.location.point.kind() == MirPointKind::BeforeStatement &&
          slot.key.location.point.beforeStatementValue().block == copyFunction.blocks[0].id &&
          slot.key.location.point.beforeStatementValue().ordinal == 1 &&
          slot.key.operandOrdinal == 0) {
        ZC_EXPECT(slot.stage == OwnershipEventStage::Source);
        ZC_REQUIRE(slot.roles.size() == 2);
        ZC_EXPECT(slot.roles[0] == OwnershipEventRole::OperandRead);
        ZC_EXPECT(slot.roles[1] == OwnershipEventRole::OperandCopy);
        foundAssignmentSource = true;
      }
      if (slot.key.location.point.kind() == MirPointKind::BeforeTerminator &&
          slot.key.location.point.beforeTerminatorValue().block == copyFunction.blocks[0].id &&
          slot.key.operandOrdinal == 0) {
        ZC_EXPECT(slot.stage == OwnershipEventStage::Source);
        ZC_REQUIRE(slot.roles.size() == 2);
        ZC_EXPECT(slot.roles[0] == OwnershipEventRole::OperandRead);
        ZC_EXPECT(slot.roles[1] == OwnershipEventRole::OperandCopy);
        foundReturnSource = true;
      }
    }
  }
  ZC_EXPECT(foundAssignmentSource);
  ZC_EXPECT(foundReturnSource);

  OwnershipPipelineFixture borrowFixture(
      "fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  const auto& borrowMir = borrowFixture.builtMir();
  const auto& borrowOverlay =
      borrowFixture.compilerSession().getOwnershipCheckedMirModules()[0].eventOverlay();
  ZC_REQUIRE(borrowMir.functions().size() == 1);
  const auto& borrowFunction = borrowMir.functions()[0];

  zc::Maybe<mir::MirBlockId> borrowBlock;
  uint32_t borrowOrdinal = 0;
  for (const auto& block : borrowFunction.blocks) {
    for (uint32_t ordinal = 0; ordinal < block.statements.size(); ++ordinal) {
      if (block.statements[ordinal].kind() != mir::MirStatementKind::BorrowCreation) continue;
      ZC_REQUIRE(borrowBlock == zc::none);
      borrowBlock = block.id;
      borrowOrdinal = ordinal;
    }
  }
  ZC_REQUIRE(borrowBlock != zc::none);

  bool foundBorrowSource = false;
  bool foundBorrowEffect = false;
  bool foundBorrowCommit = false;
  ZC_IF_SOME(block, borrowBlock) {
    for (const auto& functionOverlay : borrowOverlay.functions()) {
      if (functionOverlay.owner != borrowFunction.owner) continue;
      for (const auto& slot : functionOverlay.slots) {
        if (slot.key.location.point.kind() != MirPointKind::BeforeStatement ||
            slot.key.location.point.beforeStatementValue().block != block ||
            slot.key.location.point.beforeStatementValue().ordinal != borrowOrdinal) {
          continue;
        }
        if (slot.key.operandOrdinal == 0) {
          ZC_EXPECT(slot.stage == OwnershipEventStage::Source);
          ZC_REQUIRE(slot.roles.size() == 1);
          ZC_EXPECT(slot.roles[0] == OwnershipEventRole::OperandRead);
          foundBorrowSource = true;
        }
        if (slot.key.operandOrdinal == 1) {
          ZC_EXPECT(slot.stage == OwnershipEventStage::Effect);
          ZC_REQUIRE(slot.roles.size() == 2);
          ZC_EXPECT(slot.roles[0] == OwnershipEventRole::Operation);
          ZC_EXPECT(slot.roles[1] == OwnershipEventRole::BorrowIssue);
          foundBorrowEffect = true;
        }
        if (slot.key.operandOrdinal == 2) {
          ZC_EXPECT(slot.stage == OwnershipEventStage::Commit);
          ZC_REQUIRE(slot.roles.size() == 1);
          ZC_EXPECT(slot.roles[0] == OwnershipEventRole::DestinationWrite);
          foundBorrowCommit = true;
        }
      }
    }
  }
  ZC_EXPECT(foundBorrowSource);
  ZC_EXPECT(foundBorrowEffect);
  ZC_EXPECT(foundBorrowCommit);
}

ZC_TEST("Ownership facts initialize a returned user local from a direct call") {
  OwnershipPipelineFixture fixture(
      "fun helper() -> i32 { return 0; }\n"
      "fun entry() -> i32 { let value = helper(); return value; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();
  zc::Maybe<const mir::MirFunction&> caller;
  for (const auto& function : builtMir.functions()) {
    if (function.locals.size() != 1 || function.locals[0].kind != mir::MirLocalKind::UserLocal) {
      continue;
    }
    ZC_REQUIRE(caller == zc::none);
    caller = function;
  }
  ZC_REQUIRE(caller != zc::none);
  ZC_IF_SOME(function, caller) {
    ZC_REQUIRE(function.blocks.size() == 2);
    const auto& entry = function.blocks[0];
    const auto& continuation = function.blocks[1];
    ZC_REQUIRE(entry.statements.size() == 1);
    ZC_EXPECT(entry.statements[0].kind() == mir::MirStatementKind::StorageLive);
    ZC_EXPECT(entry.terminator.kind() == mir::MirTerminatorKind::Call);
    ZC_EXPECT(continuation.statements.size() == 0);
    ZC_EXPECT(continuation.terminator.kind() == mir::MirTerminatorKind::Return);
    const auto& call = entry.terminator.callValue();
    const auto local = function.locals[0].id;
    ZC_EXPECT(call.destination.local() == local);
    ZC_EXPECT(call.normalTarget == continuation.id);
    ZC_REQUIRE(continuation.terminator.returnValue().value != zc::none);
    ZC_IF_SOME(value, continuation.terminator.returnValue().value) {
      ZC_EXPECT(value.kind() == mir::MirOperandKind::Copy);
      ZC_EXPECT(value.place().local() == local);
    }

    bool foundCommit = false;
    bool foundPlan = false;
    for (const auto& functionOverlay : overlay.functions()) {
      if (functionOverlay.owner != function.owner) continue;
      for (const auto& slot : functionOverlay.slots) {
        if (slot.key.location.point.kind() != MirPointKind::Edge ||
            slot.key.location.point.edgeValue().from != entry.id ||
            slot.key.location.point.edgeValue().to != continuation.id) {
          continue;
        }
        ZC_EXPECT(slot.stage == OwnershipEventStage::Commit);
        ZC_REQUIRE(slot.roles.size() == 1);
        ZC_EXPECT(slot.roles[0] == OwnershipEventRole::DestinationWrite);
        foundCommit = true;
      }
      ZC_REQUIRE(functionOverlay.logicalDropPlans.size() == 1);
      const auto& plan = functionOverlay.logicalDropPlans[0];
      ZC_EXPECT(plan.initialization.location.point.kind() == MirPointKind::Edge);
      ZC_EXPECT(plan.initialization.location.point.edgeValue().from == entry.id);
      ZC_EXPECT(plan.initialization.location.point.edgeValue().to == continuation.id);
      ZC_EXPECT(plan.initialization.operandOrdinal == 0);
      ZC_EXPECT(plan.root.local() == local);
      ZC_EXPECT(plan.root.resultType() == call.destination.resultType());
      foundPlan = true;
    }
    ZC_EXPECT(foundCommit);
    ZC_EXPECT(foundPlan);

    zc::Maybe<const facts::InitializationFunction&> initialization;
    for (const auto& candidate : ownershipInputs(session).initialization().functions()) {
      if (candidate.owner == function.owner) initialization = candidate;
    }
    ZC_REQUIRE(initialization != zc::none);
    ZC_IF_SOME(value, initialization) {
      bool foundEdgeInitialization = false;
      bool foundReturnCopy = false;
      for (const auto& fact : value.facts) {
        if (fact.key.place.local() != local) continue;
        if (fact.point.kind() == MirPointKind::Edge && fact.point.edgeValue().from == entry.id &&
            fact.point.edgeValue().to == continuation.id) {
          ZC_EXPECT(fact.state == facts::InitializationState::initialized());
          ZC_EXPECT(fact.lossCauses.size() == 0);
          foundEdgeInitialization = true;
        }
        if (fact.point.kind() == MirPointKind::Exit &&
            fact.point.exitValue().block == continuation.id) {
          ZC_EXPECT(fact.state == facts::InitializationState::initialized());
          ZC_EXPECT(fact.lossCauses.size() == 0);
          foundReturnCopy = true;
        }
      }
      ZC_EXPECT(foundEdgeInitialization);
      ZC_EXPECT(foundReturnCopy);
    }
  }
}

ZC_TEST("Ownership event overlay verifier rejects a tampered direct call result commit") {
  OwnershipPipelineFixture fixture(
      "fun helper() -> i32 { return 0; }\n"
      "fun entry() -> i32 { return helper(); }"_zc);
  const auto& builtMir = fixture.builtMir();

  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  bool tampered = false;
  for (const auto& function : builtMir.functions()) {
    if (function.blocks.size() != 2) continue;
    const auto& call = function.blocks[0].terminator.callValue();
    for (auto& overlay : candidate.functions) {
      if (overlay.owner != function.owner) continue;
      for (auto& slot : overlay.slots) {
        if (slot.key.location.point.kind() != MirPointKind::Edge ||
            slot.key.location.point.edgeValue().from != function.blocks[0].id ||
            slot.key.location.point.edgeValue().edgeOrdinal != 0 ||
            slot.key.location.point.edgeValue().to != call.normalTarget) {
          continue;
        }
        ZC_REQUIRE(slot.stage == OwnershipEventStage::Commit);
        ZC_REQUIRE(slot.roles.size() == 1);
        ZC_REQUIRE(slot.roles[0] == OwnershipEventRole::DestinationWrite);
        slot.stage = OwnershipEventStage::Effect;
        tampered = true;
      }
    }
  }
  ZC_REQUIRE(tampered);

  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::CanonicalCodecMismatch);
}

ZC_TEST("Ownership event overlay verifier rejects a tampered direct call marker use") {
  OwnershipPipelineFixture fixture(
      "fun helper() -> i32 { return 0; }\n"
      "fun entry() -> i32 { return helper(); }"_zc);
  const auto& builtMir = fixture.builtMir();

  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  bool tampered = false;
  for (const auto& function : builtMir.functions()) {
    if (function.blocks.size() != 2) continue;
    const auto& call = function.blocks[0].terminator.callValue();
    for (auto& overlay : candidate.functions) {
      if (overlay.owner != function.owner) continue;
      for (auto& use : overlay.markerUses) {
        if (use.key.event.location.point.kind() != MirPointKind::Edge ||
            use.key.event.location.point.edgeValue().from != function.blocks[0].id ||
            use.key.event.location.point.edgeValue().edgeOrdinal != 0 ||
            use.key.event.location.point.edgeValue().to != call.normalTarget) {
          continue;
        }
        ZC_REQUIRE(use.key.event.operandOrdinal == 0);
        use.key.event.operandOrdinal = 1;
        tampered = true;
        break;
      }
    }
  }
  ZC_REQUIRE(tampered);

  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::CanonicalCodecMismatch);
}

ZC_TEST("Ownership event overlay verifier rejects a tampered direct call drop plan") {
  OwnershipPipelineFixture fixture(
      "fun helper() -> i32 { return 0; }\n"
      "fun entry() -> i32 { return helper(); }"_zc);
  const auto& builtMir = fixture.builtMir();

  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  bool tampered = false;
  for (const auto& function : builtMir.functions()) {
    if (function.blocks.size() != 2) continue;
    const auto& call = function.blocks[0].terminator.callValue();
    for (auto& overlay : candidate.functions) {
      if (overlay.owner != function.owner) continue;
      for (auto& plan : overlay.logicalDropPlans) {
        if (plan.initialization.location.point.kind() != MirPointKind::Edge ||
            plan.initialization.location.point.edgeValue().from != function.blocks[0].id ||
            plan.initialization.location.point.edgeValue().edgeOrdinal != 0 ||
            plan.initialization.location.point.edgeValue().to != call.normalTarget) {
          continue;
        }
        ZC_REQUIRE(plan.initialization.operandOrdinal == 0);
        plan.initialization.operandOrdinal = 1;
        tampered = true;
      }
    }
  }
  ZC_REQUIRE(tampered);

  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::CanonicalCodecMismatch);
}

identity::Sha256Digest repeatedDigest(uint8_t byte) {
  uint8_t bytes[32];
  for (auto& value : bytes) value = byte;
  ZC_IF_SOME(digest, identity::Sha256Digest::fromBytes(zc::arrayPtr(bytes))) { return digest; }
  ZC_FAIL_REQUIRE("invalid ownership digest fixture");
}

zc::Array<uint8_t> encodeOverlayOracle(const identity::Sha256Digest& contextFingerprint,
                                       zc::ArrayPtr<const uint8_t> expandedModuleKey,
                                       const identity::Sha256Digest& checkedFactsRevision,
                                       const identity::Sha256Digest& builtRevision,
                                       zc::ArrayPtr<const zc::Array<uint8_t>> functions) {
  identity::CanonicalEncoder encoder;
  constexpr char domain[] = "zom.ownership-event-overlay";
  for (size_t index = 0; index + 1 < sizeof(domain); ++index) {
    encoder.encodeUint8(static_cast<uint8_t>(domain[index]));
  }
  encoder.encodeUint8(0);
  encoder.encodeDigest(contextFingerprint);
  encoder.encodeByteString(expandedModuleKey);
  encoder.encodeDigest(checkedFactsRevision);
  encoder.encodeDigest(builtRevision);
  encoder.encodeSequenceSize(functions.size());
  for (const auto& function : functions) { encoder.encodeByteString(function.asPtr()); }
  return encoder.finish();
}

zc::Array<uint8_t> encodeFixedOverlayOracle(zc::ArrayPtr<const zc::Array<uint8_t>> functions) {
  const uint8_t module[] = {0xa1};
  return encodeOverlayOracle(repeatedDigest(0x00), zc::arrayPtr(module), repeatedDigest(0x44),
                             repeatedDigest(0x22), functions);
}

zc::Array<uint8_t> encodeStatementEventKeyOracle(zc::ArrayPtr<const uint8_t> owner, uint32_t block,
                                                 uint32_t statement, uint32_t operandOrdinal) {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(owner);
  encoder.encodeUint8(0x02);
  encoder.encodeUint32(block);
  encoder.encodeUint32(statement);
  encoder.encodeUint32(operandOrdinal);
  return encoder.finish();
}

zc::Array<uint8_t> encodeTerminatorEventKeyOracle(zc::ArrayPtr<const uint8_t> owner, uint32_t block,
                                                  uint32_t operandOrdinal) {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(owner);
  encoder.encodeUint8(0x04);
  encoder.encodeUint32(block);
  encoder.encodeUint32(operandOrdinal);
  return encoder.finish();
}

zc::Array<uint8_t> encodeEventSlotOracle(zc::ArrayPtr<const uint8_t> key, uint8_t stage,
                                         zc::ArrayPtr<const uint8_t> roles) {
  identity::CanonicalEncoder encoder;
  for (uint8_t value : key) { encoder.encodeUint8(value); }
  encoder.encodeUint8(stage);
  encoder.encodeSequenceSize(roles.size());
  for (uint8_t role : roles) {
    const uint8_t encodedRole[] = {role};
    encoder.encodeByteString(zc::arrayPtr(encodedRole));
  }
  return encoder.finish();
}

zc::Array<uint8_t> twoPointFunctionOracle() {
  const uint8_t owner[] = {0xb1};
  const uint8_t statementRoles[] = {0x03, 0x04};
  const uint8_t terminatorRoles[] = {0x03, 0x05};
  auto statementKey = encodeStatementEventKeyOracle(zc::arrayPtr(owner), 1, 0, 0);
  auto statementSlot =
      encodeEventSlotOracle(statementKey.asPtr(), 0x01, zc::arrayPtr(statementRoles));
  auto terminatorKey = encodeTerminatorEventKeyOracle(zc::arrayPtr(owner), 1, 0);
  auto terminatorSlot =
      encodeEventSlotOracle(terminatorKey.asPtr(), 0x01, zc::arrayPtr(terminatorRoles));

  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(zc::arrayPtr(owner));
  encoder.encodeSequenceSize(2);
  encoder.encodeByteString(statementKey.asPtr());
  encoder.encodeByteString(statementSlot.asPtr());
  encoder.encodeByteString(terminatorKey.asPtr());
  encoder.encodeByteString(terminatorSlot.asPtr());
  for (int index = 0; index < 5; ++index) { encoder.encodeSequenceSize(0); }
  return encoder.finish();
}

void expectOverlayOracle(zc::Vector<zc::Array<uint8_t>>&& functions, zc::StringPtr expectedPreimage,
                         zc::StringPtr expectedDigest) {
  auto bytes = encodeFixedOverlayOracle(functions.asPtr());
  ZC_EXPECT(zc::encodeHex(bytes.asPtr()) == expectedPreimage);
  auto digest = identity::sha256(bytes.asPtr());
  ZC_REQUIRE(digest != zc::none);
  ZC_IF_SOME(value, digest) { ZC_EXPECT(zc::encodeHex(value.bytes()) == expectedDigest); }
}

ZC_TEST("Ownership event overlay projects standard marker decisions on resource roots") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);
  const auto& builtMir = fixture.builtMir();
  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_REQUIRE(verifiedResult.isVerified());
  auto overlay = zc::mv(verifiedResult).takeVerified();
  ZC_REQUIRE(overlay.functions().size() == 1);
  ZC_REQUIRE(overlay.functions()[0].markerUses.size() ==
             builtMir.functions()[0].locals.size() * 2 + 1);
  bool foundPositive = false;
  bool foundUnsatisfied = false;
  for (const auto& use : overlay.functions()[0].markerUses) {
    if (use.decision.is<OwnershipMarkerDecisionPositive>()) foundPositive = true;
    if (use.decision.is<OwnershipMarkerDecisionUnsatisfied>()) foundUnsatisfied = true;
  }
  ZC_EXPECT(foundPositive);
  ZC_EXPECT(foundUnsatisfied);
}

ZC_TEST("Ownership marker proof construction records a positive Copy decision") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Copy};\n"
      "struct Cell { value: i32, }\n"
      "unsafe impl Copy for Cell;\n"
      "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc);
  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_REQUIRE(verifiedResult.isVerified());
  auto overlay = zc::mv(verifiedResult).takeVerified();
  ZC_REQUIRE(overlay.functions().size() == 1);
  const auto& function = overlay.functions()[0];
  ZC_REQUIRE(function.logicalDropPlans.size() == 1);
  const auto cellType = function.logicalDropPlans[0].root.resultType();
  const auto copyMarker = fixture.overlayInput().body.standardMarkers.copy();
  const auto linearMarker = fixture.overlayInput().body.standardMarkers.linear();

  bool foundCopyPositive = false;
  bool foundLinearUnsatisfied = false;
  for (const auto& use : function.markerUses) {
    if (use.key.subject != cellType) continue;
    if (use.key.marker == copyMarker) {
      ZC_EXPECT(use.decision.is<OwnershipMarkerDecisionPositive>());
      foundCopyPositive = true;
    }
    if (use.key.marker == linearMarker) {
      ZC_EXPECT(use.decision.is<OwnershipMarkerDecisionUnsatisfied>());
      foundLinearUnsatisfied = true;
    }
  }
  ZC_EXPECT(foundCopyPositive);
  ZC_EXPECT(foundLinearUnsatisfied);
}

ZC_TEST("Ownership marker proof construction records a positive Linear decision") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Linear};\n"
      "struct Cell { value: i32, }\n"
      "unsafe impl Linear for Cell;\n"
      "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc);
  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_REQUIRE(verifiedResult.isVerified());
  auto overlay = zc::mv(verifiedResult).takeVerified();
  ZC_REQUIRE(overlay.functions().size() == 1);
  const auto& function = overlay.functions()[0];
  ZC_REQUIRE(function.logicalDropPlans.size() == 1);
  const auto cellType = function.logicalDropPlans[0].root.resultType();
  const auto copyMarker = fixture.overlayInput().body.standardMarkers.copy();
  const auto linearMarker = fixture.overlayInput().body.standardMarkers.linear();

  // Cell has no explicit Copy impl, so Copy is derived structurally: the i32
  // field is Copy+ (builtin), therefore Cell is Copy+ via structural proof.
  bool foundLinearPositive = false;
  bool foundCopyPositive = false;
  for (const auto& use : function.markerUses) {
    if (use.key.subject != cellType) continue;
    if (use.key.marker == linearMarker) {
      ZC_EXPECT(use.decision.is<OwnershipMarkerDecisionPositive>());
      foundLinearPositive = true;
    }
    if (use.key.marker == copyMarker) {
      ZC_EXPECT(use.decision.is<OwnershipMarkerDecisionPositive>());
      foundCopyPositive = true;
    }
  }
  ZC_EXPECT(foundLinearPositive);
  ZC_EXPECT(foundCopyPositive);
}

ZC_TEST("Ownership descendant query tree enumerates aggregate field descendants") {
  OwnershipPipelineFixture fixture(
      "struct Pair { left: i32, right: bool, }\n"
      "fun entry() -> Pair { let pair = Pair { left: 0, right: true }; return pair; }"_zc);
  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_REQUIRE(verifiedResult.isVerified());
  auto overlay = zc::mv(verifiedResult).takeVerified();
  ZC_REQUIRE(overlay.functions().size() == 1);
  const auto& function = overlay.functions()[0];
  ZC_REQUIRE(function.logicalDropPlans.size() == 1);
  const auto pairType = function.logicalDropPlans[0].root.resultType();

  // Recover the closed component types from the aggregate literal elements.
  const auto& mirFunction = fixture.builtMir().functions()[0];
  const auto& assignment = mirFunction.blocks[0].statements[1].assignmentValue();
  const auto& aggregate = assignment.value.nominalAggregateValue();
  ZC_REQUIRE(aggregate.elements.size() == 2);
  const auto i32Type = aggregate.elements[0].operand.constantValue().type;
  const auto boolType = aggregate.elements[1].operand.constantValue().type;
  const auto copyMarker = fixture.overlayInput().body.standardMarkers.copy();
  const auto linearMarker = fixture.overlayInput().body.standardMarkers.linear();

  // The descendant query tree must emit Copy and Linear queries for every node:
  // the Pair root, the i32 field, and the bool field.
  bool foundPairCopy = false;
  bool foundPairLinear = false;
  bool foundI32Copy = false;
  bool foundI32Linear = false;
  bool foundBoolCopy = false;
  bool foundBoolLinear = false;
  for (const auto& use : function.markerUses) {
    if (use.key.subject == pairType) {
      if (use.key.marker == copyMarker) foundPairCopy = true;
      if (use.key.marker == linearMarker) foundPairLinear = true;
    }
    if (use.key.subject == i32Type) {
      if (use.key.marker == copyMarker) foundI32Copy = true;
      if (use.key.marker == linearMarker) foundI32Linear = true;
    }
    if (use.key.subject == boolType) {
      if (use.key.marker == copyMarker) foundBoolCopy = true;
      if (use.key.marker == linearMarker) foundBoolLinear = true;
    }
  }
  ZC_EXPECT(foundPairCopy);
  ZC_EXPECT(foundPairLinear);
  ZC_EXPECT(foundI32Copy);
  ZC_EXPECT(foundI32Linear);
  ZC_EXPECT(foundBoolCopy);
  ZC_EXPECT(foundBoolLinear);
}

ZC_TEST("Ownership postorder fold emits a maximal linear component for an aggregate") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Linear};\n"
      "struct Pair { left: i32, right: bool, }\n"
      "unsafe impl Linear for Pair;\n"
      "fun entry() -> Pair { let pair = Pair { left: 0, right: true }; return pair; }"_zc);
  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_REQUIRE(verifiedResult.isVerified());
  auto overlay = zc::mv(verifiedResult).takeVerified();
  ZC_REQUIRE(overlay.functions().size() == 1);
  const auto& function = overlay.functions()[0];
  ZC_REQUIRE(function.logicalDropPlans.size() == 1);
  const auto& plan = function.logicalDropPlans[0];
  const auto pairType = plan.root.resultType();

  // Pair is Linear+ (explicit) and Copy+ (structural: i32 and bool are Copy+).
  // Case 2 of the fold: emit root only, no action, suppress descendants.
  ZC_REQUIRE(plan.components.size() == 1);
  const auto& component = plan.components[0];
  ZC_EXPECT(component.place.resultType() == pairType);
  ZC_EXPECT(component.valueType == pairType);
  ZC_EXPECT(component.dropAction == zc::none);
  ZC_EXPECT(component.declarationOrdinal == 0);
}

ZC_TEST("Ownership postorder fold suppresses components for a copy aggregate") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Copy};\n"
      "struct Pair { left: i32, right: bool, }\n"
      "unsafe impl Copy for Pair;\n"
      "fun entry() -> Pair { let pair = Pair { left: 0, right: true }; return pair; }"_zc);
  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_REQUIRE(verifiedResult.isVerified());
  auto overlay = zc::mv(verifiedResult).takeVerified();
  ZC_REQUIRE(overlay.functions().size() == 1);
  const auto& function = overlay.functions()[0];
  ZC_REQUIRE(function.logicalDropPlans.size() == 1);
  const auto& plan = function.logicalDropPlans[0];

  // Pair is Copy+ (explicit) and Linear- (no policy).  Case 4 of the fold:
  // Copy+ leaf with empty child fold emits nothing.
  ZC_EXPECT(plan.components.size() == 0);
}

ZC_TEST("Ownership postorder fold retains a logical component for a non-copy aggregate") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Copy};\n"
      "struct Pair { left: i32, right: bool, }\n"
      "impl !Copy for Pair;\n"
      "fun entry() -> Pair { let pair = Pair { left: 0, right: true }; return pair; }"_zc);
  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_REQUIRE(verifiedResult.isVerified());
  auto overlay = zc::mv(verifiedResult).takeVerified();
  ZC_REQUIRE(overlay.functions().size() == 1);
  const auto& function = overlay.functions()[0];
  ZC_REQUIRE(function.logicalDropPlans.size() == 1);
  const auto& plan = function.logicalDropPlans[0];
  const auto pairType = plan.root.resultType();

  // Pair is Copy- (explicit negative) and Linear- (no policy).  Case 4 of the
  // fold: Copy- leaf with empty child fold emits one action-free component.
  ZC_REQUIRE(plan.components.size() == 1);
  const auto& component = plan.components[0];
  ZC_EXPECT(component.place.resultType() == pairType);
  ZC_EXPECT(component.valueType == pairType);
  ZC_EXPECT(component.dropAction == zc::none);
  ZC_EXPECT(component.declarationOrdinal == 0);
}

/// \brief Resolves the materialized struct definition behind one nominal type.
zc::Maybe<identity::DefId> nominalStructDefinition(const OwnershipEventOverlayInput& input,
                                                   identity::SemanticTypeId typeId) {
  auto lookup = input.body.semanticTypes.get(typeId);
  if (!lookup.is<type::SemanticTypeLookup>()) return zc::none;
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  if (!data.is<type::semantic::NominalTypeData>()) return zc::none;
  const auto& nominal = data.get<type::semantic::NominalTypeData>();
  auto entry = input.body.identities.definition(nominal.definition);
  if (entry == zc::none) return zc::none;
  identity::DefId materialized;
  ZC_IF_SOME(value, entry) { materialized = value.handle(); }
  return materialized;
}

/// \brief Independently derives the declared deinitializer of one nominal struct type.
///
/// Test oracle mirroring directDeinitializerForType through the overlay input's public
/// authority surface, so the expected DefId does not come from the production path.
zc::Maybe<identity::DefId> expectedDeinitializer(const OwnershipEventOverlayInput& input,
                                                 identity::SemanticTypeId typeId) {
  auto definition = nominalStructDefinition(input, typeId);
  if (definition == zc::none) return zc::none;
  identity::DefId structDefinition;
  ZC_IF_SOME(value, definition) { structDefinition = value; }
  for (const auto& signature : input.body.signatureFacts.signatures()) {
    if (signature.definition != structDefinition) continue;
    if (signature.definitionKind != identity::DefinitionKind::Struct) return zc::none;
    if (!signature.payload.variant().is<checker::signature::NominalSignature>()) return zc::none;
    const auto& nominalSignature =
        signature.payload.variant().get<checker::signature::NominalSignature>();
    for (const auto member : nominalSignature.members) {
      auto memberEntry = input.body.identities.definition(member);
      if (memberEntry == zc::none) return zc::none;
      ZC_IF_SOME(memberValue, memberEntry) {
        if (memberValue.record().kind() == identity::DefinitionKind::Destructor) { return member; }
      }
    }
    return zc::none;
  }
  return zc::none;
}

ZC_TEST(
    "Ownership deinitializer attachment yields one maximal root component with a declared action") {
  OwnershipPipelineFixture fixture(
      "struct Managed { value: i32; deinit() {} }\n"
      "fun entry() -> Managed { let managed = Managed { value: 0 }; return managed; }"_zc);
  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_REQUIRE(verifiedResult.isVerified());
  auto overlay = zc::mv(verifiedResult).takeVerified();
  ZC_REQUIRE(overlay.functions().size() == 1);
  const auto& function = overlay.functions()[0];
  ZC_REQUIRE(function.logicalDropPlans.size() == 1);
  const auto& plan = function.logicalDropPlans[0];
  const auto managedType = plan.root.resultType();

  // The deinitializer cutoff suppresses field descendants: one maximal root
  // component carrying the declared deinitializer action.
  ZC_REQUIRE(plan.components.size() == 1);
  const auto& component = plan.components[0];
  ZC_EXPECT(component.place.resultType() == managedType);
  ZC_EXPECT(component.valueType == managedType);
  ZC_REQUIRE(component.dropAction != zc::none);
  ZC_IF_SOME(action, component.dropAction) {
    ZC_REQUIRE(action.is<LogicalDropDeclaredAction>());
    auto expected = expectedDeinitializer(fixture.overlayInput(), managedType);
    ZC_REQUIRE(expected != zc::none);
    ZC_IF_SOME(deinitializer, expected) {
      ZC_EXPECT(action.get<LogicalDropDeclaredAction>().deinitializer == deinitializer);
    }
  }
  ZC_EXPECT(component.declarationOrdinal == 0);

  // No marker use may target the suppressed i32 field descendant; every query
  // targets the Managed root.
  for (const auto& use : function.markerUses) { ZC_EXPECT(use.key.subject == managedType); }
}

ZC_TEST("Ownership deinitializer attachment stops descendant discovery at the outer root") {
  // The Outer parameter is transferred through a let binding so the drop plan is
  // generated on the Use-rvalue destination path.  This avoids the aggregate
  // literal admission restriction (scalar values only) while still producing a
  // drop plan for a nominal type that carries a nested nominal field.
  OwnershipPipelineFixture fixture(
      "struct Inner { value: i32, }\n"
      "struct Outer { inner: Inner, deinit() {} }\n"
      "fun entry(outer: Outer) -> Outer { let result = outer; return result; }"_zc);
  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_REQUIRE(verifiedResult.isVerified());
  auto overlay = zc::mv(verifiedResult).takeVerified();
  ZC_REQUIRE(overlay.functions().size() == 1);
  const auto& function = overlay.functions()[0];
  ZC_REQUIRE(function.logicalDropPlans.size() == 1);
  const auto& plan = function.logicalDropPlans[0];
  const auto outerType = plan.root.resultType();

  // The Outer deinitializer stops discovery at the outer root: one maximal
  // component carrying the declared action, no Inner or i32 descendants.
  ZC_REQUIRE(plan.components.size() == 1);
  const auto& component = plan.components[0];
  ZC_EXPECT(component.place.resultType() == outerType);
  ZC_EXPECT(component.valueType == outerType);
  ZC_REQUIRE(component.dropAction != zc::none);
  ZC_IF_SOME(action, component.dropAction) {
    ZC_REQUIRE(action.is<LogicalDropDeclaredAction>());
    auto expected = expectedDeinitializer(fixture.overlayInput(), outerType);
    ZC_REQUIRE(expected != zc::none);
    ZC_IF_SOME(deinitializer, expected) {
      ZC_EXPECT(action.get<LogicalDropDeclaredAction>().deinitializer == deinitializer);
    }
  }
  ZC_EXPECT(component.declarationOrdinal == 0);

  // Every marker use targets the Outer root; the Inner field and its i32
  // descendant are suppressed by the deinitializer cutoff.
  for (const auto& use : function.markerUses) { ZC_EXPECT(use.key.subject == outerType); }
}

ZC_TEST("Ownership event overlay verifier rejects a tampered deinitializer") {
  OwnershipPipelineFixture fixture(
      "struct Managed { value: i32; deinit() {} }\n"
      "fun entry() -> Managed { let managed = Managed { value: 0 }; return managed; }"_zc);
  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  auto& function = candidate.functions[0];
  ZC_REQUIRE(function.logicalDropPlans.size() == 1);
  auto& plan = function.logicalDropPlans[0];
  ZC_REQUIRE(plan.components.size() == 1);
  auto& component = plan.components[0];
  ZC_REQUIRE(component.dropAction != zc::none);
  ZC_IF_SOME(action, component.dropAction) { ZC_REQUIRE(action.is<LogicalDropDeclaredAction>()); }
  // Replace the deinitializer with the struct definition, a resolvable but
  // foreign DefId that encodes to different canonical bytes.
  const auto managedType = plan.root.resultType();
  auto foreign = nominalStructDefinition(fixture.overlayInput(), managedType);
  ZC_REQUIRE(foreign != zc::none);
  ZC_IF_SOME(definition, foreign) {
    component.dropAction = LogicalDropAction(LogicalDropDeclaredAction{definition});
  }

  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_EXPECT(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::CanonicalCodecMismatch);
}

ZC_TEST("Ownership event overlay test encoder matches the RFC 0007 empty oracle") {
  zc::Vector<zc::Array<uint8_t>> functions;
  expectOverlayOracle(
      zc::mv(functions),
      "7a6f6d2e6f776e6572736869702d6576656e742d6f7665726c61790000000000000000000000000000000000000000000000000000000000000000000000000000000001a1444444444444444444444444444444444444444444444444444444444444444422222222222222222222222222222222222222222222222222222222222222220000000000000000"_zc,
      "9e673e954367c3f2783cef1a9ca46e4d7e89040f2d4285ac6e42c2137bbed1d2"_zc);
}

ZC_TEST("Ownership event overlay test encoder matches the RFC 0007 empty-function oracle") {
  const uint8_t owner[] = {0xb1};
  identity::CanonicalEncoder functionEncoder;
  functionEncoder.encodeByteString(zc::arrayPtr(owner));
  for (int index = 0; index < 6; ++index) functionEncoder.encodeSequenceSize(0);
  zc::Vector<zc::Array<uint8_t>> functions;
  functions.add(functionEncoder.finish());
  expectOverlayOracle(
      zc::mv(functions),
      "7a6f6d2e6f776e6572736869702d6576656e742d6f7665726c61790000000000000000000000000000000000000000000000000000000000000000000000000000000001a144444444444444444444444444444444444444444444444444444444444444442222222222222222222222222222222222222222222222222222222222222222000000000000000100000000000000390000000000000001b1000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"_zc,
      "5e36e3dd6068992f4e3b99ea9eb7df4e3836f9f8c40eb9821238a3c6090d724c"_zc);
}

zc::Array<uint8_t> unsafeCollisionFunctionOracle() {
  const uint8_t owner[] = {0xb1};
  const uint8_t sourceFile[] = {0xc1};
  const uint8_t slotRoles[] = {0x01, 0x14};  // Operation, UnsafeOperation

  auto eventKey = encodeStatementEventKeyOracle(zc::arrayPtr(owner), 1, 0, 0);
  auto slotValue =
      encodeEventSlotOracle(eventKey.asPtr(), 0x02 /* Effect */, zc::arrayPtr(slotRoles));

  auto encodeOccurrenceKey = [&](uint32_t ordinal) {
    identity::CanonicalEncoder encoder;
    for (uint8_t byte : eventKey) { encoder.encodeUint8(byte); }
    encoder.encodeUint32(ordinal);
    return encoder.finish();
  };
  auto occurrenceKey0 = encodeOccurrenceKey(0);
  auto occurrenceKey1 = encodeOccurrenceKey(1);

  // Occurrence value: raw event key + ordinal + operation + requirement +
  // acknowledgement tag + raw acknowledgement event key + source span. The
  // source span is a raw source-file byte followed by two u64 bounds, matching
  // the RFC 0007 oracle layout.
  auto encodeOccurrenceValue = [&](uint32_t ordinal, uint8_t operation) {
    identity::CanonicalEncoder encoder;
    for (uint8_t byte : eventKey) { encoder.encodeUint8(byte); }
    encoder.encodeUint32(ordinal);
    encoder.encodeUint8(operation);
    encoder.encodeUint8(0x02);  // UnsafeRequirement::RawPointerBoundary
    encoder.encodeUint8(0x01);  // acknowledgement present
    for (uint8_t byte : eventKey) { encoder.encodeUint8(byte); }
    encoder.encodeUint8(sourceFile[0]);
    encoder.encodeUint64(ordinal);
    encoder.encodeUint64(ordinal + 1);
    return encoder.finish();
  };
  auto occurrenceValue0 = encodeOccurrenceValue(0, 0x01 /* RawDereference */);
  auto occurrenceValue1 = encodeOccurrenceValue(1, 0x02 /* RawCast */);

  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(zc::arrayPtr(owner));
  encoder.encodeSequenceSize(1);  // slots
  encoder.encodeByteString(eventKey.asPtr());
  encoder.encodeByteString(slotValue.asPtr());
  encoder.encodeSequenceSize(0);  // deferredActivations
  encoder.encodeSequenceSize(2);  // unsafeOccurrences
  encoder.encodeByteString(occurrenceKey0.asPtr());
  encoder.encodeByteString(occurrenceValue0.asPtr());
  encoder.encodeByteString(occurrenceKey1.asPtr());
  encoder.encodeByteString(occurrenceValue1.asPtr());
  encoder.encodeSequenceSize(0);  // markerUses
  encoder.encodeSequenceSize(0);  // logicalDropPlans
  encoder.encodeSequenceSize(0);  // castResourcePlans
  return encoder.finish();
}

ZC_TEST("Ownership event overlay test encoder matches the RFC 0007 unsafe-collision oracle") {
  zc::Vector<zc::Array<uint8_t>> functions;
  functions.add(unsafeCollisionFunctionOracle());
  expectOverlayOracle(zc::mv(functions),
                      "7a6f6d2e6f776e6572736869702d6576656e742d6f7665726c61790000000000000000000000"
                      "0000000000000000000000"
                      "00000000000000000000000000000000000001a1444444444444444444444444444444444444"
                      "4444444444444444444444"
                      "4444442222222222222222222222222222222222222222222222222222222222222222000000"
                      "0000000001000000000000"
                      "016c0000000000000001b1000000000000000100000000000000160000000000000001b10200"
                      "0000010000000000000000"
                      "00000000000000310000000000000001b1020000000100000000000000000200000000000000"
                      "0200000000000000010100"
                      "000000000000011400000000000000000000000000000002000000000000001a000000000000"
                      "0001b10200000001000000"
                      "00000000000000000000000000000000440000000000000001b1020000000100000000000000"
                      "0000000000010201000000"
                      "0000000001b102000000010000000000000000c1000000000000000000000000000000010000"
                      "00000000001a0000000000"
                      "000001b1020000000100000000000000000000000100000000000000440000000000000001b1"
                      "0200000001000000000000"
                      "0000000000010202010000000000000001b102000000010000000000000000c1000000000000"
                      "0001000000000000000200"
                      "0000000000000000000000000000000000000000000000"_zc,
                      "bb0547e574d4e5929b0c23807103b8566895fe9df68de0e9cfff01d3a9679f85"_zc);
}

ZC_TEST("Ownership event overlay unsafe-collision oracle changes when any input byte is mutated") {
  auto record = unsafeCollisionFunctionOracle();
  ZC_REQUIRE(record.size() == 364);
  zc::Vector<zc::Array<uint8_t>> baselineFunctions;
  baselineFunctions.add(zc::heapArray(record.asPtr()));
  auto baseline = encodeFixedOverlayOracle(baselineFunctions.asPtr());
  auto baselineDigest = identity::sha256(baseline.asPtr());
  ZC_REQUIRE(baselineDigest != zc::none);

  struct Mutation final {
    size_t offset;
    uint8_t expected;
    uint8_t mutated;
  };
  const Mutation mutations[] = {
      {8, 0xb1, 0xb2},    // owner
      {77, 0x02, 0x01},   // slot stage (Effect -> Before)
      {188, 0x01, 0x03},  // occurrence 0 operation (RawDereference -> 0x03)
      {298, 0x02, 0x04},  // occurrence 1 operation (RawCast -> 0x04)
  };

  for (const auto& mutation : mutations) {
    ZC_REQUIRE(mutation.offset < record.size());
    ZC_REQUIRE(record[mutation.offset] == mutation.expected);
    auto mutated = zc::heapArray(record.asPtr());
    mutated[mutation.offset] = mutation.mutated;
    zc::Vector<zc::Array<uint8_t>> mutatedFunctions;
    mutatedFunctions.add(zc::mv(mutated));
    auto changed = encodeFixedOverlayOracle(mutatedFunctions.asPtr());
    auto changedDigest = identity::sha256(changed.asPtr());
    ZC_REQUIRE(changedDigest != zc::none);
    ZC_IF_SOME(before, baselineDigest) {
      ZC_IF_SOME(after, changedDigest) { ZC_EXPECT(before != after); }
    }
  }
}

ZC_TEST("Ownership event overlay oracle binds copy and move reads to complete MIR points") {
  zc::Vector<zc::Array<uint8_t>> functions;
  functions.add(twoPointFunctionOracle());
  expectOverlayOracle(
      zc::mv(functions),
      "7a6f6d2e6f776e6572736869702d6576656e742d6f7665726c617900000000000000000000000000"
      "00000000000000000000000000000000000000000000000000000001a14444444444444444444444"
      "44444444444444444444444444444444444444444422222222222222222222222222222222222222"
      "22222222222222222222222222000000000000000100000000000000df0000000000000001b10000"
      "00000000000200000000000000160000000000000001b10200000001000000000000000000000000"
      "000000310000000000000001b1020000000100000000000000000100000000000000020000000000"
      "0000010300000000000000010400000000000000120000000000000001b104000000010000000000"
      "0000000000002d0000000000000001b1040000000100000000010000000000000002000000000000"
      "00010300000000000000010500000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000"_zc,
      "2a05a2df34387dc8b31426748a62d9eb4c84a9c3148f401a171ef1092779d3eb"_zc);
}

// ---------------------------------------------------------------------------
// Differential oracle: a third implementation recomputes every facts
// inventory directly from Built MIR, the event overlay, and the borrow
// evidence, then matches the production derivation as sets.
// ---------------------------------------------------------------------------

void expectOracleMatchesInventory(const OwnershipPipelineFixture& fixture) {
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = session.getOwnershipCheckedMirModules()[0].eventOverlay();
  auto evidence = fixture.cloneBorrowEvidence();
  const auto& inputs = ownershipInputs(session);
  const test_oracle::OwnershipFactsOracle oracle(builtMir, overlay, evidence);

  {
    auto derived = oracle.movePaths();
    ZC_REQUIRE(derived != zc::none);
    ZC_IF_SOME(value, derived) {
      ZC_EXPECT(test_oracle::matchesMovePaths(value.asPtr(), inputs.movePaths().functions()));
    }
  }
  {
    auto derived = oracle.flow();
    ZC_REQUIRE(derived != zc::none);
    ZC_IF_SOME(value, derived) {
      ZC_EXPECT(test_oracle::matchesFlow(value.asPtr(), inputs.flow().functions()));
    }
  }
  {
    auto derived = oracle.initialization();
    ZC_REQUIRE(derived != zc::none);
    ZC_IF_SOME(value, derived) {
      ZC_EXPECT(
          test_oracle::matchesInitialization(value.asPtr(), inputs.initialization().functions()));
    }
  }
  {
    auto derived = oracle.loans();
    ZC_REQUIRE(derived != zc::none);
    ZC_IF_SOME(value, derived) {
      ZC_EXPECT(test_oracle::matchesLoans(value.asPtr(), inputs.loans().loans()));
    }
  }
  {
    auto derived = oracle.references();
    ZC_REQUIRE(derived != zc::none);
    ZC_IF_SOME(value, derived) {
      ZC_EXPECT(test_oracle::matchesReferences(value.asPtr(), inputs.references().definitions()));
    }
  }
  {
    auto derived = oracle.regions();
    ZC_REQUIRE(derived != zc::none);
    ZC_IF_SOME(value, derived) {
      ZC_EXPECT(test_oracle::matchesRegions(value.asPtr(), inputs.regions().regions()));
    }
  }
  {
    auto derived = oracle.states();
    ZC_REQUIRE(derived != zc::none);
    ZC_IF_SOME(value, derived) {
      ZC_EXPECT(test_oracle::matchesStates(value.asPtr(), inputs.states().states()));
    }
  }
  {
    auto derived = oracle.resources();
    ZC_REQUIRE(derived != zc::none);
    ZC_IF_SOME(value, derived) {
      ZC_EXPECT(test_oracle::matchesResources(value.asPtr(), inputs.resources().functions()));
    }
  }
}

/// Differential oracle check for a source the borrow checker rejects: the
/// oracle is compared against fact families rebuilt from the staged Built MIR
/// rather than committed session facts, exercising the identical comparisons as
/// expectOracleMatchesInventory.
void expectOracleMatchesRejectedInventory(const RejectedBorrowPipelineFixture& fixture) {
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = fixture.overlay();
  auto evidence = fixture.cloneBorrowEvidence();
  const auto inventory = buildRejectedBorrowInventory(fixture);
  const test_oracle::OwnershipFactsOracle oracle(builtMir, overlay, evidence);

  {
    auto derived = oracle.movePaths();
    ZC_REQUIRE(derived != zc::none);
    ZC_IF_SOME(value, derived) {
      ZC_EXPECT(test_oracle::matchesMovePaths(value.asPtr(), inventory.movePaths.functions()));
    }
  }
  {
    auto derived = oracle.flow();
    ZC_REQUIRE(derived != zc::none);
    ZC_IF_SOME(value, derived) {
      ZC_EXPECT(test_oracle::matchesFlow(value.asPtr(), inventory.flow.functions()));
    }
  }
  {
    auto derived = oracle.initialization();
    ZC_REQUIRE(derived != zc::none);
    ZC_IF_SOME(value, derived) {
      ZC_EXPECT(
          test_oracle::matchesInitialization(value.asPtr(), inventory.initialization.functions()));
    }
  }
  {
    auto derived = oracle.loans();
    ZC_REQUIRE(derived != zc::none);
    ZC_IF_SOME(value, derived) {
      ZC_EXPECT(test_oracle::matchesLoans(value.asPtr(), inventory.loans.loans()));
    }
  }
  {
    auto derived = oracle.references();
    ZC_REQUIRE(derived != zc::none);
    ZC_IF_SOME(value, derived) {
      ZC_EXPECT(test_oracle::matchesReferences(value.asPtr(), inventory.references.definitions()));
    }
  }
  {
    auto derived = oracle.regions();
    ZC_REQUIRE(derived != zc::none);
    ZC_IF_SOME(value, derived) {
      ZC_EXPECT(test_oracle::matchesRegions(value.asPtr(), inventory.regions.regions()));
    }
  }
  {
    auto derived = oracle.states();
    ZC_REQUIRE(derived != zc::none);
    ZC_IF_SOME(value, derived) {
      ZC_EXPECT(test_oracle::matchesStates(value.asPtr(), inventory.states.states()));
    }
  }
  {
    auto derived = oracle.resources();
    ZC_REQUIRE(derived != zc::none);
    ZC_IF_SOME(value, derived) {
      ZC_EXPECT(test_oracle::matchesResources(value.asPtr(), inventory.resources.functions()));
    }
  }
}

ZC_TEST("Differential oracle matches production facts for a scalar parameter return") {
  OwnershipPipelineFixture fixture("fun entry(value: i32) -> i32 { return value; }"_zc);
  expectOracleMatchesInventory(fixture);
}

ZC_TEST("Differential oracle matches production facts for an aggregate local return") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Copy};\n"
      "struct Cell { value: i32, }\n"
      "impl !Copy for Cell;\n"
      "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc);
  expectOracleMatchesInventory(fixture);
}

ZC_TEST("Differential oracle matches production facts for a sequential aggregate move") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Copy};\n"
      "struct Cell { value: i32, }\n"
      "impl !Copy for Cell;\n"
      "fun entry() -> Cell { let first = Cell { value: 0 }; let second = first; return second; }"_zc);
  expectOracleMatchesInventory(fixture);
}

ZC_TEST("Differential oracle matches production facts for a direct call result") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Copy};\n"
      "struct Cell { value: i32, }\n"
      "impl !Copy for Cell;\n"
      "fun helper() -> Cell { let cell = Cell { value: 0 }; return cell; }\n"
      "fun entry() -> Cell { return helper(); }"_zc);
  expectOracleMatchesInventory(fixture);
}

ZC_TEST("Differential oracle matches production facts for a parameter reborrow") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectOracleMatchesInventory(fixture);
}

ZC_TEST("Differential oracle matches production facts for a local borrow") {
  RejectedBorrowPipelineFixture fixture(
      "fun borrow_local() -> &i32 { let value: i32 = 0; return &value; }"_zc);
  expectOracleMatchesRejectedInventory(fixture);
}

ZC_TEST("Differential oracle matches production facts for a mutable local borrow") {
  RejectedBorrowPipelineFixture fixture(
      "fun borrow_local_mut() -> &mut i32 { mut value: i32 = 0; return &mut value; }"_zc);
  expectOracleMatchesRejectedInventory(fixture);
}

ZC_TEST("Differential oracle matches production facts for a field projection write and read") {
  OwnershipPipelineFixture fixture(
      "struct Pair { mut left: i32, right: bool, }\n"
      "fun entry() -> bool { mut pair = Pair { left: 0, right: true }; pair.left = 2; return "
      "pair.right; }"_zc);
  expectOracleMatchesInventory(fixture);
}

ZC_TEST("Differential oracle matches production facts for a partial aggregate initialization") {
  OwnershipPipelineFixture fixture(
      "struct Pair { mut left: i32, mut right: bool, }\n"
      "fun entry() -> bool { mut pair: Pair; pair.left = 0; pair.right = true; return pair.right; }"_zc);
  expectOracleMatchesInventory(fixture);
}

ZC_TEST("Differential oracle matches production facts for a mutable receiver activation") {
  OwnershipPipelineFixture fixture(
      "struct Cell { value: i32, mutating fun read(this, amount: i32) -> i32; }\n"
      "fun entry() -> i32 { mut cell = Cell { value: 0 }; return cell.read(1); }"_zc);
  expectOracleMatchesInventory(fixture);
}

ZC_TEST("Differential oracle matches production facts for a linear aggregate return") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Linear};\n"
      "struct Cell { value: i32, }\n"
      "unsafe impl Linear for Cell;\n"
      "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc);
  expectOracleMatchesInventory(fixture);
}

ZC_TEST("Differential oracle matches production facts for a parameter move into a local") {
  OwnershipPipelineFixture fixture(
      "struct Cell { value: i32, }\n"
      "import core::marker::{Copy};\n"
      "impl !Copy for Cell;\n"
      "fun entry(value: Cell) -> Cell { let cell = value; return cell; }"_zc);
  expectOracleMatchesInventory(fixture);
}

ZC_TEST("Differential oracle matches production facts for a multi-function call chain") {
  OwnershipPipelineFixture fixture(
      "fun inner() -> i32 { return 0; }\n"
      "fun middle() -> i32 { return inner(); }\n"
      "fun entry() -> i32 { return middle(); }"_zc);
  expectOracleMatchesInventory(fixture);
}

ZC_TEST("Differential oracle matches production facts for a linear-logical aggregate return") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Copy, Linear};\n"
      "struct Cell { value: i32, }\n"
      "impl !Copy for Cell;\n"
      "unsafe impl Linear for Cell;\n"
      "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc);
  expectOracleMatchesInventory(fixture);
}

namespace {

struct AdapterCapture final {
  zc::Vector<diagnostics::DiagID> primaryIds;
  zc::Vector<zc::Vector<diagnostics::DiagID>> childIds;
};

class AdapterDiagnosticConsumer final : public diagnostics::DiagnosticConsumer {
public:
  explicit AdapterDiagnosticConsumer(AdapterCapture& capture) noexcept : capture(capture) {}

  void handleDiagnostic(const source::SourceManager&,
                        const diagnostics::Diagnostic& diagnostic) override {
    capture.primaryIds.add(diagnostic.getId());
    zc::Vector<diagnostics::DiagID> children;
    for (const auto& child : diagnostic.getChildDiagnostics()) { children.add(child->getId()); }
    capture.childIds.add(zc::mv(children));
  }

private:
  AdapterCapture& capture;
};

struct SyntheticIdentity final {
  identity::DefId owner;
  MirEventKey event;
  identity::SourceSpan span;
  mir::MirLocalId localId;
  identity::SemanticTypeId localType;
};

SyntheticIdentity syntheticIdentity(const OwnershipPipelineFixture& fixture) {
  const auto& function = fixture.builtMir().functions()[0];
  ZC_REQUIRE(function.locals.size() >= 1);
  const auto& local = function.locals[0];
  return {function.owner, MirEventKey{MirLocation{function.owner, MirPoint::entry()}, 0},
          local.sourceSpan.clone(), local.id, local.type};
}

facts::MovePathKey syntheticPlace(const SyntheticIdentity& identity) {
  return facts::MovePathKey{identity.owner,
                            mir::MirPlace(identity.localId, identity.localType,
                                          zc::Vector<mir::MirProjection>{}, identity.localType)};
}

template <typename Cause>
zc::Vector<Cause> causeVector(Cause cause) {
  zc::Vector<Cause> result;
  result.add(zc::mv(cause));
  return result;
}

zc::Maybe<zc::Vector<driver::ParsedModuleRecord>> materializeParsedModules(
    const OwnershipPipelineFixture& fixture) {
  return fixture.compilerSession().materializeParsedModules();
}

}  // namespace

ZC_TEST("Ownership diagnostic adapter maps every closed failure variant") {
  OwnershipPipelineFixture fixture("let a = 0; let b = 1;"_zc);
  auto parsedModules = materializeParsedModules(fixture);
  ZC_REQUIRE(parsedModules != zc::none);
  ZC_IF_SOME(modules, parsedModules) {
    ZC_REQUIRE(modules.size() >= 1);
    const auto& parsed = modules[0].parsedModule();
    auto identity = syntheticIdentity(fixture);

    AdapterCapture capture;
    fixture.compilerSession().getDiagnosticEngine().addConsumer(
        zc::heap<AdapterDiagnosticConsumer>(capture));

    zc::Vector<OwnershipSourceFailure> failures;
    failures.add(UseAfterMoveFailure{
        identity.owner, identity.event, identity.span.clone(), syntheticPlace(identity), 0,
        causeVector(InitializationFailureCause{facts::InitializationLossKind::Moved, identity.event,
                                               identity.span.clone()})});
    failures.add(MutableBorrowConflictFailure{
        identity.owner, identity.event, identity.span.clone(), syntheticPlace(identity), 1,
        causeVector(LoanFailureCause{LoanKey{identity.event}, syntheticPlace(identity),
                                     identity.event, identity.span.clone()})});
    failures.add(UninitializedPlaceUseFailure{
        identity.owner, identity.event, identity.span.clone(), syntheticPlace(identity), 2,
        causeVector(InitializationFailureCause{facts::InitializationLossKind::NeverInitialized,
                                               identity.event, identity.span.clone()})});
    failures.add(SharedBorrowConflictFailure{
        identity.owner, identity.event, identity.span.clone(), syntheticPlace(identity), 3,
        causeVector(LoanFailureCause{LoanKey{identity.event}, syntheticPlace(identity),
                                     identity.event, identity.span.clone()})});
    failures.add(BorrowDoesNotLiveLongEnoughFailure{
        identity.owner, identity.event, identity.span.clone(), syntheticPlace(identity), 4,
        causeVector(EscapeFailureCause{identity.event, identity.span.clone()})});
    failures.add(LinearNotConsumedFailure{
        identity.owner, identity.event, identity.span.clone(), syntheticPlace(identity), 5,
        causeVector(LinearConsumptionCause{identity.event, identity.span.clone()})});
    failures.add(LinearConsumedTwiceFailure{
        identity.owner, identity.event, identity.span.clone(), syntheticPlace(identity), 6,
        causeVector(LinearConsumptionCause{identity.event, identity.span.clone()})});
    failures.add(RawPointerBoundaryRequiresUnsafeFailure{identity.owner, identity.event,
                                                         identity.span.clone(), 7,
                                                         UnsafeBoundaryKey{identity.event, 0}});
    failures.add(MoveOutOfBorrowFailure{
        identity.owner, identity.event, identity.span.clone(), syntheticPlace(identity), 8,
        causeVector(LoanFailureCause{LoanKey{identity.event}, syntheticPlace(identity),
                                     identity.event, identity.span.clone()})});

    emitOwnershipSourceFailures(fixture.compilerSession().getDiagnosticEngine(), parsed,
                                failures.asPtr());

    ZC_REQUIRE(capture.primaryIds.size() == 9);
    ZC_EXPECT(capture.primaryIds[0] == diagnostics::DiagID::UseAfterMove);
    ZC_EXPECT(capture.primaryIds[1] == diagnostics::DiagID::MutableBorrowConflicts);
    ZC_EXPECT(capture.primaryIds[2] == diagnostics::DiagID::UninitializedPlaceUse);
    ZC_EXPECT(capture.primaryIds[3] == diagnostics::DiagID::SharedBorrowConflicts);
    ZC_EXPECT(capture.primaryIds[4] == diagnostics::DiagID::BorrowDoesNotLiveLongEnough);
    ZC_EXPECT(capture.primaryIds[5] == diagnostics::DiagID::LinearNotConsumed);
    ZC_EXPECT(capture.primaryIds[6] == diagnostics::DiagID::LinearConsumedTwice);
    ZC_EXPECT(capture.primaryIds[7] == diagnostics::DiagID::RawPointerBoundaryRequiresUnsafe);
    ZC_EXPECT(capture.primaryIds[8] == diagnostics::DiagID::MoveOutOfBorrow);

    ZC_REQUIRE(capture.childIds.size() == 9);
    ZC_REQUIRE(capture.childIds[0].size() == 1);
    ZC_EXPECT(capture.childIds[0][0] == diagnostics::DiagID::ValueMovedHere);
    ZC_REQUIRE(capture.childIds[1].size() == 1);
    ZC_EXPECT(capture.childIds[1][0] == diagnostics::DiagID::BorrowOriginHere);
    ZC_REQUIRE(capture.childIds[2].size() == 1);
    ZC_EXPECT(capture.childIds[2][0] == diagnostics::DiagID::PlaceBecameUnavailableHere);
    ZC_REQUIRE(capture.childIds[3].size() == 1);
    ZC_EXPECT(capture.childIds[3][0] == diagnostics::DiagID::BorrowOriginHere);
    ZC_REQUIRE(capture.childIds[4].size() == 1);
    ZC_EXPECT(capture.childIds[4][0] == diagnostics::DiagID::BorrowReferentHere);
    ZC_REQUIRE(capture.childIds[5].size() == 1);
    ZC_EXPECT(capture.childIds[5][0] == diagnostics::DiagID::LinearInitializedHere);
    ZC_REQUIRE(capture.childIds[6].size() == 1);
    ZC_EXPECT(capture.childIds[6][0] == diagnostics::DiagID::LinearFirstConsumedHere);
    ZC_EXPECT(capture.childIds[7].size() == 0);
    ZC_REQUIRE(capture.childIds[8].size() == 1);
    ZC_EXPECT(capture.childIds[8][0] == diagnostics::DiagID::BorrowOriginHere);
  }
}

ZC_TEST("Ownership diagnostic adapter emits notes in cause order") {
  OwnershipPipelineFixture fixture("let a = 0; let b = 1;"_zc);
  auto parsedModules = materializeParsedModules(fixture);
  ZC_REQUIRE(parsedModules != zc::none);
  ZC_IF_SOME(modules, parsedModules) {
    ZC_REQUIRE(modules.size() >= 1);
    const auto& parsed = modules[0].parsedModule();
    auto identity = syntheticIdentity(fixture);
    const auto& function = fixture.builtMir().functions()[0];
    const auto functionSpan = function.sourceSpan.clone();

    AdapterCapture capture;
    fixture.compilerSession().getDiagnosticEngine().addConsumer(
        zc::heap<AdapterDiagnosticConsumer>(capture));

    zc::Vector<InitializationFailureCause> causes;
    causes.add(InitializationFailureCause{facts::InitializationLossKind::Moved, identity.event,
                                          identity.span.clone()});
    causes.add(InitializationFailureCause{facts::InitializationLossKind::Moved, identity.event,
                                          functionSpan.clone()});
    causes.add(InitializationFailureCause{facts::InitializationLossKind::Moved, identity.event,
                                          identity.span.clone()});

    zc::Vector<OwnershipSourceFailure> failures;
    failures.add(UseAfterMoveFailure{identity.owner, identity.event, identity.span.clone(),
                                     syntheticPlace(identity), 0, zc::mv(causes)});

    emitOwnershipSourceFailures(fixture.compilerSession().getDiagnosticEngine(), parsed,
                                failures.asPtr());

    ZC_REQUIRE(capture.primaryIds.size() == 1);
    ZC_EXPECT(capture.primaryIds[0] == diagnostics::DiagID::UseAfterMove);
    ZC_REQUIRE(capture.childIds.size() == 1);
    ZC_REQUIRE(capture.childIds[0].size() == 3);
    ZC_EXPECT(capture.childIds[0][0] == diagnostics::DiagID::ValueMovedHere);
    ZC_EXPECT(capture.childIds[0][1] == diagnostics::DiagID::ValueMovedHere);
    ZC_EXPECT(capture.childIds[0][2] == diagnostics::DiagID::ValueMovedHere);
  }
}

ZC_TEST("Ownership source failure ordering distinguishes a secondary cause span") {
  OwnershipPipelineFixture fixture("let a = 0; let b = 1;"_zc);
  auto identity = syntheticIdentity(fixture);
  const auto& function = fixture.builtMir().functions()[0];
  const auto functionSpan = function.sourceSpan.clone();

  zc::Vector<InitializationFailureCause> leftCauses;
  leftCauses.add(InitializationFailureCause{facts::InitializationLossKind::Moved, identity.event,
                                            identity.span.clone()});
  zc::Vector<InitializationFailureCause> rightCauses;
  rightCauses.add(InitializationFailureCause{facts::InitializationLossKind::Moved, identity.event,
                                             functionSpan.clone()});

  auto left = UseAfterMoveFailure{
      identity.owner,    identity.event, identity.span.clone(), syntheticPlace(identity), 0,
      zc::mv(leftCauses)};
  auto right = UseAfterMoveFailure{
      identity.owner,     identity.event, identity.span.clone(), syntheticPlace(identity), 0,
      zc::mv(rightCauses)};

  OwnershipSourceFailure leftFailure{zc::mv(left)};
  OwnershipSourceFailure rightFailure{zc::mv(right)};

  ZC_EXPECT(OwnershipSourceFailureOrdering::less(leftFailure, rightFailure) !=
            OwnershipSourceFailureOrdering::less(rightFailure, leftFailure));
}

ZC_TEST("Ownership source failure ordering distinguishes the primary diagnostic ID") {
  OwnershipPipelineFixture fixture("let a = 0; let b = 1;"_zc);
  auto identity = syntheticIdentity(fixture);

  // UninitializedPlaceUse (diag 4093, variant tag 2) and SharedBorrowConflict
  // (diag 4059, variant tag 3) share every other field. The numeric diagnostic
  // ID order reverses the variant-tag order, proving the ordering keys on the
  // diagnostic ID rather than the variant tag.
  auto uninitialized = UninitializedPlaceUseFailure{
      identity.owner,
      identity.event,
      identity.span.clone(),
      syntheticPlace(identity),
      0,
      causeVector(InitializationFailureCause{facts::InitializationLossKind::NeverInitialized,
                                             identity.event, identity.span.clone()})};
  auto conflict = SharedBorrowConflictFailure{
      identity.owner,
      identity.event,
      identity.span.clone(),
      syntheticPlace(identity),
      0,
      causeVector(LoanFailureCause{LoanKey{identity.event}, syntheticPlace(identity),
                                   identity.event, identity.span.clone()})};

  OwnershipSourceFailure uninitializedFailure{zc::mv(uninitialized)};
  OwnershipSourceFailure conflictFailure{zc::mv(conflict)};

  ZC_EXPECT(OwnershipSourceFailureOrdering::less(conflictFailure, uninitializedFailure));
  ZC_EXPECT(!OwnershipSourceFailureOrdering::less(uninitializedFailure, conflictFailure));
}

ZC_TEST("Ownership source failure equality distinguishes the owner key") {
  OwnershipPipelineFixture fixture("let a = 0; let b = 1;"_zc);
  auto identity = syntheticIdentity(fixture);
  const auto otherOwner = identity::DefId{};

  auto make = [&](identity::DefId owner) {
    return UseAfterMoveFailure{
        owner,
        identity.event,
        identity.span.clone(),
        syntheticPlace(identity),
        0,
        causeVector(InitializationFailureCause{facts::InitializationLossKind::Moved, identity.event,
                                               identity.span.clone()})};
  };

  OwnershipSourceFailure leftFailure{make(identity.owner)};
  OwnershipSourceFailure rightFailure{make(otherOwner)};

  // Distinct owners now have deterministic context-local ordering (context
  // brand then arena slot). Equality still separates them for deduplication.
  ZC_EXPECT(!OwnershipSourceFailureOrdering::equal(leftFailure, rightFailure));
  ZC_EXPECT(OwnershipSourceFailureOrdering::less(leftFailure, rightFailure) !=
            OwnershipSourceFailureOrdering::less(rightFailure, leftFailure));
}

ZC_TEST("Ownership source failure deduplication collapses byte-identical failures") {
  OwnershipPipelineFixture fixture("let a = 0; let b = 1;"_zc);
  auto identity = syntheticIdentity(fixture);

  auto make = [&] {
    return UseAfterMoveFailure{
        identity.owner,
        identity.event,
        identity.span.clone(),
        syntheticPlace(identity),
        0,
        causeVector(InitializationFailureCause{facts::InitializationLossKind::Moved, identity.event,
                                               identity.span.clone()})};
  };

  zc::Vector<OwnershipSourceFailure> failures;
  failures.add(make());
  failures.add(make());

  auto deduplicated = OwnershipSourceFailureOrdering::deduplicate(zc::mv(failures));
  ZC_EXPECT(deduplicated.size() == 1);
}

ZC_TEST("Ownership source failure deduplication retains distinct owners") {
  OwnershipPipelineFixture fixture("let a = 0; let b = 1;"_zc);
  auto identity = syntheticIdentity(fixture);
  const auto otherOwner = identity::DefId{};

  auto make = [&](identity::DefId owner) {
    return UseAfterMoveFailure{
        owner,
        identity.event,
        identity.span.clone(),
        syntheticPlace(identity),
        0,
        causeVector(InitializationFailureCause{facts::InitializationLossKind::Moved, identity.event,
                                               identity.span.clone()})};
  };

  zc::Vector<OwnershipSourceFailure> failures;
  failures.add(make(identity.owner));
  failures.add(make(otherOwner));

  auto deduplicated = OwnershipSourceFailureOrdering::deduplicate(zc::mv(failures));
  ZC_EXPECT(deduplicated.size() == 2);
}

ZC_TEST("Ownership source failure ordering distinguishes unsafe boundaries on one place event") {
  OwnershipPipelineFixture fixture("let a = 0; let b = 1;"_zc);
  auto identity = syntheticIdentity(fixture);

  auto left =
      RawPointerBoundaryRequiresUnsafeFailure{identity.owner, identity.event, identity.span.clone(),
                                              0, UnsafeBoundaryKey{identity.event, 0}};
  auto right =
      RawPointerBoundaryRequiresUnsafeFailure{identity.owner, identity.event, identity.span.clone(),
                                              0, UnsafeBoundaryKey{identity.event, 1}};

  OwnershipSourceFailure leftFailure{zc::mv(left)};
  OwnershipSourceFailure rightFailure{zc::mv(right)};

  ZC_EXPECT(OwnershipSourceFailureOrdering::less(leftFailure, rightFailure));
  ZC_EXPECT(!OwnershipSourceFailureOrdering::less(rightFailure, leftFailure));
  ZC_EXPECT(!OwnershipSourceFailureOrdering::equal(leftFailure, rightFailure));
}

ZC_TEST("Ownership event overlay projects unsafe scope boundaries from a scalar return") {
  OwnershipPipelineFixture fixture("fun answer() -> i32 { return unsafe { 42 }; }"_zc);
  const auto& builtMir = fixture.builtMir();
  ZC_REQUIRE(builtMir.functions().size() == 1);
  const auto& function = builtMir.functions()[0];
  ZC_REQUIRE(function.sourceScopes.size() == 2);
  ZC_EXPECT(function.sourceScopes[1].id.ordinal() == 2);
  ZC_IF_SOME(parent, function.sourceScopes[1].parent) { ZC_EXPECT(parent.ordinal() == 1); }
  ZC_REQUIRE(function.blocks.size() == 1);
  const auto& block = function.blocks[0];
  ZC_REQUIRE(block.statements.size() == 2);
  ZC_EXPECT(block.statements[0].kind() == mir::MirStatementKind::UnsafeScopeBoundary);
  ZC_EXPECT(block.statements[0].unsafeScopeBoundaryValue().kind ==
            mir::MirUnsafeScopeBoundaryKind::Enter);
  ZC_EXPECT(block.statements[0].unsafeScopeBoundaryValue().scope.ordinal() == 2);
  ZC_EXPECT(block.statements[1].kind() == mir::MirStatementKind::UnsafeScopeBoundary);
  ZC_EXPECT(block.statements[1].unsafeScopeBoundaryValue().kind ==
            mir::MirUnsafeScopeBoundaryKind::Exit);
  ZC_EXPECT(block.statements[1].unsafeScopeBoundaryValue().scope.ordinal() == 2);

  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  const auto& overlayFunction = candidate.functions[0];
  bool foundEnter = false;
  bool foundExit = false;
  bool foundUnsafeAcknowledgement = false;
  for (const auto& slot : overlayFunction.slots) {
    if (slot.key.location.point.kind() != MirPointKind::BeforeStatement) continue;
    if (slot.key.location.point.beforeStatementValue().block != block.id) continue;
    const auto ordinal = slot.key.location.point.beforeStatementValue().ordinal;
    if (ordinal == 0) {
      ZC_EXPECT(slot.stage == OwnershipEventStage::Effect);
      bool hasOperation = false;
      bool hasUnsafeAcknowledgement = false;
      for (const auto role : slot.roles) {
        if (role == OwnershipEventRole::Operation) hasOperation = true;
        if (role == OwnershipEventRole::UnsafeAcknowledgement) hasUnsafeAcknowledgement = true;
      }
      ZC_EXPECT(hasOperation);
      ZC_EXPECT(hasUnsafeAcknowledgement);
      foundEnter = true;
      foundUnsafeAcknowledgement = foundUnsafeAcknowledgement || hasUnsafeAcknowledgement;
    }
    if (ordinal == 1) {
      ZC_EXPECT(slot.stage == OwnershipEventStage::Effect);
      bool hasOperation = false;
      for (const auto role : slot.roles) {
        if (role == OwnershipEventRole::Operation) hasOperation = true;
      }
      ZC_EXPECT(hasOperation);
      foundExit = true;
    }
  }
  ZC_EXPECT(foundEnter);
  ZC_EXPECT(foundExit);
  ZC_EXPECT(foundUnsafeAcknowledgement);

  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_REQUIRE(verifiedResult.isVerified());
}

ZC_TEST("Ownership event overlay projects unsafe scope boundaries from a sequential return") {
  OwnershipPipelineFixture fixture(
      "fun answer() -> i32 { let source = 42; let dest = source; return unsafe { dest }; }"_zc);
  const auto& builtMir = fixture.builtMir();
  ZC_REQUIRE(builtMir.functions().size() == 1);
  const auto& function = builtMir.functions()[0];
  ZC_REQUIRE(function.sourceScopes.size() == 2);
  ZC_EXPECT(function.sourceScopes[1].id.ordinal() == 2);
  ZC_IF_SOME(parent, function.sourceScopes[1].parent) { ZC_EXPECT(parent.ordinal() == 1); }
  ZC_REQUIRE(function.blocks.size() == 1);
  const auto& block = function.blocks[0];
  ZC_REQUIRE(block.statements.size() == 6);
  ZC_EXPECT(block.statements[0].kind() == mir::MirStatementKind::StorageLive);
  ZC_EXPECT(block.statements[1].kind() == mir::MirStatementKind::Assign);
  ZC_EXPECT(block.statements[2].kind() == mir::MirStatementKind::StorageLive);
  ZC_EXPECT(block.statements[3].kind() == mir::MirStatementKind::Assign);
  ZC_EXPECT(block.statements[4].kind() == mir::MirStatementKind::UnsafeScopeBoundary);
  ZC_EXPECT(block.statements[4].unsafeScopeBoundaryValue().kind ==
            mir::MirUnsafeScopeBoundaryKind::Enter);
  ZC_EXPECT(block.statements[4].unsafeScopeBoundaryValue().scope.ordinal() == 2);
  ZC_EXPECT(block.statements[5].kind() == mir::MirStatementKind::UnsafeScopeBoundary);
  ZC_EXPECT(block.statements[5].unsafeScopeBoundaryValue().kind ==
            mir::MirUnsafeScopeBoundaryKind::Exit);
  ZC_EXPECT(block.statements[5].unsafeScopeBoundaryValue().scope.ordinal() == 2);

  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  const auto& overlayFunction = candidate.functions[0];
  bool foundEnter = false;
  bool foundExit = false;
  for (const auto& slot : overlayFunction.slots) {
    if (slot.key.location.point.kind() != MirPointKind::BeforeStatement) continue;
    if (slot.key.location.point.beforeStatementValue().block != block.id) continue;
    const auto ordinal = slot.key.location.point.beforeStatementValue().ordinal;
    if (ordinal == 4) {
      ZC_EXPECT(slot.stage == OwnershipEventStage::Effect);
      bool hasOperation = false;
      for (const auto role : slot.roles) {
        if (role == OwnershipEventRole::Operation) hasOperation = true;
      }
      ZC_EXPECT(hasOperation);
      foundEnter = true;
    }
    if (ordinal == 5) {
      ZC_EXPECT(slot.stage == OwnershipEventStage::Effect);
      bool hasOperation = false;
      for (const auto role : slot.roles) {
        if (role == OwnershipEventRole::Operation) hasOperation = true;
      }
      ZC_EXPECT(hasOperation);
      foundExit = true;
    }
  }
  ZC_EXPECT(foundEnter);
  ZC_EXPECT(foundExit);

  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_REQUIRE(verifiedResult.isVerified());
}

ZC_TEST("Ownership event overlay projects unsafe scope boundaries from a local return") {
  OwnershipPipelineFixture fixture(
      "fun answer() -> i32 { let local = 42; return unsafe { local }; }"_zc);
  const auto& builtMir = fixture.builtMir();
  ZC_REQUIRE(builtMir.functions().size() == 1);
  const auto& function = builtMir.functions()[0];
  ZC_REQUIRE(function.sourceScopes.size() == 2);
  ZC_EXPECT(function.sourceScopes[1].id.ordinal() == 2);
  ZC_IF_SOME(parent, function.sourceScopes[1].parent) { ZC_EXPECT(parent.ordinal() == 1); }
  ZC_REQUIRE(function.blocks.size() == 1);
  const auto& block = function.blocks[0];
  ZC_REQUIRE(block.statements.size() == 4);
  ZC_EXPECT(block.statements[0].kind() == mir::MirStatementKind::StorageLive);
  ZC_EXPECT(block.statements[1].kind() == mir::MirStatementKind::Assign);
  ZC_EXPECT(block.statements[2].kind() == mir::MirStatementKind::UnsafeScopeBoundary);
  ZC_EXPECT(block.statements[2].unsafeScopeBoundaryValue().kind ==
            mir::MirUnsafeScopeBoundaryKind::Enter);
  ZC_EXPECT(block.statements[2].unsafeScopeBoundaryValue().scope.ordinal() == 2);
  ZC_EXPECT(block.statements[3].kind() == mir::MirStatementKind::UnsafeScopeBoundary);
  ZC_EXPECT(block.statements[3].unsafeScopeBoundaryValue().kind ==
            mir::MirUnsafeScopeBoundaryKind::Exit);
  ZC_EXPECT(block.statements[3].unsafeScopeBoundaryValue().scope.ordinal() == 2);

  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  const auto& overlayFunction = candidate.functions[0];
  bool foundEnter = false;
  bool foundExit = false;
  for (const auto& slot : overlayFunction.slots) {
    if (slot.key.location.point.kind() != MirPointKind::BeforeStatement) continue;
    if (slot.key.location.point.beforeStatementValue().block != block.id) continue;
    const auto ordinal = slot.key.location.point.beforeStatementValue().ordinal;
    if (ordinal == 2) {
      ZC_EXPECT(slot.stage == OwnershipEventStage::Effect);
      bool hasOperation = false;
      for (const auto role : slot.roles) {
        if (role == OwnershipEventRole::Operation) hasOperation = true;
      }
      ZC_EXPECT(hasOperation);
      foundEnter = true;
    }
    if (ordinal == 3) {
      ZC_EXPECT(slot.stage == OwnershipEventStage::Effect);
      bool hasOperation = false;
      for (const auto role : slot.roles) {
        if (role == OwnershipEventRole::Operation) hasOperation = true;
      }
      ZC_EXPECT(hasOperation);
      foundExit = true;
    }
  }
  ZC_EXPECT(foundEnter);
  ZC_EXPECT(foundExit);

  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_REQUIRE(verifiedResult.isVerified());
}

ZC_TEST("Source suppression rule 3 suppresses UseAfterMove at the second-consumption event") {
  OwnershipPipelineFixture fixture("let a = 0; let b = 1;"_zc);
  auto identity = syntheticIdentity(fixture);
  const auto event = identity.event;

  auto movedUse = [&](uint32_t ordinal) {
    return UseAfterMoveFailure{
        identity.owner,
        event,
        identity.span.clone(),
        syntheticPlace(identity),
        ordinal,
        causeVector(InitializationFailureCause{facts::InitializationLossKind::Moved, event,
                                               identity.span.clone()})};
  };
  auto twice = [&](uint32_t ordinal) {
    return LinearConsumedTwiceFailure{
        identity.owner,
        event,
        identity.span.clone(),
        syntheticPlace(identity),
        ordinal,
        causeVector(LinearConsumptionCause{event, identity.span.clone()})};
  };

  zc::Vector<OwnershipSourceFailure> failures;
  failures.add(twice(0));
  failures.add(movedUse(1));

  auto retained = SourceSuppression::suppress(zc::mv(failures));
  ZC_REQUIRE(retained.size() == 1);
  ZC_EXPECT(retained[0].is<LinearConsumedTwiceFailure>());
}

ZC_TEST("Source suppression rule 4 suppresses UseAfterMove at the blocked-move event") {
  OwnershipPipelineFixture fixture("let a = 0; let b = 1;"_zc);
  auto identity = syntheticIdentity(fixture);
  const auto event = identity.event;

  auto movedUse = [&](uint32_t ordinal) {
    return UseAfterMoveFailure{
        identity.owner,
        event,
        identity.span.clone(),
        syntheticPlace(identity),
        ordinal,
        causeVector(InitializationFailureCause{facts::InitializationLossKind::Moved, event,
                                               identity.span.clone()})};
  };
  auto blocked = [&](uint32_t ordinal) {
    return MoveOutOfBorrowFailure{
        identity.owner,
        event,
        identity.span.clone(),
        syntheticPlace(identity),
        ordinal,
        causeVector(LoanFailureCause{LoanKey{event}, syntheticPlace(identity), event,
                                     identity.span.clone()})};
  };

  zc::Vector<OwnershipSourceFailure> failures;
  failures.add(blocked(0));
  failures.add(movedUse(1));

  auto retained = SourceSuppression::suppress(zc::mv(failures));
  ZC_REQUIRE(retained.size() == 1);
  ZC_EXPECT(retained[0].is<MoveOutOfBorrowFailure>());
}

ZC_TEST("Source suppression rule 7 retains unsafe and independent safe failures") {
  OwnershipPipelineFixture fixture("let a = 0; let b = 1;"_zc);
  auto identity = syntheticIdentity(fixture);
  const auto event = identity.event;

  auto unsafe = [&](uint32_t ordinal) {
    return RawPointerBoundaryRequiresUnsafeFailure{identity.owner, event, identity.span.clone(),
                                                   ordinal, UnsafeBoundaryKey{event, 0}};
  };
  auto movedUse = [&](uint32_t ordinal) {
    return UseAfterMoveFailure{
        identity.owner,
        event,
        identity.span.clone(),
        syntheticPlace(identity),
        ordinal,
        causeVector(InitializationFailureCause{facts::InitializationLossKind::Moved, event,
                                               identity.span.clone()})};
  };

  zc::Vector<OwnershipSourceFailure> failures;
  failures.add(unsafe(0));
  failures.add(movedUse(1));

  auto retained = SourceSuppression::suppress(zc::mv(failures));
  ZC_REQUIRE(retained.size() == 2);
  ZC_EXPECT(retained[0].is<RawPointerBoundaryRequiresUnsafeFailure>());
  ZC_EXPECT(retained[1].is<UseAfterMoveFailure>());
}

ZC_TEST("Source suppression retains UseAfterMove at a different event") {
  OwnershipPipelineFixture fixture("let a = 0; let b = 1;"_zc);
  auto identity = syntheticIdentity(fixture);
  const auto event = identity.event;
  const MirEventKey otherEvent{MirLocation{identity.owner, MirPoint::entry()}, 1};

  auto movedUse = [&](MirEventKey failureEvent, uint32_t ordinal) {
    return UseAfterMoveFailure{
        identity.owner,
        failureEvent,
        identity.span.clone(),
        syntheticPlace(identity),
        ordinal,
        causeVector(InitializationFailureCause{facts::InitializationLossKind::Moved, failureEvent,
                                               identity.span.clone()})};
  };
  auto twice = [&](uint32_t ordinal) {
    return LinearConsumedTwiceFailure{
        identity.owner,
        event,
        identity.span.clone(),
        syntheticPlace(identity),
        ordinal,
        causeVector(LinearConsumptionCause{event, identity.span.clone()})};
  };

  zc::Vector<OwnershipSourceFailure> failures;
  failures.add(twice(0));
  failures.add(movedUse(otherEvent, 1));

  auto retained = SourceSuppression::suppress(zc::mv(failures));
  ZC_REQUIRE(retained.size() == 2);
}

ZC_TEST("Source suppression retains UninitializedPlaceUse at a suppressed event") {
  OwnershipPipelineFixture fixture("let a = 0; let b = 1;"_zc);
  auto identity = syntheticIdentity(fixture);
  const auto event = identity.event;

  auto uninitialized = [&](uint32_t ordinal) {
    return UninitializedPlaceUseFailure{
        identity.owner,
        event,
        identity.span.clone(),
        syntheticPlace(identity),
        ordinal,
        causeVector(InitializationFailureCause{facts::InitializationLossKind::NeverInitialized,
                                               event, identity.span.clone()})};
  };
  auto twice = [&](uint32_t ordinal) {
    return LinearConsumedTwiceFailure{
        identity.owner,
        event,
        identity.span.clone(),
        syntheticPlace(identity),
        ordinal,
        causeVector(LinearConsumptionCause{event, identity.span.clone()})};
  };

  zc::Vector<OwnershipSourceFailure> failures;
  failures.add(twice(0));
  failures.add(uninitialized(1));

  auto retained = SourceSuppression::suppress(zc::mv(failures));
  ZC_REQUIRE(retained.size() == 2);
  ZC_EXPECT(retained[0].is<LinearConsumedTwiceFailure>());
  ZC_EXPECT(retained[1].is<UninitializedPlaceUseFailure>());
}

ZC_TEST("Source suppression retains every first-consumption cause on the primary") {
  OwnershipPipelineFixture fixture("let a = 0; let b = 1;"_zc);
  auto identity = syntheticIdentity(fixture);
  const auto event = identity.event;
  const MirEventKey firstConsumption{MirLocation{identity.owner, MirPoint::entry()}, 2};

  zc::Vector<LinearConsumptionCause> causes;
  causes.add(LinearConsumptionCause{event, identity.span.clone()});
  causes.add(LinearConsumptionCause{firstConsumption, identity.span.clone()});

  auto movedUse = [&](uint32_t ordinal) {
    return UseAfterMoveFailure{
        identity.owner,
        event,
        identity.span.clone(),
        syntheticPlace(identity),
        ordinal,
        causeVector(InitializationFailureCause{facts::InitializationLossKind::Moved, event,
                                               identity.span.clone()})};
  };

  zc::Vector<OwnershipSourceFailure> failures;
  failures.add(LinearConsumedTwiceFailure{identity.owner, event, identity.span.clone(),
                                          syntheticPlace(identity), 0, zc::mv(causes)});
  failures.add(movedUse(1));

  auto retained = SourceSuppression::suppress(zc::mv(failures));
  ZC_REQUIRE(retained.size() == 1);
  ZC_REQUIRE(retained[0].is<LinearConsumedTwiceFailure>());
  ZC_EXPECT(retained[0].get<LinearConsumedTwiceFailure>().causes.size() == 2);
}

ZC_TEST("Source suppression is a no-op without a suppressing primary") {
  OwnershipPipelineFixture fixture("let a = 0; let b = 1;"_zc);
  auto identity = syntheticIdentity(fixture);
  const auto event = identity.event;

  auto movedUse = [&](uint32_t ordinal) {
    return UseAfterMoveFailure{
        identity.owner,
        event,
        identity.span.clone(),
        syntheticPlace(identity),
        ordinal,
        causeVector(InitializationFailureCause{facts::InitializationLossKind::Moved, event,
                                               identity.span.clone()})};
  };
  auto uninitialized = [&](uint32_t ordinal) {
    return UninitializedPlaceUseFailure{
        identity.owner,
        event,
        identity.span.clone(),
        syntheticPlace(identity),
        ordinal,
        causeVector(InitializationFailureCause{facts::InitializationLossKind::NeverInitialized,
                                               event, identity.span.clone()})};
  };

  zc::Vector<OwnershipSourceFailure> failures;
  failures.add(movedUse(0));
  failures.add(uninitialized(1));

  auto retained = SourceSuppression::suppress(zc::mv(failures));
  ZC_REQUIRE(retained.size() == 2);
}

namespace {

identity::SemanticTypeId internType(type::SemanticTypeStore& semanticTypes,
                                    type::semantic::TypeData&& data) {
  auto canonical = semanticTypes.canonicalizeClosed(zc::mv(data));
  ZC_REQUIRE(canonical.is<type::semantic::CanonicalTypeData>());
  auto result = semanticTypes.intern(zc::mv(canonical).get<type::semantic::CanonicalTypeData>());
  ZC_REQUIRE(result.is<type::SemanticTypeInterned>());
  return result.get<type::SemanticTypeInterned>().id;
}

identity::SemanticTypeId internRawPointer(type::SemanticTypeStore& semanticTypes,
                                          identity::SemanticTypeId pointee) {
  return internType(semanticTypes, type::semantic::TypeData(type::semantic::RawPointerTypeData{
                                       type::semantic::Mutability::Mutable, pointee}));
}

identity::SemanticTypeId internReference(type::SemanticTypeStore& semanticTypes,
                                         identity::SemanticTypeId referent) {
  return internType(semanticTypes, type::semantic::TypeData(type::semantic::ReferenceTypeData{
                                       type::semantic::Mutability::Const, referent}));
}

identity::SemanticTypeId internPrimitive(type::SemanticTypeStore& semanticTypes,
                                         type::semantic::PrimitiveKind kind) {
  return internType(semanticTypes,
                    type::semantic::TypeData(type::semantic::PrimitiveTypeData{kind}));
}

}  // namespace

ZC_TEST("Ownership event overlay verifier rejects a forged raw-to-reference cast plan") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);

  auto overlayInput = fixture.overlayInput();
  auto& semanticTypes = overlayInput.body.semanticTypes;
  const auto primitiveId = internPrimitive(semanticTypes, type::semantic::PrimitiveKind::I32);
  const auto rawPointerId = internRawPointer(semanticTypes, primitiveId);
  const auto referenceId = internReference(semanticTypes, primitiveId);

  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].slots.size() > 0);
  const auto eventKey = candidate.functions[0].slots[0].key;

  zc::Vector<CastResourceRoute> routes;
  candidate.functions[0].castResourcePlans.add(
      VerifiedCastResourcePlanFact{CastCarrierKey{eventKey}, checker::checked::CastMode::Guaranteed,
                                   checker::checked::CastKind::RawPointerReinterpret, rawPointerId,
                                   referenceId, referenceId, eventKey, eventKey, zc::mv(routes)});

  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() == ir::IrFailureKind::InvalidFact);
}

ZC_TEST(
    "Ownership event overlay verifier rejects a forged raw-to-reference cast plan on result type") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);

  auto overlayInput = fixture.overlayInput();
  auto& semanticTypes = overlayInput.body.semanticTypes;
  const auto primitiveId = internPrimitive(semanticTypes, type::semantic::PrimitiveKind::I32);
  const auto rawPointerId = internRawPointer(semanticTypes, primitiveId);
  const auto referenceId = internReference(semanticTypes, primitiveId);

  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].slots.size() > 0);
  const auto eventKey = candidate.functions[0].slots[0].key;

  // Target is a raw pointer (not reference), but result is a reference.
  zc::Vector<CastResourceRoute> routes;
  candidate.functions[0].castResourcePlans.add(
      VerifiedCastResourcePlanFact{CastCarrierKey{eventKey}, checker::checked::CastMode::Guaranteed,
                                   checker::checked::CastKind::RawPointerReinterpret, rawPointerId,
                                   rawPointerId, referenceId, eventKey, eventKey, zc::mv(routes)});

  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  ZC_REQUIRE(verifiedResult.isIrInvariantRejected());
  ZC_EXPECT(verifiedResult.invariantFailures().facts().size() == 1);
  ZC_EXPECT(verifiedResult.invariantFailures().facts()[0].kind() == ir::IrFailureKind::InvalidFact);
}

}  // namespace
}  // namespace zomlang::compiler::ownership
