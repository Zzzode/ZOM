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
#include "zomlang/compiler/diagnostics/consumer/diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/core/diagnostic-engine.h"
#include "zomlang/compiler/driver/interface/borrow-evidence.h"
#include "zomlang/compiler/driver/package/manifest-parser.h"
#include "zomlang/compiler/driver/package/source-record.h"
#include "zomlang/compiler/driver/session/compiler-session.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"
#include "zomlang/compiler/identity/crypto/sha256.h"
#include "zomlang/compiler/ir/ir-failure.h"
#include "zomlang/compiler/ir/target-registry.h"
#include "zomlang/compiler/mir/built-mir.h"
#include "zomlang/compiler/ownership/facts/flow.h"
#include "zomlang/compiler/ownership/facts/init.h"
#include "zomlang/compiler/ownership/facts/inputs.h"
#include "zomlang/compiler/ownership/facts/loans.h"
#include "zomlang/compiler/ownership/facts/paths.h"
#include "zomlang/compiler/ownership/facts/refs.h"
#include "zomlang/compiler/ownership/facts/regions.h"
#include "zomlang/compiler/ownership/facts/resources.h"
#include "zomlang/compiler/ownership/facts/states.h"
#include "zomlang/compiler/ownership/ownership-event-overlay.h"
#include "zomlang/compiler/source/manager.h"
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
      "import core::marker::{Linear};\n"
      "struct Cell { value: i32, }\n"
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

}  // namespace zomlang::compiler::ownership
