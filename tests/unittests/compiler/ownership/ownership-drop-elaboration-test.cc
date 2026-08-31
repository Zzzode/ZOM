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
// See the License for the specific language governing permissions and
// limitations under the License.

#include "compiler/driver/interface/borrow-evidence.h"
#include "compiler/driver/package/source-record.h"
#include "compiler/driver/session/compiler-session.h"
#include "compiler/identity/source-snapshot.h"
#include "compiler/ir/target-registry.h"
#include "compiler/mir/built-mir.h"
#include "compiler/ownership/drop-elaborated-mir.h"
#include "compiler/ownership/facts/init.h"
#include "compiler/ownership/facts/inputs.h"
#include "compiler/ownership/facts/resources.h"
#include "compiler/ownership/ownership-checked-mir.h"
#include "compiler/ownership/ownership-event-overlay.h"
#include "tests/unittests/compiler/driver/core/core-library-test-fixture.h"
#include "tests/unittests/compiler/test-semantic-identities.h"
#include "tests/unittests/compiler/test-semantic-type-context.h"
#include "zc/ztest/test.h"

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

class DropElaborationFixture final {
public:
  explicit DropElaborationFixture(zc::StringPtr sourceText)
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

ir::IrOperationResult<facts::VerifiedOwnershipInputs> verifyOwnershipInputs(
    const DropElaborationFixture& fixture,
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

ir::IrOperationResult<OwnershipCheckedMir> buildCheckedMir(
    const DropElaborationFixture& fixture,
    const driver::borrow_evidence::BorrowEvidenceRepositoryCapability& capability) {
  auto overlayInput = fixture.overlayInput();
  const mir::BuiltMirInput mirInput{overlayInput.hir, overlayInput.body};
  auto mirCandidate = mir::BuiltMirBuilder::build(mirInput);
  ZC_REQUIRE(mirCandidate.isVerified());
  auto verifiedMir = mir::BuiltMirVerifier::verify(zc::mv(mirCandidate).takeVerified(), mirInput);
  ZC_REQUIRE(verifiedMir.isVerified());
  auto builtMir = zc::mv(verifiedMir).takeVerified();

  auto overlayCandidate = OwnershipEventOverlayBuilder::build(overlayInput);
  ZC_REQUIRE(overlayCandidate.isVerified());
  auto verifiedOverlay =
      OwnershipEventOverlayVerifier::verify(zc::mv(overlayCandidate).takeVerified(), overlayInput);
  ZC_REQUIRE(verifiedOverlay.isVerified());
  auto overlay = zc::mv(verifiedOverlay).takeVerified();

  const auto& lease = builtMir.borrowEvidenceLease();
  auto inputs = verifyOwnershipInputs(fixture, lease, capability);
  ZC_REQUIRE(inputs.isVerified());

  return OwnershipFinalizer::finalizeOwnership(zc::mv(builtMir), zc::mv(overlay),
                                               zc::mv(inputs).takeVerified(), capability,
                                               overlayInput.body.semanticTypes);
}

}  // namespace

namespace {

zc::StringPtr linearReturnSource() {
  return "import core::marker::{Copy, Linear};\n"
         "struct Cell { value: i32, }\n"
         "impl !Copy for Cell;\n"
         "unsafe impl Linear for Cell;\n"
         "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc;
}

zc::StringPtr linearMoveReturnSource() {
  return "import core::marker::{Copy, Linear};\n"
         "struct Cell { value: i32, }\n"
         "impl !Copy for Cell;\n"
         "unsafe impl Linear for Cell;\n"
         "fun entry() -> Cell { let first = Cell { value: 0 }; let second = first; return second; }"_zc;
}

}  // namespace

ZC_TEST("Drop elaborator classifies a returned linear resource as ReturnTransfer") {
  DropElaborationFixture fixture(linearReturnSource());
  const auto repository = fixture.compilerSession().getBorrowEvidenceRepository();
  ZC_REQUIRE(repository != zc::none);
  ZC_IF_SOME(value, repository) {
    auto checked = buildCheckedMir(fixture, value.capability());
    ZC_REQUIRE(checked.isVerified());
    auto elaborated =
        DropElaborator::elaborateDrops(zc::mv(checked).takeVerified(), value.capability());
    ZC_REQUIRE(elaborated.isVerified());
    auto result = zc::mv(elaborated).takeVerified();
    ZC_EXPECT(result.discharges().size() == 1);
    const auto& discharge = result.discharges()[0];
    ZC_EXPECT(discharge.kind == DropDischargeKind::ReturnTransfer);
    ZC_EXPECT(discharge.mode == facts::DropPlanMode::Closed);
    ZC_EXPECT(discharge.components.size() == 1);
    ZC_EXPECT(discharge.components[0].declarationOrdinal == 0);
    ZC_REQUIRE(discharge.linearConsume != zc::none);
    ZC_IF_SOME(consume, discharge.linearConsume) {
      ZC_EXPECT(consume.kind == facts::LinearConsumptionKind::Return);
      ZC_EXPECT(consume.event.location.point.kind() == MirPointKind::BeforeTerminator);
    }
  }
}

ZC_TEST("Drop elaborator classifies a moved-then-returned linear resource as ReturnTransfer") {
  DropElaborationFixture fixture(linearMoveReturnSource());
  const auto repository = fixture.compilerSession().getBorrowEvidenceRepository();
  ZC_REQUIRE(repository != zc::none);
  ZC_IF_SOME(value, repository) {
    auto checked = buildCheckedMir(fixture, value.capability());
    ZC_REQUIRE(checked.isVerified());
    auto elaborated =
        DropElaborator::elaborateDrops(zc::mv(checked).takeVerified(), value.capability());
    ZC_REQUIRE(elaborated.isVerified());
    auto result = zc::mv(elaborated).takeVerified();
    ZC_EXPECT(result.discharges().size() >= 1);
    for (const auto& discharge : result.discharges()) {
      ZC_EXPECT(discharge.kind == DropDischargeKind::ReturnTransfer);
      ZC_EXPECT(discharge.mode == facts::DropPlanMode::Closed);
      ZC_REQUIRE(discharge.linearConsume != zc::none);
      ZC_IF_SOME(consume, discharge.linearConsume) {
        ZC_EXPECT(consume.kind == facts::LinearConsumptionKind::Return);
      }
    }
  }
}

ZC_TEST("Drop elaborator rejects a foreign lease") {
  DropElaborationFixture fixture(linearReturnSource());
  DropElaborationFixture foreignFixture(linearReturnSource());
  const auto repository = fixture.compilerSession().getBorrowEvidenceRepository();
  const auto foreignRepository = foreignFixture.compilerSession().getBorrowEvidenceRepository();
  ZC_REQUIRE(repository != zc::none);
  ZC_REQUIRE(foreignRepository != zc::none);
  ZC_IF_SOME(value, repository) {
    ZC_IF_SOME(foreignValue, foreignRepository) {
      auto checked = buildCheckedMir(fixture, value.capability());
      ZC_REQUIRE(checked.isVerified());
      auto elaborated =
          DropElaborator::elaborateDrops(zc::mv(checked).takeVerified(), foreignValue.capability());
      ZC_REQUIRE(elaborated.isIrInvariantRejected());
      ZC_EXPECT(elaborated.invariantFailures().facts().size() == 1);
      ZC_EXPECT(elaborated.invariantFailures().facts()[0].kind() ==
                ir::IrFailureKind::InputRevisionMismatch);
    }
  }
}

ZC_TEST("Coroutine elaborator publishes a wrapper with the discharge inventory") {
  DropElaborationFixture fixture(linearReturnSource());
  const auto repository = fixture.compilerSession().getBorrowEvidenceRepository();
  ZC_REQUIRE(repository != zc::none);
  ZC_IF_SOME(value, repository) {
    auto checked = buildCheckedMir(fixture, value.capability());
    ZC_REQUIRE(checked.isVerified());
    auto elaborated =
        DropElaborator::elaborateDrops(zc::mv(checked).takeVerified(), value.capability());
    ZC_REQUIRE(elaborated.isVerified());
    auto coroutineElaborated = CoroutineElaborator::elaborateCoroutines(
        zc::mv(elaborated).takeVerified(), value.capability());
    ZC_REQUIRE(coroutineElaborated.isVerified());
    auto result = zc::mv(coroutineElaborated).takeVerified();
    ZC_EXPECT(result.discharges().size() == 1);
    ZC_EXPECT(result.discharges()[0].kind == DropDischargeKind::ReturnTransfer);
    ZC_EXPECT(result.module() == fixture.builtMir().module());
  }
}

ZC_TEST("Coroutine elaborator rejects a foreign lease") {
  DropElaborationFixture fixture(linearReturnSource());
  DropElaborationFixture foreignFixture(linearReturnSource());
  const auto repository = fixture.compilerSession().getBorrowEvidenceRepository();
  const auto foreignRepository = foreignFixture.compilerSession().getBorrowEvidenceRepository();
  ZC_REQUIRE(repository != zc::none);
  ZC_REQUIRE(foreignRepository != zc::none);
  ZC_IF_SOME(value, repository) {
    ZC_IF_SOME(foreignValue, foreignRepository) {
      auto checked = buildCheckedMir(fixture, value.capability());
      ZC_REQUIRE(checked.isVerified());
      auto elaborated =
          DropElaborator::elaborateDrops(zc::mv(checked).takeVerified(), value.capability());
      ZC_REQUIRE(elaborated.isVerified());
      auto coroutineElaborated = CoroutineElaborator::elaborateCoroutines(
          zc::mv(elaborated).takeVerified(), foreignValue.capability());
      ZC_REQUIRE(coroutineElaborated.isIrInvariantRejected());
      ZC_EXPECT(coroutineElaborated.invariantFailures().facts().size() == 1);
      ZC_EXPECT(coroutineElaborated.invariantFailures().facts()[0].kind() ==
                ir::IrFailureKind::InputRevisionMismatch);
    }
  }
}

ZC_TEST("Executable mir verifier certifies cleanup consumption") {
  DropElaborationFixture fixture(linearReturnSource());
  const auto repository = fixture.compilerSession().getBorrowEvidenceRepository();
  ZC_REQUIRE(repository != zc::none);
  ZC_IF_SOME(value, repository) {
    auto checked = buildCheckedMir(fixture, value.capability());
    ZC_REQUIRE(checked.isVerified());
    auto elaborated =
        DropElaborator::elaborateDrops(zc::mv(checked).takeVerified(), value.capability());
    ZC_REQUIRE(elaborated.isVerified());
    auto coroutineElaborated = CoroutineElaborator::elaborateCoroutines(
        zc::mv(elaborated).takeVerified(), value.capability());
    ZC_REQUIRE(coroutineElaborated.isVerified());
    auto verified = ExecutableMirVerifier::verifyExecutableMir(
        zc::mv(coroutineElaborated).takeVerified(), value.capability());
    ZC_REQUIRE(verified.isVerified());
    auto result = zc::mv(verified).takeVerified();
    ZC_EXPECT(result.discharges().size() == 1);
    ZC_EXPECT(result.discharges()[0].kind == DropDischargeKind::ReturnTransfer);
    ZC_EXPECT(result.module() == fixture.builtMir().module());
  }
}

ZC_TEST("Executable mir verifier certifies a moved-then-returned cleanup") {
  DropElaborationFixture fixture(linearMoveReturnSource());
  const auto repository = fixture.compilerSession().getBorrowEvidenceRepository();
  ZC_REQUIRE(repository != zc::none);
  ZC_IF_SOME(value, repository) {
    auto checked = buildCheckedMir(fixture, value.capability());
    ZC_REQUIRE(checked.isVerified());
    auto elaborated =
        DropElaborator::elaborateDrops(zc::mv(checked).takeVerified(), value.capability());
    ZC_REQUIRE(elaborated.isVerified());
    auto coroutineElaborated = CoroutineElaborator::elaborateCoroutines(
        zc::mv(elaborated).takeVerified(), value.capability());
    ZC_REQUIRE(coroutineElaborated.isVerified());
    auto verified = ExecutableMirVerifier::verifyExecutableMir(
        zc::mv(coroutineElaborated).takeVerified(), value.capability());
    ZC_REQUIRE(verified.isVerified());
    auto result = zc::mv(verified).takeVerified();
    ZC_EXPECT(result.discharges().size() >= 1);
    for (const auto& discharge : result.discharges()) {
      ZC_EXPECT(discharge.kind == DropDischargeKind::ReturnTransfer);
    }
  }
}

ZC_TEST("Executable mir verifier rejects a foreign lease") {
  DropElaborationFixture fixture(linearReturnSource());
  DropElaborationFixture foreignFixture(linearReturnSource());
  const auto repository = fixture.compilerSession().getBorrowEvidenceRepository();
  const auto foreignRepository = foreignFixture.compilerSession().getBorrowEvidenceRepository();
  ZC_REQUIRE(repository != zc::none);
  ZC_REQUIRE(foreignRepository != zc::none);
  ZC_IF_SOME(value, repository) {
    ZC_IF_SOME(foreignValue, foreignRepository) {
      auto checked = buildCheckedMir(fixture, value.capability());
      ZC_REQUIRE(checked.isVerified());
      auto elaborated =
          DropElaborator::elaborateDrops(zc::mv(checked).takeVerified(), value.capability());
      ZC_REQUIRE(elaborated.isVerified());
      auto coroutineElaborated = CoroutineElaborator::elaborateCoroutines(
          zc::mv(elaborated).takeVerified(), value.capability());
      ZC_REQUIRE(coroutineElaborated.isVerified());
      auto verified = ExecutableMirVerifier::verifyExecutableMir(
          zc::mv(coroutineElaborated).takeVerified(), foreignValue.capability());
      ZC_REQUIRE(verified.isIrInvariantRejected());
      ZC_EXPECT(verified.invariantFailures().facts().size() == 1);
      ZC_EXPECT(verified.invariantFailures().facts()[0].kind() ==
                ir::IrFailureKind::InputRevisionMismatch);
    }
  }
}

ZC_TEST("Session publishes verified executable mir modules after checkSources") {
  DropElaborationFixture fixture(linearReturnSource());
  const auto& session = fixture.compilerSession();
  const auto executables = session.getVerifiedExecutableMirModules();
  ZC_EXPECT(executables.size() == 1);
  if (executables.size() == 1) {
    ZC_EXPECT(executables[0].discharges().size() == 1);
    ZC_EXPECT(executables[0].discharges()[0].kind == DropDischargeKind::ReturnTransfer);
    ZC_EXPECT(executables[0].module() == fixture.builtMir().module());
  }
}

// ---------------------------------------------------------------------------
// Multi-block cleanup graph (hand-built MIR + facts, no session)
// ---------------------------------------------------------------------------
//
// These cases drive the pure DropElaborator::computeDischarges seam directly.
// A sealed OwnershipCheckedMir cannot be hand-forged (it needs the full
// verified lease/evidence lineage), so the discharge algorithm is exercised on
// hand-built resource facts, MIR functions, and initialization functions. The
// scope is the MIR-level DropDischargeRecord inventory only.

namespace {

identity::SourceSpan handSpan() {
  auto snapshot = identity::ImmutableSourceSnapshot::from(tests::test_identity_detail::source(),
                                                          zc::heapArray<uint8_t>(8, uint8_t{0}));
  ZC_REQUIRE(snapshot != zc::none);
  ZC_IF_SOME(value, snapshot) {
    auto span = value.span(1, 7);
    ZC_IF_SOME(admitted, span) { return zc::mv(admitted); }
  }
  ZC_FAIL_REQUIRE("invalid drop-elaboration hand-built source span");
}

mir::MirBlockId handBlockId(uint32_t ordinal) {
  return ZC_REQUIRE_NONNULL(mir::MirBlockId::fromOrdinal(ordinal));
}

mir::MirBasicBlock handBlock(mir::MirBlockId id, mir::MirTerminator&& terminator) {
  return mir::MirBasicBlock{id, mir::MirSourceScopeId{}, zc::Vector<mir::MirStatement>{},
                            zc::mv(terminator)};
}

mir::MirTerminator handReturn() { return mir::MirTerminator::returnVoid(handSpan()); }

mir::MirTerminator handGoto(mir::MirBlockId target) {
  return mir::MirTerminator::gotoTarget(target, handSpan());
}

mir::MirTerminator handSwitchInt(mir::MirBlockId trueTarget, mir::MirBlockId defaultTarget) {
  const auto type = tests::testSemanticType();
  auto discriminant =
      mir::MirOperand::constant(type, checker::checked::CanonicalConstValue::boolean(true));
  zc::Vector<mir::MirSwitchIntArm> arms;
  arms.add(mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), trueTarget});
  return mir::MirTerminator::switchInt(zc::mv(discriminant), zc::mv(arms), defaultTarget,
                                       handSpan());
}

mir::MirFunction handFunction(identity::DefId owner, zc::Vector<mir::MirBasicBlock>&& blocks) {
  return mir::MirFunction{owner,
                          mir::MirFunctionKind::Function,
                          identity::DefinitionKind::Function,
                          tests::testSemanticType(),
                          handSpan(),
                          zc::Vector<mir::MirSourceScope>{},
                          zc::Vector<mir::MirLocalDeclaration>{},
                          zc::mv(blocks)};
}

// Builds the SwitchInt diamond A1 emits: bb1 switches to bb2/bb3, both Goto
// bb4, bb4 returns. Both bb2 and bb3 are also acceptable as direct-return arms
// (used for the returned-on-both-arms case) via the returnBranches flag.
mir::MirFunction handDiamond(identity::DefId owner, bool returnBranches) {
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(handBlock(handBlockId(1), handSwitchInt(handBlockId(2), handBlockId(3))));
  if (returnBranches) {
    blocks.add(handBlock(handBlockId(2), handReturn()));
    blocks.add(handBlock(handBlockId(3), handReturn()));
  } else {
    blocks.add(handBlock(handBlockId(2), handGoto(handBlockId(4))));
    blocks.add(handBlock(handBlockId(3), handGoto(handBlockId(4))));
    blocks.add(handBlock(handBlockId(4), handReturn()));
  }
  return handFunction(owner, zc::mv(blocks));
}

mir::MirPlace handPlace(uint32_t local) {
  zc::Vector<mir::MirProjection> projections;
  return mir::MirPlace(ZC_REQUIRE_NONNULL(mir::MirLocalId::fromOrdinal(local)),
                       identity::SemanticTypeId(), zc::mv(projections), identity::SemanticTypeId());
}

facts::MovePathKey handMovePath(identity::DefId owner, uint32_t local) {
  return facts::MovePathKey{owner, handPlace(local)};
}

MirEventKey handEvent(identity::DefId owner, MirPoint&& point) {
  return MirEventKey{MirLocation{owner, zc::mv(point)}, 0};
}

facts::DropResourceSubject handSubject(identity::DefId owner, uint32_t local, MirEventKey intro) {
  return facts::DropResourceSubject{intro, handMovePath(owner, local), identity::SemanticTypeId()};
}

}  // namespace

// A linear resource created in the entry block and returned on both arms of a
// SwitchInt diamond yields one ReturnTransfer discharge per return, each bound
// to its own block's before-terminator cutpoint.
ZC_TEST("Drop elaborator fans one ReturnTransfer per branch return") {
  const auto owner = tests::testDefinition(0);
  auto function = handDiamond(owner, /*returnBranches=*/true);

  const auto intro = handEvent(owner, MirPoint::beforeStatement(handBlockId(1), 0));
  const auto returnB2 = handEvent(owner, MirPoint::beforeTerminator(handBlockId(2)));
  const auto returnB3 = handEvent(owner, MirPoint::beforeTerminator(handBlockId(3)));

  facts::OwnershipResourceFunction resourceFunction;
  resourceFunction.owner = owner;
  resourceFunction.facts.add(facts::OwnershipResourceFact{
      handSubject(owner, 1, intro), facts::DropRequirement::Linear, zc::none, 0});
  zc::Vector<facts::DropPlanComponent> components;
  components.add(facts::DropPlanComponent{0, zc::none});
  resourceFunction.dropPlans.add(facts::DropPlan{handSubject(owner, 1, intro),
                                                 facts::DropPlanMode::Closed, zc::mv(components)});
  zc::Vector<facts::LinearConsumption> consumptions;
  consumptions.add(facts::LinearConsumption{handMovePath(owner, 1), returnB2,
                                            facts::LinearConsumptionKind::Return});
  consumptions.add(facts::LinearConsumption{handMovePath(owner, 1), returnB3,
                                            facts::LinearConsumptionKind::Return});
  resourceFunction.linearObligations.add(facts::LinearObligationFact{
      facts::LinearObligationKey{intro, handMovePath(owner, 1)}, identity::SemanticTypeId(),
      zc::Vector<facts::LinearTransfer>{}, zc::mv(consumptions)});

  zc::Vector<facts::OwnershipResourceFunction> resources;
  resources.add(zc::mv(resourceFunction));
  zc::Vector<mir::MirFunction> functions;
  functions.add(zc::mv(function));
  zc::Vector<facts::InitializationFunction> initialization;

  auto discharges = DropElaborator::computeDischarges(resources.asPtr(), functions.asPtr(),
                                                      initialization.asPtr());
  ZC_REQUIRE(discharges != zc::none);
  ZC_IF_SOME(records, discharges) {
    ZC_REQUIRE(records.size() == 2);
    ZC_EXPECT(records[0].kind == DropDischargeKind::ReturnTransfer);
    ZC_EXPECT(records[1].kind == DropDischargeKind::ReturnTransfer);
    ZC_EXPECT(records[0].event == returnB2);
    ZC_EXPECT(records[1].event == returnB3);
    ZC_EXPECT(records[0].event != records[1].event);
    ZC_REQUIRE(records[0].linearConsume != zc::none);
    ZC_REQUIRE(records[1].linearConsume != zc::none);
  }
}

// A linear resource returned on one arm and consumed by a call on the other
// yields one ReturnTransfer and one ConsumingCallTransfer with distinct events.
ZC_TEST("Drop elaborator fans a mixed return and consuming-call cleanup") {
  const auto owner = tests::testDefinition(0);
  auto function = handDiamond(owner, /*returnBranches=*/true);

  const auto intro = handEvent(owner, MirPoint::beforeStatement(handBlockId(1), 0));
  const auto returnB2 = handEvent(owner, MirPoint::beforeTerminator(handBlockId(2)));
  const auto callB3 = handEvent(owner, MirPoint::beforeTerminator(handBlockId(3)));

  facts::OwnershipResourceFunction resourceFunction;
  resourceFunction.owner = owner;
  resourceFunction.facts.add(facts::OwnershipResourceFact{
      handSubject(owner, 1, intro), facts::DropRequirement::Linear, zc::none, 0});
  zc::Vector<facts::DropPlanComponent> components;
  components.add(facts::DropPlanComponent{0, zc::none});
  resourceFunction.dropPlans.add(facts::DropPlan{handSubject(owner, 1, intro),
                                                 facts::DropPlanMode::Closed, zc::mv(components)});
  zc::Vector<facts::LinearConsumption> consumptions;
  consumptions.add(facts::LinearConsumption{handMovePath(owner, 1), returnB2,
                                            facts::LinearConsumptionKind::Return});
  consumptions.add(facts::LinearConsumption{handMovePath(owner, 1), callB3,
                                            facts::LinearConsumptionKind::ConsumingCall});
  resourceFunction.linearObligations.add(facts::LinearObligationFact{
      facts::LinearObligationKey{intro, handMovePath(owner, 1)}, identity::SemanticTypeId(),
      zc::Vector<facts::LinearTransfer>{}, zc::mv(consumptions)});

  zc::Vector<facts::OwnershipResourceFunction> resources;
  resources.add(zc::mv(resourceFunction));
  zc::Vector<mir::MirFunction> functions;
  functions.add(zc::mv(function));
  zc::Vector<facts::InitializationFunction> initialization;

  auto discharges = DropElaborator::computeDischarges(resources.asPtr(), functions.asPtr(),
                                                      initialization.asPtr());
  ZC_REQUIRE(discharges != zc::none);
  ZC_IF_SOME(records, discharges) {
    ZC_REQUIRE(records.size() == 2);
    ZC_EXPECT(records[0].kind == DropDischargeKind::ReturnTransfer);
    ZC_EXPECT(records[1].kind == DropDischargeKind::ConsumingCallTransfer);
    ZC_EXPECT(records[0].event != records[1].event);
  }
}

// A single-block linear return still yields exactly one ReturnTransfer through
// the pure seam: the multi-block generalization preserves the linear case.
ZC_TEST("Drop elaborator computeDischarges yields one record for a single-block return") {
  const auto owner = tests::testDefinition(0);
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(handBlock(handBlockId(1), handReturn()));
  auto function = handFunction(owner, zc::mv(blocks));

  const auto intro = handEvent(owner, MirPoint::beforeStatement(handBlockId(1), 0));
  const auto ret = handEvent(owner, MirPoint::beforeTerminator(handBlockId(1)));

  facts::OwnershipResourceFunction resourceFunction;
  resourceFunction.owner = owner;
  resourceFunction.facts.add(facts::OwnershipResourceFact{
      handSubject(owner, 1, intro), facts::DropRequirement::Linear, zc::none, 0});
  zc::Vector<facts::DropPlanComponent> components;
  components.add(facts::DropPlanComponent{0, zc::none});
  resourceFunction.dropPlans.add(facts::DropPlan{handSubject(owner, 1, intro),
                                                 facts::DropPlanMode::Closed, zc::mv(components)});
  zc::Vector<facts::LinearConsumption> consumptions;
  consumptions.add(
      facts::LinearConsumption{handMovePath(owner, 1), ret, facts::LinearConsumptionKind::Return});
  resourceFunction.linearObligations.add(facts::LinearObligationFact{
      facts::LinearObligationKey{intro, handMovePath(owner, 1)}, identity::SemanticTypeId(),
      zc::Vector<facts::LinearTransfer>{}, zc::mv(consumptions)});

  zc::Vector<facts::OwnershipResourceFunction> resources;
  resources.add(zc::mv(resourceFunction));
  zc::Vector<mir::MirFunction> functions;
  functions.add(zc::mv(function));
  zc::Vector<facts::InitializationFunction> initialization;

  auto discharges = DropElaborator::computeDischarges(resources.asPtr(), functions.asPtr(),
                                                      initialization.asPtr());
  ZC_REQUIRE(discharges != zc::none);
  ZC_IF_SOME(records, discharges) {
    ZC_REQUIRE(records.size() == 1);
    ZC_EXPECT(records[0].kind == DropDischargeKind::ReturnTransfer);
  }
}

// A purely logical resource (no linear obligation) initialized at both return
// exits of a diamond yields one LogicalDrop discharge per normal-return exit.
ZC_TEST("Drop elaborator emits a logical drop at each initialized return exit") {
  const auto owner = tests::testDefinition(0);
  auto function = handDiamond(owner, /*returnBranches=*/true);

  const auto intro = handEvent(owner, MirPoint::beforeStatement(handBlockId(1), 0));

  facts::OwnershipResourceFunction resourceFunction;
  resourceFunction.owner = owner;
  resourceFunction.facts.add(facts::OwnershipResourceFact{
      handSubject(owner, 1, intro), facts::DropRequirement::Logical, zc::none, 0});
  zc::Vector<facts::DropPlanComponent> components;
  components.add(facts::DropPlanComponent{0, zc::none});
  resourceFunction.dropPlans.add(facts::DropPlan{handSubject(owner, 1, intro),
                                                 facts::DropPlanMode::Closed, zc::mv(components)});

  // Initialization facts mark the subject initialized at both return exits.
  facts::InitializationFunction initFunction;
  initFunction.owner = owner;
  initFunction.facts.add(facts::InitializationFact{
      MirPoint::exit(handBlockId(2), MirExitKind::Return), handMovePath(owner, 1),
      facts::InitializationState::initialized(), zc::Vector<facts::InitializationLossCause>{}});
  initFunction.facts.add(facts::InitializationFact{
      MirPoint::exit(handBlockId(3), MirExitKind::Return), handMovePath(owner, 1),
      facts::InitializationState::initialized(), zc::Vector<facts::InitializationLossCause>{}});

  zc::Vector<facts::OwnershipResourceFunction> resources;
  resources.add(zc::mv(resourceFunction));
  zc::Vector<mir::MirFunction> functions;
  functions.add(zc::mv(function));
  zc::Vector<facts::InitializationFunction> initialization;
  initialization.add(zc::mv(initFunction));

  auto discharges = DropElaborator::computeDischarges(resources.asPtr(), functions.asPtr(),
                                                      initialization.asPtr());
  ZC_REQUIRE(discharges != zc::none);
  ZC_IF_SOME(records, discharges) {
    ZC_REQUIRE(records.size() == 2);
    ZC_EXPECT(records[0].kind == DropDischargeKind::LogicalDrop);
    ZC_EXPECT(records[1].kind == DropDischargeKind::LogicalDrop);
    ZC_EXPECT(records[0].linearConsume == zc::none);
    ZC_EXPECT(records[1].linearConsume == zc::none);
  }
}

// A purely logical resource moved out before every normal-return exit (its
// ownership transferred into the return value) is initialized at no exit, so it
// correctly discharges to zero LogicalDrop records. The plan is still complete:
// the drop obligation left with the move. This must not be rejected as a
// missing discharge. Regression for the elaborateLinear moved-out logical case.
ZC_TEST("Drop elaborator emits no logical drop when the subject is moved out at every return") {
  const auto owner = tests::testDefinition(0);
  auto function = handDiamond(owner, /*returnBranches=*/true);

  const auto intro = handEvent(owner, MirPoint::beforeStatement(handBlockId(1), 0));

  facts::OwnershipResourceFunction resourceFunction;
  resourceFunction.owner = owner;
  resourceFunction.facts.add(facts::OwnershipResourceFact{
      handSubject(owner, 1, intro), facts::DropRequirement::Logical, zc::none, 0});
  zc::Vector<facts::DropPlanComponent> components;
  components.add(facts::DropPlanComponent{0, zc::none});
  resourceFunction.dropPlans.add(facts::DropPlan{handSubject(owner, 1, intro),
                                                 facts::DropPlanMode::Closed, zc::mv(components)});

  // Initialization facts mark the subject NOT initialized at either return exit:
  // it was moved out into the return value before the exits.
  facts::InitializationFunction initFunction;
  initFunction.owner = owner;
  initFunction.facts.add(facts::InitializationFact{
      MirPoint::exit(handBlockId(2), MirExitKind::Return), handMovePath(owner, 1),
      facts::InitializationState::dead(), zc::Vector<facts::InitializationLossCause>{}});
  initFunction.facts.add(facts::InitializationFact{
      MirPoint::exit(handBlockId(3), MirExitKind::Return), handMovePath(owner, 1),
      facts::InitializationState::dead(), zc::Vector<facts::InitializationLossCause>{}});

  zc::Vector<facts::OwnershipResourceFunction> resources;
  resources.add(zc::mv(resourceFunction));
  zc::Vector<mir::MirFunction> functions;
  functions.add(zc::mv(function));
  zc::Vector<facts::InitializationFunction> initialization;
  initialization.add(zc::mv(initFunction));

  auto discharges = DropElaborator::computeDischarges(resources.asPtr(), functions.asPtr(),
                                                      initialization.asPtr());
  ZC_REQUIRE(discharges != zc::none);
  ZC_IF_SOME(records, discharges) { ZC_EXPECT(records.size() == 0); }
}

ZC_TEST("Drop elaborator rejects a missing discharge") {
  const auto owner = tests::testDefinition(0);
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(handBlock(handBlockId(1), handReturn()));
  auto function = handFunction(owner, zc::mv(blocks));
  const auto intro = handEvent(owner, MirPoint::beforeStatement(handBlockId(1), 0));

  facts::OwnershipResourceFunction resourceFunction;
  resourceFunction.owner = owner;
  resourceFunction.facts.add(facts::OwnershipResourceFact{
      handSubject(owner, 1, intro), facts::DropRequirement::Logical, zc::none, 0});
  // No DropPlan covers the pending resource fact, so completeness must fail.

  zc::Vector<facts::OwnershipResourceFunction> resources;
  resources.add(zc::mv(resourceFunction));
  zc::Vector<mir::MirFunction> functions;
  functions.add(zc::mv(function));
  zc::Vector<facts::InitializationFunction> initialization;

  auto discharges = DropElaborator::computeDischarges(resources.asPtr(), functions.asPtr(),
                                                      initialization.asPtr());
  ZC_EXPECT(discharges == zc::none);
}

// A Positive Linear obligation left with no consumption on any path has no
// return/call discharge; with no normal-return exit reachable it fails closed.
ZC_TEST("Drop elaborator rejects an unconsumed linear obligation with no exit") {
  const auto owner = tests::testDefinition(0);
  // A single Unreachable block: divergent exit, no normal return.
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(handBlock(handBlockId(1), mir::MirTerminator::unreachable(handSpan())));
  auto function = handFunction(owner, zc::mv(blocks));

  const auto intro = handEvent(owner, MirPoint::beforeStatement(handBlockId(1), 0));

  facts::OwnershipResourceFunction resourceFunction;
  resourceFunction.owner = owner;
  resourceFunction.facts.add(facts::OwnershipResourceFact{
      handSubject(owner, 1, intro), facts::DropRequirement::Linear, zc::none, 0});
  zc::Vector<facts::DropPlanComponent> components;
  components.add(facts::DropPlanComponent{0, zc::none});
  resourceFunction.dropPlans.add(facts::DropPlan{handSubject(owner, 1, intro),
                                                 facts::DropPlanMode::Closed, zc::mv(components)});
  // Obligation with an empty consumptions sequence and no normal-return exit.
  resourceFunction.linearObligations.add(facts::LinearObligationFact{
      facts::LinearObligationKey{intro, handMovePath(owner, 1)}, identity::SemanticTypeId(),
      zc::Vector<facts::LinearTransfer>{}, zc::Vector<facts::LinearConsumption>{}});

  zc::Vector<facts::OwnershipResourceFunction> resources;
  resources.add(zc::mv(resourceFunction));
  zc::Vector<mir::MirFunction> functions;
  functions.add(zc::mv(function));
  zc::Vector<facts::InitializationFunction> initialization;

  auto discharges = DropElaborator::computeDischarges(resources.asPtr(), functions.asPtr(),
                                                      initialization.asPtr());
  ZC_EXPECT(discharges == zc::none);
}

}  // namespace zomlang::compiler::ownership
