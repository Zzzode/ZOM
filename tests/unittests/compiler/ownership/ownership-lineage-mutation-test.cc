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
#include "compiler/diagnostics/consumer/diagnostic-consumer.h"
#include "compiler/diagnostics/core/diagnostic-engine.h"
#include "compiler/driver/interface/borrow-evidence.h"
#include "compiler/driver/package/manifest-parser.h"
#include "compiler/driver/package/source-record.h"
#include "compiler/driver/session/compiler-session.h"
#include "compiler/identity/canonical/canonical-encoder.h"
#include "compiler/identity/crypto/sha256.h"
#include "compiler/ir/ir-failure.h"
#include "compiler/ir/target-registry.h"
#include "compiler/mir/built-mir.h"
#include "compiler/ownership/facts/escape.h"
#include "compiler/ownership/facts/flow.h"
#include "compiler/ownership/facts/init.h"
#include "compiler/ownership/facts/inputs.h"
#include "compiler/ownership/facts/loans.h"
#include "compiler/ownership/facts/paths.h"
#include "compiler/ownership/facts/refs.h"
#include "compiler/ownership/facts/regions.h"
#include "compiler/ownership/facts/resources.h"
#include "compiler/ownership/facts/states.h"
#include "compiler/ownership/ownership-event-overlay.h"
#include "compiler/source/manager.h"
#include "tests/unittests/compiler/driver/core/core-library-test-fixture.h"

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

/// Fixture for sources the borrow-source verifier rejects (for example a
/// returned function-local borrow). checkSources() fails, so the Built MIR and
/// event overlay are read from the staged rejection view instead of the
/// published ownership-checked modules. This lets fact-derivation tests
/// reconstruct inventories for the only source shape that produces a local
/// loan.
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

const VerifiedOwnershipEventOverlay& sessionOverlay(const driver::CompilerSession& session) {
  const auto checkedMir = session.getOwnershipCheckedMirModules();
  ZC_REQUIRE(checkedMir.size() == 1);
  return checkedMir[0].eventOverlay();
}

/// Builds and independently verifies the region-membership inventory for one
/// fixture. The escape builder and verifier consume memberships to validate
/// Static proof containment, so every escape call site reconstructs them from
/// the same verified flow and loan inputs.
ir::IrOperationResult<facts::VerifiedRegionMemberships> buildVerifiedMemberships(
    const OwnershipPipelineFixture& fixture) {
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = sessionOverlay(session);
  const auto& inputs = ownershipInputs(session);
  auto candidate =
      facts::RegionMembershipBuilder::build(inputs.flow(), inputs.loans(), builtMir, overlay);
  ZC_REQUIRE(candidate.isVerified());
  return facts::RegionMembershipVerifier::verify(zc::mv(candidate).takeVerified(), inputs.flow(),
                                                 inputs.loans(), builtMir, overlay);
}

/// Asserts that a rejected ownership operation published exactly one invariant
/// failure of the expected kind and produced no verified ownership output.
template <typename Result>
void expectPublishedRejection(const Result& result, ir::IrFailureKind expected) {
  ZC_EXPECT(!result.isVerified());
  ZC_REQUIRE(result.isIrInvariantRejected());
  ZC_EXPECT(result.invariantFailures().facts().size() == 1);
  ZC_EXPECT(result.invariantFailures().facts()[0].kind() == expected);
}

/// Builds a reborrow-state candidate from the fixture, applies one lineage
/// tamper, and requires the independent verifier to reject it with an input
/// revision mismatch, publishing no ownership output.
template <typename Tamper>
void expectReborrowStateLineageRejection(const OwnershipPipelineFixture& fixture, Tamper&& tamper) {
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = sessionOverlay(session);
  const auto& inputs = ownershipInputs(session);

  auto candidateResult =
      facts::ReborrowStateBuilder::build(inputs.references(), inputs.regions(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.states.size() != 0);
  tamper(candidate, builtMir, overlay);

  auto verifiedResult = facts::ReborrowStateVerifier::verify(zc::mv(candidate), inputs.references(),
                                                             inputs.regions(), builtMir, overlay);
  expectPublishedRejection(verifiedResult, ir::IrFailureKind::InputRevisionMismatch);
}

// Shared lineage tamper operations. Every ownership candidate carries the same
// five lineage fields (plus a borrow-evidence revision on borrow-derived
// candidates), so the same tamper applies to every candidate type.

auto tamperSemanticContext = [](auto& candidate, const auto& builtMir, const auto&) {
  candidate.semanticContext = identity::SemanticContextBrand{};
  ZC_REQUIRE(candidate.semanticContext != builtMir.semanticContext());
};

auto tamperContextFingerprint = [](auto& candidate, const auto& builtMir, const auto&) {
  candidate.contextFingerprint =
      identity::ContextFingerprint::fromCanonicalDigest(identity::Sha256Digest{});
  ZC_REQUIRE(candidate.contextFingerprint.digest() != builtMir.contextFingerprint().digest());
};

auto tamperModuleIdentity = [](auto& candidate, const auto& builtMir, const auto&) {
  candidate.module = identity::ModuleId{};
  ZC_REQUIRE(candidate.module != builtMir.module());
};

auto tamperBuiltRevision = [](auto& candidate, const auto& builtMir, const auto&) {
  candidate.builtRevision = mir::MirRevisionId::fromDigest(identity::Sha256Digest{});
  ZC_REQUIRE(candidate.builtRevision.digest() != builtMir.revision().digest());
};

auto tamperOverlayRevision = [](auto& candidate, const auto&, const auto& overlay) {
  candidate.overlayRevision = OwnershipEventOverlayRevision::fromDigest(identity::Sha256Digest{});
  ZC_REQUIRE(candidate.overlayRevision.digest() != overlay.revision().digest());
};

/// Builds a move-path candidate from the fixture, applies one lineage tamper,
/// and requires the independent verifier to reject it with an input revision
/// mismatch.
template <typename Tamper>
void expectMovePathLineageRejection(const OwnershipPipelineFixture& fixture, Tamper&& tamper) {
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = sessionOverlay(fixture.compilerSession());

  auto candidateResult = facts::MovePathBuilder::build(builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() != 0);
  tamper(candidate, builtMir, overlay);

  auto verifiedResult = facts::MovePathVerifier::verify(zc::mv(candidate), builtMir, overlay);
  expectPublishedRejection(verifiedResult, ir::IrFailureKind::InputRevisionMismatch);
}

/// Builds a flow candidate from the fixture, applies one lineage tamper, and
/// requires the independent verifier to reject it with an input revision
/// mismatch.
template <typename Tamper>
void expectFlowLineageRejection(const OwnershipPipelineFixture& fixture, Tamper&& tamper) {
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = sessionOverlay(fixture.compilerSession());

  auto candidateResult = facts::FlowBuilder::build(builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() != 0);
  tamper(candidate, builtMir, overlay);

  auto verifiedResult = facts::FlowVerifier::verify(zc::mv(candidate), builtMir, overlay);
  expectPublishedRejection(verifiedResult, ir::IrFailureKind::InputRevisionMismatch);
}

/// Builds a loan candidate from the fixture, applies one lineage tamper, and
/// requires the independent verifier to reject it with an input revision
/// mismatch.
template <typename Tamper>
void expectLoanLineageRejection(const OwnershipPipelineFixture& fixture, Tamper&& tamper) {
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = sessionOverlay(session);
  const auto& inputs = ownershipInputs(session);

  auto candidateResult = facts::LoanBuilder::build(inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.loans.size() != 0);
  tamper(candidate, builtMir, overlay);

  auto verifiedResult =
      facts::LoanVerifier::verify(zc::mv(candidate), inputs.movePaths(), builtMir, overlay);
  expectPublishedRejection(verifiedResult, ir::IrFailureKind::InputRevisionMismatch);
}

/// Builds a reference-definition candidate from the fixture, applies one
/// lineage tamper, and requires the independent verifier to reject it with an
/// input revision mismatch.
template <typename Tamper>
void expectReferenceDefinitionLineageRejection(const OwnershipPipelineFixture& fixture,
                                               Tamper&& tamper) {
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = sessionOverlay(session);
  const auto& inputs = ownershipInputs(session);

  auto candidateResult = facts::ReferenceDefinitionBuilder::build(
      inputs.movePaths(), inputs.loans(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.definitions.size() != 0);
  tamper(candidate, builtMir, overlay);

  auto verifiedResult = facts::ReferenceDefinitionVerifier::verify(
      zc::mv(candidate), inputs.movePaths(), inputs.loans(), builtMir, overlay);
  expectPublishedRejection(verifiedResult, ir::IrFailureKind::InputRevisionMismatch);
}

/// Builds a reborrow-region candidate from the fixture, applies one lineage
/// tamper, and requires the independent verifier to reject it with an input
/// revision mismatch.
template <typename Tamper>
void expectReborrowRegionLineageRejection(const OwnershipPipelineFixture& fixture,
                                          Tamper&& tamper) {
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = sessionOverlay(session);
  const auto& inputs = ownershipInputs(session);

  auto candidateResult = facts::ReborrowRegionBuilder::build(
      inputs.flow(), inputs.loans(), inputs.references(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.regions.size() != 0);
  tamper(candidate, builtMir, overlay);

  auto verifiedResult = facts::ReborrowRegionVerifier::verify(
      zc::mv(candidate), inputs.flow(), inputs.loans(), inputs.references(), builtMir, overlay);
  expectPublishedRejection(verifiedResult, ir::IrFailureKind::InputRevisionMismatch);
}

/// Builds an initialization candidate from the fixture, applies one lineage
/// tamper, and requires the independent verifier to reject it with an input
/// revision mismatch.
template <typename Tamper>
void expectInitializationLineageRejection(const OwnershipPipelineFixture& fixture,
                                          Tamper&& tamper) {
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = sessionOverlay(session);
  const auto& inputs = ownershipInputs(session);

  auto candidateResult =
      facts::InitializationBuilder::build(builtMir, overlay, inputs.flow(), inputs.movePaths());
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() != 0);
  tamper(candidate, builtMir, overlay);

  auto verifiedResult = facts::InitializationVerifier::verify(zc::mv(candidate), builtMir, overlay,
                                                              inputs.flow(), inputs.movePaths());
  expectPublishedRejection(verifiedResult, ir::IrFailureKind::InputRevisionMismatch);
}

/// Builds an ownership-resource candidate from the fixture, applies one
/// lineage tamper, and requires the independent verifier to reject it with an
/// input revision mismatch.
template <typename Tamper>
void expectOwnershipResourceLineageRejection(const OwnershipPipelineFixture& fixture,
                                             Tamper&& tamper) {
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = sessionOverlay(session);
  const auto& inputs = ownershipInputs(session);

  auto candidateResult =
      facts::OwnershipResourceBuilder::build(inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() != 0);
  tamper(candidate, builtMir, overlay);

  auto verifiedResult = facts::OwnershipResourceVerifier::verify(
      zc::mv(candidate), inputs.movePaths(), builtMir, overlay);
  expectPublishedRejection(verifiedResult, ir::IrFailureKind::InputRevisionMismatch);
}

/// Builds an escape candidate from the fixture, applies one lineage tamper,
/// and requires the independent verifier to reject it with an input revision
/// mismatch.
///
/// The reborrow fixture returns one reference, so the builder emits one
/// escape fact; the verifier checks all six lineage fields before
/// reconstructing the expected inventory, so every lineage tamper still
/// rejects with an input revision mismatch.
template <typename Tamper>
void expectEscapeLineageRejection(const OwnershipPipelineFixture& fixture, Tamper&& tamper) {
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = sessionOverlay(session);
  const auto& inputs = ownershipInputs(session);
  auto memberships = buildVerifiedMemberships(fixture);
  ZC_REQUIRE(memberships.isVerified());

  auto candidateResult = facts::EscapeBuilder::build(
      inputs.flow(), inputs.loans(), inputs.references(), inputs.resources(), inputs.captures(),
      memberships.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  tamper(candidate, builtMir, overlay);

  auto verifiedResult = facts::EscapeVerifier::verify(
      zc::mv(candidate), inputs.flow(), inputs.loans(), inputs.references(), inputs.resources(),
      inputs.captures(), memberships.verifiedValue(), builtMir, overlay);
  expectPublishedRejection(verifiedResult, ir::IrFailureKind::InputRevisionMismatch);
}

}  // namespace

// Every ownership fact carries lineage fields that bind it to the Built MIR,
// event overlay, and borrow-evidence composition. Tampering any one field must
// make the independent verifier reconstruct a different fact and reject the
// candidate, publishing the rejection instead of an ownership output.

ZC_TEST("Ownership lineage mutation rejects a tampered fact owner") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = sessionOverlay(session);
  const auto& inputs = ownershipInputs(session);

  auto candidateResult =
      facts::ReborrowStateBuilder::build(inputs.references(), inputs.regions(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.states.size() != 0);
  candidate.states[0].owner = identity::DefId();

  auto verifiedResult = facts::ReborrowStateVerifier::verify(zc::mv(candidate), inputs.references(),
                                                             inputs.regions(), builtMir, overlay);
  expectPublishedRejection(verifiedResult, ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Ownership lineage mutation rejects a tampered fact point phase") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = sessionOverlay(session);
  const auto& inputs = ownershipInputs(session);

  auto candidateResult =
      facts::ReborrowStateBuilder::build(inputs.references(), inputs.regions(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.states.size() != 0);
  // The first state point is the after-commit event point; flip its phase.
  ZC_REQUIRE(candidate.states[0].point.kind() == facts::OwnershipPointKind::AfterEvent);
  candidate.states[0].point =
      facts::OwnershipPoint::beforeEvent(candidate.states[0].point.afterEventValue().event);

  auto verifiedResult = facts::ReborrowStateVerifier::verify(zc::mv(candidate), inputs.references(),
                                                             inputs.regions(), builtMir, overlay);
  expectPublishedRejection(verifiedResult, ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Ownership lineage mutation rejects a tampered causal operand ordinal") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = sessionOverlay(session);
  const auto& inputs = ownershipInputs(session);

  auto candidateResult =
      facts::ReborrowStateBuilder::build(inputs.references(), inputs.regions(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.states.size() != 0);
  candidate.states[0].loan.operandOrdinal += 1;

  auto verifiedResult = facts::ReborrowStateVerifier::verify(zc::mv(candidate), inputs.references(),
                                                             inputs.regions(), builtMir, overlay);
  expectPublishedRejection(verifiedResult, ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Ownership lineage mutation rejects a swapped liveness role sequence") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = sessionOverlay(session);
  const auto& inputs = ownershipInputs(session);

  auto candidateResult = facts::ReferenceDefinitionBuilder::build(
      inputs.movePaths(), inputs.loans(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.definitions.size() == 1);
  auto& livePoints = candidate.definitions[0].livePoints;
  ZC_REQUIRE(livePoints.afterCommit != livePoints.beforeReturn);
  auto afterCommit = zc::mv(livePoints.afterCommit);
  livePoints.afterCommit = zc::mv(livePoints.beforeReturn);
  livePoints.beforeReturn = zc::mv(afterCommit);

  auto verifiedResult = facts::ReferenceDefinitionVerifier::verify(
      zc::mv(candidate), inputs.movePaths(), inputs.loans(), builtMir, overlay);
  expectPublishedRejection(verifiedResult, ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Ownership lineage mutation rejects a tampered presentation source span") {
  OwnershipPipelineFixture fixture(
      "fun helper() -> i32 { return 0; }\n"
      "fun entry() -> i32 { return helper(); }"_zc);
  const auto& builtMir = fixture.builtMir();

  auto candidateResult = buildOverlay(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 2);
  // The two functions cover disjoint source ranges, so the callee's first
  // presentation span is a genuine foreign span for the caller.
  zc::Maybe<identity::DefId> callerOwner;
  for (const auto& function : builtMir.functions()) {
    if (function.blocks.size() == 2) callerOwner = function.owner;
  }
  ZC_REQUIRE(callerOwner != zc::none);
  zc::Maybe<identity::SourceSpan> donor;
  ZC_IF_SOME(owner, callerOwner) {
    for (const auto& function : candidate.functions) {
      if (function.owner == owner) continue;
      ZC_REQUIRE(function.sourceMap.size() != 0);
      donor = function.sourceMap[0].span.clone();
    }
    ZC_REQUIRE(donor != zc::none);
    for (auto& function : candidate.functions) {
      if (function.owner != owner) continue;
      ZC_REQUIRE(function.sourceMap.size() != 0);
      ZC_IF_SOME(span, donor) {
        ZC_REQUIRE(function.sourceMap[0].span.byteStart() != span.byteStart() ||
                   function.sourceMap[0].span.byteEnd() != span.byteEnd());
        function.sourceMap[0].span = span.clone();
      }
    }
  }

  auto verifiedResult = verifyOverlay(zc::mv(candidate), fixture);
  expectPublishedRejection(verifiedResult, ir::IrFailureKind::InvalidFact);
}

ZC_TEST("Ownership lineage mutation rejects a tampered marker-derived drop requirement") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Copy};\n"
      "struct Cell { value: i32, }\n"
      "impl !Copy for Cell;\n"
      "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = sessionOverlay(session);
  const auto& inputs = ownershipInputs(session);

  auto candidateResult =
      facts::OwnershipResourceBuilder::build(inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].facts.size() == 1);
  ZC_EXPECT(candidate.functions[0].facts[0].requirement == facts::DropRequirement::Logical);
  candidate.functions[0].facts[0].requirement = facts::DropRequirement::Linear;

  auto verifiedResult = facts::OwnershipResourceVerifier::verify(
      zc::mv(candidate), inputs.movePaths(), builtMir, overlay);
  expectPublishedRejection(verifiedResult, ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Ownership lineage mutation rejects a tampered logical drop plan subject") {
  OwnershipPipelineFixture fixture(
      "import core::marker::{Copy, Linear};\n"
      "struct Cell { value: i32, }\n"
      "impl !Copy for Cell;\n"
      "unsafe impl Linear for Cell;\n"
      "fun entry() -> Cell { let cell = Cell { value: 0 }; return cell; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = sessionOverlay(session);
  const auto& inputs = ownershipInputs(session);

  auto candidateResult =
      facts::OwnershipResourceBuilder::build(inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].dropPlans.size() == 1);
  candidate.functions[0].dropPlans[0].subject.introduction.operandOrdinal += 1;

  auto verifiedResult = facts::OwnershipResourceVerifier::verify(
      zc::mv(candidate), inputs.movePaths(), builtMir, overlay);
  expectPublishedRejection(verifiedResult, ir::IrFailureKind::InvalidOwnershipProof);
}

// Every ownership candidate carries the semantic-context brand, context
// fingerprint, module identity, built/overlay revisions, and borrow-evidence
// revision that bind it to its inputs. Tampering any one lineage field must
// make the independent verifier reject the candidate with an input revision
// mismatch, publishing the rejection instead of an ownership output.

ZC_TEST("Ownership lineage mutation rejects a foreign semantic context brand") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectReborrowStateLineageRejection(
      fixture, [](facts::ReborrowStateCandidate& candidate, const mir::VerifiedBuiltMir& builtMir,
                  const VerifiedOwnershipEventOverlay&) {
        candidate.semanticContext = identity::SemanticContextBrand{};
        ZC_REQUIRE(candidate.semanticContext != builtMir.semanticContext());
      });
}

ZC_TEST("Ownership lineage mutation rejects a foreign context fingerprint") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectReborrowStateLineageRejection(
      fixture, [](facts::ReborrowStateCandidate& candidate, const mir::VerifiedBuiltMir& builtMir,
                  const VerifiedOwnershipEventOverlay&) {
        candidate.contextFingerprint =
            identity::ContextFingerprint::fromCanonicalDigest(identity::Sha256Digest{});
        ZC_REQUIRE(candidate.contextFingerprint.digest() != builtMir.contextFingerprint().digest());
      });
}

ZC_TEST("Ownership lineage mutation rejects a foreign module identity") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectReborrowStateLineageRejection(
      fixture, [](facts::ReborrowStateCandidate& candidate, const mir::VerifiedBuiltMir& builtMir,
                  const VerifiedOwnershipEventOverlay&) {
        candidate.module = identity::ModuleId{};
        ZC_REQUIRE(candidate.module != builtMir.module());
      });
}

ZC_TEST("Ownership lineage mutation rejects a foreign built revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectReborrowStateLineageRejection(
      fixture, [](facts::ReborrowStateCandidate& candidate, const mir::VerifiedBuiltMir& builtMir,
                  const VerifiedOwnershipEventOverlay&) {
        candidate.builtRevision = mir::MirRevisionId::fromDigest(identity::Sha256Digest{});
        ZC_REQUIRE(candidate.builtRevision.digest() != builtMir.revision().digest());
      });
}

ZC_TEST("Ownership lineage mutation rejects a foreign overlay revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectReborrowStateLineageRejection(fixture, [](facts::ReborrowStateCandidate& candidate,
                                                  const mir::VerifiedBuiltMir&,
                                                  const VerifiedOwnershipEventOverlay& overlay) {
    candidate.overlayRevision = OwnershipEventOverlayRevision::fromDigest(identity::Sha256Digest{});
    ZC_REQUIRE(candidate.overlayRevision.digest() != overlay.revision().digest());
  });
}

ZC_TEST("Ownership lineage mutation rejects a foreign borrow evidence revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  // BorrowEvidenceRevision has no public digest constructor, so a second,
  // genuinely different compilation donates a foreign-but-valid revision.
  OwnershipPipelineFixture foreign("fun entry() -> i32 { return 0; }"_zc);
  ZC_REQUIRE(fixture.builtMir().borrowEvidenceRevision().digest() !=
             foreign.builtMir().borrowEvidenceRevision().digest());
  expectReborrowStateLineageRejection(fixture, [&foreign](facts::ReborrowStateCandidate& candidate,
                                                          const mir::VerifiedBuiltMir& builtMir,
                                                          const VerifiedOwnershipEventOverlay&) {
    candidate.borrowEvidenceRevision = foreign.builtMir().borrowEvidenceRevision();
    ZC_REQUIRE(candidate.borrowEvidenceRevision.digest() !=
               builtMir.borrowEvidenceRevision().digest());
  });
}

// --- MovePathCandidate lineage tamper tests ---

ZC_TEST("Ownership lineage mutation rejects a foreign move-path semantic context brand") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectMovePathLineageRejection(fixture, tamperSemanticContext);
}

ZC_TEST("Ownership lineage mutation rejects a foreign move-path context fingerprint") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectMovePathLineageRejection(fixture, tamperContextFingerprint);
}

ZC_TEST("Ownership lineage mutation rejects a foreign move-path module identity") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectMovePathLineageRejection(fixture, tamperModuleIdentity);
}

ZC_TEST("Ownership lineage mutation rejects a foreign move-path built revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectMovePathLineageRejection(fixture, tamperBuiltRevision);
}

ZC_TEST("Ownership lineage mutation rejects a foreign move-path overlay revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectMovePathLineageRejection(fixture, tamperOverlayRevision);
}

// --- FlowCandidate lineage tamper tests ---

ZC_TEST("Ownership lineage mutation rejects a foreign flow semantic context brand") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectFlowLineageRejection(fixture, tamperSemanticContext);
}

ZC_TEST("Ownership lineage mutation rejects a foreign flow context fingerprint") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectFlowLineageRejection(fixture, tamperContextFingerprint);
}

ZC_TEST("Ownership lineage mutation rejects a foreign flow module identity") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectFlowLineageRejection(fixture, tamperModuleIdentity);
}

ZC_TEST("Ownership lineage mutation rejects a foreign flow built revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectFlowLineageRejection(fixture, tamperBuiltRevision);
}

ZC_TEST("Ownership lineage mutation rejects a foreign flow overlay revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectFlowLineageRejection(fixture, tamperOverlayRevision);
}

// --- LoanCandidate lineage tamper tests ---

ZC_TEST("Ownership lineage mutation rejects a foreign loan semantic context brand") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectLoanLineageRejection(fixture, tamperSemanticContext);
}

ZC_TEST("Ownership lineage mutation rejects a foreign loan context fingerprint") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectLoanLineageRejection(fixture, tamperContextFingerprint);
}

ZC_TEST("Ownership lineage mutation rejects a foreign loan module identity") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectLoanLineageRejection(fixture, tamperModuleIdentity);
}

ZC_TEST("Ownership lineage mutation rejects a foreign loan built revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectLoanLineageRejection(fixture, tamperBuiltRevision);
}

ZC_TEST("Ownership lineage mutation rejects a foreign loan overlay revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectLoanLineageRejection(fixture, tamperOverlayRevision);
}

ZC_TEST("Ownership lineage mutation rejects a foreign loan borrow evidence revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  OwnershipPipelineFixture foreign("fun entry() -> i32 { return 0; }"_zc);
  ZC_REQUIRE(fixture.builtMir().borrowEvidenceRevision().digest() !=
             foreign.builtMir().borrowEvidenceRevision().digest());
  expectLoanLineageRejection(
      fixture, [&foreign](auto& candidate, const auto& builtMir, const auto&) {
        candidate.borrowEvidenceRevision = foreign.builtMir().borrowEvidenceRevision();
        ZC_REQUIRE(candidate.borrowEvidenceRevision.digest() !=
                   builtMir.borrowEvidenceRevision().digest());
      });
}

// --- ReferenceDefinitionCandidate lineage tamper tests ---

ZC_TEST(
    "Ownership lineage mutation rejects a foreign reference-definition semantic context brand") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectReferenceDefinitionLineageRejection(fixture, tamperSemanticContext);
}

ZC_TEST("Ownership lineage mutation rejects a foreign reference-definition context fingerprint") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectReferenceDefinitionLineageRejection(fixture, tamperContextFingerprint);
}

ZC_TEST("Ownership lineage mutation rejects a foreign reference-definition module identity") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectReferenceDefinitionLineageRejection(fixture, tamperModuleIdentity);
}

ZC_TEST("Ownership lineage mutation rejects a foreign reference-definition built revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectReferenceDefinitionLineageRejection(fixture, tamperBuiltRevision);
}

ZC_TEST("Ownership lineage mutation rejects a foreign reference-definition overlay revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectReferenceDefinitionLineageRejection(fixture, tamperOverlayRevision);
}

ZC_TEST(
    "Ownership lineage mutation rejects a foreign reference-definition borrow evidence revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  OwnershipPipelineFixture foreign("fun entry() -> i32 { return 0; }"_zc);
  ZC_REQUIRE(fixture.builtMir().borrowEvidenceRevision().digest() !=
             foreign.builtMir().borrowEvidenceRevision().digest());
  expectReferenceDefinitionLineageRejection(
      fixture, [&foreign](auto& candidate, const auto& builtMir, const auto&) {
        candidate.borrowEvidenceRevision = foreign.builtMir().borrowEvidenceRevision();
        ZC_REQUIRE(candidate.borrowEvidenceRevision.digest() !=
                   builtMir.borrowEvidenceRevision().digest());
      });
}

// --- ReborrowRegionCandidate lineage tamper tests ---

ZC_TEST("Ownership lineage mutation rejects a foreign reborrow-region semantic context brand") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectReborrowRegionLineageRejection(fixture, tamperSemanticContext);
}

ZC_TEST("Ownership lineage mutation rejects a foreign reborrow-region context fingerprint") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectReborrowRegionLineageRejection(fixture, tamperContextFingerprint);
}

ZC_TEST("Ownership lineage mutation rejects a foreign reborrow-region module identity") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectReborrowRegionLineageRejection(fixture, tamperModuleIdentity);
}

ZC_TEST("Ownership lineage mutation rejects a foreign reborrow-region built revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectReborrowRegionLineageRejection(fixture, tamperBuiltRevision);
}

ZC_TEST("Ownership lineage mutation rejects a foreign reborrow-region overlay revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectReborrowRegionLineageRejection(fixture, tamperOverlayRevision);
}

ZC_TEST("Ownership lineage mutation rejects a foreign reborrow-region borrow evidence revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  OwnershipPipelineFixture foreign("fun entry() -> i32 { return 0; }"_zc);
  ZC_REQUIRE(fixture.builtMir().borrowEvidenceRevision().digest() !=
             foreign.builtMir().borrowEvidenceRevision().digest());
  expectReborrowRegionLineageRejection(
      fixture, [&foreign](auto& candidate, const auto& builtMir, const auto&) {
        candidate.borrowEvidenceRevision = foreign.builtMir().borrowEvidenceRevision();
        ZC_REQUIRE(candidate.borrowEvidenceRevision.digest() !=
                   builtMir.borrowEvidenceRevision().digest());
      });
}

// --- InitializationCandidate lineage tamper tests ---

ZC_TEST("Ownership lineage mutation rejects a foreign initialization semantic context brand") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectInitializationLineageRejection(fixture, tamperSemanticContext);
}

ZC_TEST("Ownership lineage mutation rejects a foreign initialization context fingerprint") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectInitializationLineageRejection(fixture, tamperContextFingerprint);
}

ZC_TEST("Ownership lineage mutation rejects a foreign initialization module identity") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectInitializationLineageRejection(fixture, tamperModuleIdentity);
}

ZC_TEST("Ownership lineage mutation rejects a foreign initialization built revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectInitializationLineageRejection(fixture, tamperBuiltRevision);
}

ZC_TEST("Ownership lineage mutation rejects a foreign initialization overlay revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectInitializationLineageRejection(fixture, tamperOverlayRevision);
}

// --- OwnershipResourceCandidate lineage tamper tests ---

ZC_TEST("Ownership lineage mutation rejects a foreign resource semantic context brand") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectOwnershipResourceLineageRejection(fixture, tamperSemanticContext);
}

ZC_TEST("Ownership lineage mutation rejects a foreign resource context fingerprint") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectOwnershipResourceLineageRejection(fixture, tamperContextFingerprint);
}

ZC_TEST("Ownership lineage mutation rejects a foreign resource module identity") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectOwnershipResourceLineageRejection(fixture, tamperModuleIdentity);
}

ZC_TEST("Ownership lineage mutation rejects a foreign resource built revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectOwnershipResourceLineageRejection(fixture, tamperBuiltRevision);
}

ZC_TEST("Ownership lineage mutation rejects a foreign resource overlay revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectOwnershipResourceLineageRejection(fixture, tamperOverlayRevision);
}

// --- EscapeCandidate lineage tamper tests ---

ZC_TEST("Ownership lineage mutation rejects a foreign escape semantic context brand") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectEscapeLineageRejection(fixture, tamperSemanticContext);
}

ZC_TEST("Ownership lineage mutation rejects a foreign escape context fingerprint") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectEscapeLineageRejection(fixture, tamperContextFingerprint);
}

ZC_TEST("Ownership lineage mutation rejects a foreign escape module identity") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectEscapeLineageRejection(fixture, tamperModuleIdentity);
}

ZC_TEST("Ownership lineage mutation rejects a foreign escape built revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectEscapeLineageRejection(fixture, tamperBuiltRevision);
}

ZC_TEST("Ownership lineage mutation rejects a foreign escape overlay revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  expectEscapeLineageRejection(fixture, tamperOverlayRevision);
}

ZC_TEST("Ownership lineage mutation rejects a foreign escape borrow evidence revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  OwnershipPipelineFixture foreign("fun entry() -> i32 { return 0; }"_zc);
  ZC_REQUIRE(fixture.builtMir().borrowEvidenceRevision().digest() !=
             foreign.builtMir().borrowEvidenceRevision().digest());
  expectEscapeLineageRejection(
      fixture, [&foreign](auto& candidate, const auto& builtMir, const auto&) {
        candidate.borrowEvidenceRevision = foreign.builtMir().borrowEvidenceRevision();
        ZC_REQUIRE(candidate.borrowEvidenceRevision.digest() !=
                   builtMir.borrowEvidenceRevision().digest());
      });
}

ZC_TEST("Ownership lineage mutation rejects a spurious escape fact") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = sessionOverlay(session);
  const auto& inputs = ownershipInputs(session);
  auto memberships = buildVerifiedMemberships(fixture);
  ZC_REQUIRE(memberships.isVerified());

  auto candidateResult = facts::EscapeBuilder::build(
      inputs.flow(), inputs.loans(), inputs.references(), inputs.resources(), inputs.captures(),
      memberships.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.escapes.size() == 1);

  // The verifier independently reconstructs the one-row inventory, so adding
  // a second synthetic-but-well-formed row makes the ordinals diverge and
  // rejects the candidate. Donate a real move-path key from the fixture to
  // keep the fabricated row well-formed.
  const auto& movePathFunctions = inputs.movePaths().functions();
  ZC_REQUIRE(movePathFunctions.size() != 0);
  ZC_REQUIRE(movePathFunctions[0].facts.size() != 0);
  facts::MovePathKey source{movePathFunctions[0].facts[0].key.owner,
                            movePathFunctions[0].facts[0].key.place.clone()};
  zc::Vector<facts::EscapeOriginCause> origins;
  zc::Vector<facts::RawProvenanceCarrierKey> rawCarriers;
  candidate.escapes.add(
      facts::EscapeFact{MirEventKey{MirLocation{identity::DefId{}, MirPoint::entry()}, 0},
                        zc::mv(source), facts::EscapeKind::returnEscape(), zc::mv(origins),
                        zc::mv(rawCarriers), facts::EscapeProof::owned()});

  auto verifiedResult = facts::EscapeVerifier::verify(
      zc::mv(candidate), inputs.flow(), inputs.loans(), inputs.references(), inputs.resources(),
      inputs.captures(), memberships.verifiedValue(), builtMir, overlay);
  expectPublishedRejection(verifiedResult, ir::IrFailureKind::InvalidOwnershipProof);
}

// --- Escape derivation tests ---

ZC_TEST("Ownership escape derivation produces one return escape for a parameter reborrow") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& inputs = ownershipInputs(session);

  const auto escapes = inputs.escapes().escapes();
  ZC_REQUIRE(escapes.size() == 1);
  const auto& fact = escapes[0];
  ZC_EXPECT(fact.kind.isReturn());
  ZC_EXPECT(fact.rawCarriers.size() == 0);
  ZC_REQUIRE(fact.proof.isDirectInput());
  ZC_EXPECT(fact.proof.directInputValue().input.isParameter());
  ZC_EXPECT(fact.proof.directInputValue().input.parameterValue().index == 0);
  ZC_REQUIRE(fact.origins.size() == 1);
  ZC_EXPECT(fact.origins[0].route.isDirect());
  ZC_EXPECT(fact.origins[0].origin.root.region.isInput());
  ZC_EXPECT(fact.origins[0].origin.root.region.inputValue().input.isParameter());
  ZC_EXPECT(fact.origins[0].origin.root.region.inputValue().input.parameterValue().index == 0);
  ZC_EXPECT(fact.origins[0].origin.active.isInput());
}

ZC_TEST("Ownership escape derivation produces no escapes for a scalar function") {
  OwnershipPipelineFixture fixture("fun entry() -> i32 { return 0; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& inputs = ownershipInputs(session);

  ZC_EXPECT(inputs.escapes().escapes().size() == 0);
}

ZC_TEST("Ownership escape derivation rejects a tampered escape kind") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = sessionOverlay(session);
  const auto& inputs = ownershipInputs(session);
  auto memberships = buildVerifiedMemberships(fixture);
  ZC_REQUIRE(memberships.isVerified());

  auto candidateResult = facts::EscapeBuilder::build(
      inputs.flow(), inputs.loans(), inputs.references(), inputs.resources(), inputs.captures(),
      memberships.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.escapes.size() == 1);
  // Tamper the kind from Return to Store; the verifier independently
  // reconstructs Return and rejects the wrong-kind row.
  candidate.escapes[0].kind = facts::EscapeKind::storeEscape(
      facts::MovePathKey{candidate.escapes[0].source.owner,
                         candidate.escapes[0].source.place.clone()},
      facts::RegionKey::staticRegion(candidate.escapes[0].source.owner));

  auto verifiedResult = facts::EscapeVerifier::verify(
      zc::mv(candidate), inputs.flow(), inputs.loans(), inputs.references(), inputs.resources(),
      inputs.captures(), memberships.verifiedValue(), builtMir, overlay);
  expectPublishedRejection(verifiedResult, ir::IrFailureKind::InvalidOwnershipProof);
}

ZC_TEST("Ownership escape derivation rejects a missing escape row") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = sessionOverlay(session);
  const auto& inputs = ownershipInputs(session);
  auto memberships = buildVerifiedMemberships(fixture);
  ZC_REQUIRE(memberships.isVerified());

  auto candidateResult = facts::EscapeBuilder::build(
      inputs.flow(), inputs.loans(), inputs.references(), inputs.resources(), inputs.captures(),
      memberships.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.escapes.size() == 1);
  // Drop the only row; the verifier reconstructs one and rejects the missing row.
  candidate.escapes.clear();

  auto verifiedResult = facts::EscapeVerifier::verify(
      zc::mv(candidate), inputs.flow(), inputs.loans(), inputs.references(), inputs.resources(),
      inputs.captures(), memberships.verifiedValue(), builtMir, overlay);
  expectPublishedRejection(verifiedResult, ir::IrFailureKind::InvalidOwnershipProof);
}

// A returned local borrow roots at the loan region, not at an input region. The
// escape proof names the reference's complete live point set so the loan region
// can be checked for containment at every required point. The borrow-source
// rejection makes the pipeline reject the module, so the escape inventory is
// reconstructed from the staged Built MIR rather than the published facts.

ZC_TEST("Ownership escape derivation produces a contained return escape for a local borrow") {
  RejectedBorrowPipelineFixture fixture(
      "fun entry() -> &i32 { let value: i32 = 0; return &value; }"_zc);
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
  auto escapesResult = facts::EscapeVerifier::verify(
      zc::mv(escapeCandidate).takeVerified(), flow.verifiedValue(), loans.verifiedValue(),
      references.verifiedValue(), resources.verifiedValue(), captures.verifiedValue(),
      memberships.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(escapesResult.isVerified());

  const auto escapes = escapesResult.verifiedValue().escapes();
  ZC_REQUIRE(escapes.size() == 1);
  const auto& fact = escapes[0];
  ZC_EXPECT(fact.kind.isReturn());
  ZC_EXPECT(fact.rawCarriers.size() == 0);
  ZC_REQUIRE(fact.proof.isContained());
  ZC_EXPECT(fact.proof.containedValue().requiredPoints.size() == 5);
  ZC_REQUIRE(fact.origins.size() == 1);
  ZC_EXPECT(fact.origins[0].route.isDirect());
  ZC_EXPECT(fact.origins[0].origin.root.region.isLoan());
  ZC_EXPECT(fact.origins[0].origin.active.isLoan());
}

// The verifier independently reconstructs the expected escape inventory. A
// candidate that carries an escape the reconstruction does not derive (a
// spurious row on an otherwise empty scalar function) must be rejected.

ZC_TEST("Ownership escape derivation rejects a spurious escape on a scalar function") {
  OwnershipPipelineFixture fixture("fun entry() -> i32 { let x = 42; return x; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = sessionOverlay(session);
  const auto& inputs = ownershipInputs(session);
  auto memberships = buildVerifiedMemberships(fixture);
  ZC_REQUIRE(memberships.isVerified());

  // A scalar function produces no reference definitions and therefore no
  // escape facts, even though it carries move-path facts for its local.
  ZC_REQUIRE(inputs.references().definitions().size() == 0);
  auto candidateResult = facts::EscapeBuilder::build(
      inputs.flow(), inputs.loans(), inputs.references(), inputs.resources(), inputs.captures(),
      memberships.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.escapes.size() == 0);

  // Fabricate a well-formed escape the reconstruction cannot derive. Donate a
  // real move-path key from the fixture to keep the fabricated row well-formed.
  const auto& movePathFunctions = inputs.movePaths().functions();
  ZC_REQUIRE(movePathFunctions.size() != 0);
  ZC_REQUIRE(movePathFunctions[0].facts.size() != 0);
  facts::MovePathKey source{movePathFunctions[0].facts[0].key.owner,
                            movePathFunctions[0].facts[0].key.place.clone()};
  zc::Vector<facts::EscapeOriginCause> origins;
  zc::Vector<facts::RawProvenanceCarrierKey> rawCarriers;
  candidate.escapes.add(
      facts::EscapeFact{MirEventKey{MirLocation{identity::DefId{}, MirPoint::entry()}, 0},
                        zc::mv(source), facts::EscapeKind::returnEscape(), zc::mv(origins),
                        zc::mv(rawCarriers), facts::EscapeProof::owned()});

  auto verifiedResult = facts::EscapeVerifier::verify(
      zc::mv(candidate), inputs.flow(), inputs.loans(), inputs.references(), inputs.resources(),
      inputs.captures(), memberships.verifiedValue(), builtMir, overlay);
  expectPublishedRejection(verifiedResult, ir::IrFailureKind::InvalidOwnershipProof);
}

// The admitted subset produces only Return escapes: the capture inventory is
// empty (closures are not admitted) and the overlay carries no Escape-role
// slot off the return path (reference stores are not admitted), so the
// extended Store and ClosureCapture derivations emit no rows.

ZC_TEST("Ownership escape derivation emits only return escapes for the admitted subset") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  const auto& session = fixture.compilerSession();
  const auto& inputs = ownershipInputs(session);

  ZC_REQUIRE(inputs.captures().captures().size() == 0);
  const auto escapes = inputs.escapes().escapes();
  ZC_REQUIRE(escapes.size() == 1);
  ZC_EXPECT(escapes[0].kind.isReturn());
  ZC_EXPECT(!escapes[0].kind.isStore());
  ZC_EXPECT(!escapes[0].kind.isClosureCapture());
}

}  // namespace zomlang::compiler::ownership
