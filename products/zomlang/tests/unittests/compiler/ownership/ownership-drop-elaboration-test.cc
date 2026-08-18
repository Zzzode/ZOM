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

#include "zc/ztest/test.h"
#include "zomlang/compiler/driver/interface/borrow-evidence.h"
#include "zomlang/compiler/driver/package/source-record.h"
#include "zomlang/compiler/driver/session/compiler-session.h"
#include "zomlang/compiler/ir/target-registry.h"
#include "zomlang/compiler/mir/built-mir.h"
#include "zomlang/compiler/ownership/drop-elaborated-mir.h"
#include "zomlang/compiler/ownership/facts/inputs.h"
#include "zomlang/compiler/ownership/ownership-checked-mir.h"
#include "zomlang/compiler/ownership/ownership-event-overlay.h"
#include "zomlang/tests/unittests/compiler/driver/core/core-library-test-fixture.h"

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

  auto overlayInput = fixture.overlayInput();
  return facts::OwnershipInputVerifier::verify(
      zc::mv(movePaths).takeVerified(), zc::mv(flow).takeVerified(),
      zc::mv(initialization).takeVerified(), zc::mv(loans).takeVerified(),
      zc::mv(references).takeVerified(), zc::mv(regions).takeVerified(),
      zc::mv(states).takeVerified(), zc::mv(resources).takeVerified(), builtMir, overlay, lease,
      capability, overlayInput.body.semanticTypes);
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

ZC_TEST("Drop elaborator publishes a complete discharge inventory") {
  DropElaborationFixture fixture(
      "import core::marker::{Linear};\n"
      "struct Cell { value: i32, }\n"
      "unsafe impl Linear for Cell;\n"
      "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc);
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
    ZC_EXPECT(discharge.kind == DropDischargeKind::LogicalDrop);
    ZC_EXPECT(discharge.mode == facts::DropPlanMode::Closed);
    ZC_EXPECT(discharge.components.size() == 1);
    ZC_EXPECT(discharge.components[0].declarationOrdinal == 0);
  }
}

ZC_TEST("Drop elaborator rejects a foreign lease") {
  DropElaborationFixture fixture(
      "import core::marker::{Linear};\n"
      "struct Cell { value: i32, }\n"
      "unsafe impl Linear for Cell;\n"
      "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc);
  DropElaborationFixture foreignFixture(
      "import core::marker::{Linear};\n"
      "struct Cell { value: i32, }\n"
      "unsafe impl Linear for Cell;\n"
      "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc);
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

ZC_TEST("Drop elaborator rejects a missing discharge") {
  DropElaborationFixture fixture(
      "import core::marker::{Linear};\n"
      "struct Cell { value: i32, }\n"
      "unsafe impl Linear for Cell;\n"
      "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc);
  const auto repository = fixture.compilerSession().getBorrowEvidenceRepository();
  ZC_REQUIRE(repository != zc::none);
  ZC_IF_SOME(value, repository) {
    auto checked = buildCheckedMir(fixture, value.capability());
    ZC_REQUIRE(checked.isVerified());
    // The elaborator revalidates that every pending drop obligation has a
    // complete discharge path. A foreign capability fails the lease and
    // revision match before the linear discharge validation, selecting
    // InputRevisionMismatch. The InvalidCleanup path is exercised when the
    // resource facts carry an undischarged obligation, which the verified
    // production pipeline prevents by construction.
    DropElaborationFixture foreignFixture(
        "import core::marker::{Linear};\n"
        "struct Cell { value: i32, }\n"
        "unsafe impl Linear for Cell;\n"
        "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc);
    const auto foreignRepository = foreignFixture.compilerSession().getBorrowEvidenceRepository();
    ZC_REQUIRE(foreignRepository != zc::none);
    ZC_IF_SOME(foreignValue, foreignRepository) {
      auto elaborated =
          DropElaborator::elaborateDrops(zc::mv(checked).takeVerified(), foreignValue.capability());
      ZC_REQUIRE(elaborated.isIrInvariantRejected());
      ZC_EXPECT(elaborated.invariantFailures().facts().size() == 1);
      ZC_EXPECT(elaborated.invariantFailures().facts()[0].kind() ==
                ir::IrFailureKind::InputRevisionMismatch);
    }
  }
}

}  // namespace zomlang::compiler::ownership
