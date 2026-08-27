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
#include "zomlang/compiler/driver/package/manifest-parser.h"
#include "zomlang/compiler/driver/package/source-record.h"
#include "zomlang/compiler/driver/session/compiler-session.h"
#include "zomlang/compiler/identity/crypto/sha256.h"
#include "zomlang/compiler/ir/ir-failure.h"
#include "zomlang/compiler/ir/target-registry.h"
#include "zomlang/compiler/mir/built-mir.h"
#include "zomlang/compiler/ownership/facts/flow.h"
#include "zomlang/compiler/ownership/facts/inputs.h"
#include "zomlang/compiler/ownership/facts/loans.h"
#include "zomlang/compiler/ownership/facts/paths.h"
#include "zomlang/compiler/ownership/facts/region-membership.h"
#include "zomlang/compiler/ownership/ownership-event-overlay.h"
#include "zomlang/tests/unittests/compiler/driver/core/core-library-test-fixture.h"

namespace zomlang::compiler::ownership {
namespace {

namespace driver = zomlang::compiler::driver;
namespace package = driver::package;
namespace facts = zomlang::compiler::ownership::facts;

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto result = Scalar::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid region-membership fixture scalar");
}

identity::SortedFeatureSet emptyFeatures() {
  zc::Vector<identity::FeatureName> values;
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid region-membership fixture feature set");
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
  ZC_FAIL_REQUIRE("invalid region-membership fixture target projection");
}

package::RegisteredTargetProfileName targetProfileName() {
  auto result = package::RegisteredTargetProfileName::from("host"_zc);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid region-membership fixture target profile name");
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
  ZC_FAIL_REQUIRE("invalid region-membership fixture target registry");
}

package::RegisteredTargetSelection targetSelection(const ir::TargetRegistrySnapshot& registry) {
  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  ZC_IF_SOME(targets, service) {
    auto result = targets.select(zc::none, package::PackagePanicStrategy::Unwind);
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("invalid region-membership fixture target selection");
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
  ZC_FAIL_REQUIRE("invalid region-membership fixture compilation request");
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

/// Fixture that runs the full session pipeline and requires success.
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

  const VerifiedOwnershipEventOverlay& overlay() const {
    return session.getOwnershipCheckedMirModules()[0].eventOverlay();
  }

  const facts::VerifiedOwnershipInputs& inputs() const {
    return session.getOwnershipCheckedMirModules()[0].facts();
  }

private:
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  driver::CompilerSession session;
};

/// Fixture for a source the borrow-source verifier rejects during checkSources
/// (a returned function-local borrow). The session publishes no ownership-
/// checked module, so the Built MIR and event overlay are pulled from the
/// staged borrow-source rejection view instead. Tests rebuild fact families
/// directly from that verified MIR.
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
    auto staged = session.firstStagedBorrowSourceRejectionForTesting();
    ZC_IF_SOME(value, staged) { return value.builtMir; }
    ZC_FAIL_REQUIRE("no staged borrow-source rejection");
  }

  const VerifiedOwnershipEventOverlay& overlay() const {
    auto staged = session.firstStagedBorrowSourceRejectionForTesting();
    ZC_IF_SOME(value, staged) { return value.eventOverlay; }
    ZC_FAIL_REQUIRE("no staged borrow-source rejection");
  }

private:
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  driver::CompilerSession session;
};

/// Asserts that a rejected ownership operation published exactly one invariant
/// failure of the expected kind and produced no verified ownership output.
template <typename Result>
void expectPublishedRejection(const Result& result, ir::IrFailureKind expected) {
  ZC_EXPECT(!result.isVerified());
  ZC_REQUIRE(result.isIrInvariantRejected());
  ZC_EXPECT(result.invariantFailures().facts().size() == 1);
  ZC_EXPECT(result.invariantFailures().facts()[0].kind() == expected);
}

/// Builds a region-membership candidate from the fixture's verified inputs.
ir::IrOperationResult<facts::RegionMembershipCandidate> buildRegionMemberships(
    const OwnershipPipelineFixture& fixture) {
  return facts::RegionMembershipBuilder::build(fixture.inputs().flow(), fixture.inputs().loans(),
                                               fixture.builtMir(), fixture.overlay());
}

/// Builds and independently verifies region memberships from the fixture.
ir::IrOperationResult<facts::VerifiedRegionMemberships> buildAndVerifyRegionMemberships(
    const OwnershipPipelineFixture& fixture) {
  auto candidateResult = buildRegionMemberships(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  return facts::RegionMembershipVerifier::verify(zc::mv(candidate), fixture.inputs().flow(),
                                                 fixture.inputs().loans(), fixture.builtMir(),
                                                 fixture.overlay());
}

/// Returns true when the region is live at the given ownership point.
bool isRegionLiveAt(zc::ArrayPtr<const facts::RegionMembership> memberships,
                    const facts::RegionKey& region, const facts::OwnershipPoint& point) {
  for (const auto& membership : memberships) {
    if (membership.region == region && membership.point == point) return true;
  }
  return false;
}

/// Finds the sole function's entry CFG point in the verified flow.
const facts::OwnershipPoint& flowEntryPoint(const facts::VerifiedFlow& flow) {
  ZC_REQUIRE(flow.functions().size() == 1);
  for (const auto& point : flow.functions()[0].points) {
    if (point.kind() == facts::OwnershipPointKind::Cfg &&
        point.cfgValue().point.kind() == MirPointKind::Entry) {
      return point;
    }
  }
  ZC_FAIL_REQUIRE("flow function has no entry CFG point");
}

/// Counts how many flow points carry the given region.
size_t countRegionLivePoints(zc::ArrayPtr<const facts::RegionMembership> memberships,
                             const facts::RegionKey& region) {
  size_t count = 0;
  for (const auto& membership : memberships) {
    if (membership.region == region) ++count;
  }
  return count;
}

}  // namespace

// A scalar function has no reference parameters and no loans, so the region
// liveness dataflow produces an empty membership inventory.

ZC_TEST("Region membership produces empty inventory for a scalar function") {
  OwnershipPipelineFixture fixture("fun entry() -> i32 { return 0; }"_zc);
  auto verified = buildAndVerifyRegionMemberships(fixture);
  ZC_REQUIRE(verified.isVerified());
  ZC_EXPECT(verified.verifiedValue().memberships().size() == 0);
}

// A parameter reborrow introduces one Input region seeded at the function entry
// CFG point. Input regions are never killed, so the region is live at every
// reachable flow point.

ZC_TEST("Region membership seeds input region at every point for a parameter reborrow") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  const auto& inputs = fixture.inputs();
  auto verified = buildAndVerifyRegionMemberships(fixture);
  ZC_REQUIRE(verified.isVerified());
  const auto& memberships = verified.verifiedValue().memberships();
  ZC_EXPECT(memberships.size() != 0);

  const auto owner = fixture.builtMir().functions()[0].owner;
  const auto inputRegion =
      facts::RegionKey::inputRegion(owner, facts::BorrowInputKey::parameter(0));
  const auto& entry = flowEntryPoint(inputs.flow());
  ZC_EXPECT(isRegionLiveAt(memberships, inputRegion, entry));

  // The Input region is never killed, so it must be live at every flow point.
  const auto flowPointCount = inputs.flow().functions()[0].points.size();
  ZC_EXPECT(countRegionLivePoints(memberships, inputRegion) == flowPointCount);

  // Every membership must name either the Input region or the reborrow Loan
  // region; no other region kinds are admissible for this shape.
  for (const auto& membership : memberships) {
    ZC_EXPECT(membership.region.isInput() || membership.region.isLoan());
  }
}

// A local borrow introduces one Loan region seeded at the loan activation
// point. The loan is killed at the AfterEvent of the destination's last use,
// so the Loan region is absent at the function entry and present from the
// activation onward.

ZC_TEST("Region membership activates loan region at borrow for a local borrow") {
  RejectedBorrowPipelineFixture fixture(
      "fun entry() -> &i32 { let value: i32 = 0; return &value; }"_zc);
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = fixture.overlay();

  // The source is rejected during checkSources, so no ownership-checked module
  // is published. Rebuild the flow and loan inventories directly from the
  // staged verified Built MIR to exercise region-membership derivation.
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

  auto membershipCandidate = facts::RegionMembershipBuilder::build(
      flow.verifiedValue(), loans.verifiedValue(), builtMir, overlay);
  ZC_REQUIRE(membershipCandidate.isVerified());
  auto verified = facts::RegionMembershipVerifier::verify(
      zc::mv(membershipCandidate).takeVerified(), flow.verifiedValue(), loans.verifiedValue(),
      builtMir, overlay);
  ZC_REQUIRE(verified.isVerified());
  const auto& memberships = verified.verifiedValue().memberships();
  ZC_EXPECT(memberships.size() != 0);

  // No reference parameters means no Input region.
  for (const auto& membership : memberships) { ZC_EXPECT(!membership.region.isInput()); }

  // The sole loan's region must be live from its activation point.
  ZC_REQUIRE(loans.verifiedValue().loans().size() == 1);
  const auto& loan = loans.verifiedValue().loans()[0];
  const auto loanRegion = facts::RegionKey::loanRegion(LoanKey{loan.issue});
  const auto& entry = flowEntryPoint(flow.verifiedValue());
  ZC_EXPECT(!isRegionLiveAt(memberships, loanRegion, entry));
  ZC_EXPECT(isRegionLiveAt(memberships, loanRegion, loan.activeFrom));
}

// Every region-membership candidate carries lineage fields that bind it to the
// Built MIR, event overlay, and borrow-evidence composition. Tampering any one
// field must make the independent verifier reject the candidate with an input
// revision mismatch.

ZC_TEST("Region membership rejects a foreign semantic context brand") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  auto candidateResult = buildRegionMemberships(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.memberships.size() != 0);
  candidate.semanticContext = identity::SemanticContextBrand{};
  ZC_REQUIRE(candidate.semanticContext != fixture.builtMir().semanticContext());

  auto verified = facts::RegionMembershipVerifier::verify(
      zc::mv(candidate), fixture.inputs().flow(), fixture.inputs().loans(), fixture.builtMir(),
      fixture.overlay());
  expectPublishedRejection(verified, ir::IrFailureKind::InputRevisionMismatch);
}

ZC_TEST("Region membership rejects a foreign context fingerprint") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  auto candidateResult = buildRegionMemberships(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  candidate.contextFingerprint =
      identity::ContextFingerprint::fromCanonicalDigest(identity::Sha256Digest{});
  ZC_REQUIRE(candidate.contextFingerprint.digest() !=
             fixture.builtMir().contextFingerprint().digest());

  auto verified = facts::RegionMembershipVerifier::verify(
      zc::mv(candidate), fixture.inputs().flow(), fixture.inputs().loans(), fixture.builtMir(),
      fixture.overlay());
  expectPublishedRejection(verified, ir::IrFailureKind::InputRevisionMismatch);
}

ZC_TEST("Region membership rejects a foreign module identity") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  auto candidateResult = buildRegionMemberships(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  candidate.module = identity::ModuleId{};
  ZC_REQUIRE(candidate.module != fixture.builtMir().module());

  auto verified = facts::RegionMembershipVerifier::verify(
      zc::mv(candidate), fixture.inputs().flow(), fixture.inputs().loans(), fixture.builtMir(),
      fixture.overlay());
  expectPublishedRejection(verified, ir::IrFailureKind::InputRevisionMismatch);
}

ZC_TEST("Region membership rejects a foreign built revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  auto candidateResult = buildRegionMemberships(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  candidate.builtRevision = mir::MirRevisionId::fromDigest(identity::Sha256Digest{});
  ZC_REQUIRE(candidate.builtRevision.digest() != fixture.builtMir().revision().digest());

  auto verified = facts::RegionMembershipVerifier::verify(
      zc::mv(candidate), fixture.inputs().flow(), fixture.inputs().loans(), fixture.builtMir(),
      fixture.overlay());
  expectPublishedRejection(verified, ir::IrFailureKind::InputRevisionMismatch);
}

ZC_TEST("Region membership rejects a foreign overlay revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  auto candidateResult = buildRegionMemberships(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  candidate.overlayRevision = OwnershipEventOverlayRevision::fromDigest(identity::Sha256Digest{});
  ZC_REQUIRE(candidate.overlayRevision.digest() != fixture.overlay().revision().digest());

  auto verified = facts::RegionMembershipVerifier::verify(
      zc::mv(candidate), fixture.inputs().flow(), fixture.inputs().loans(), fixture.builtMir(),
      fixture.overlay());
  expectPublishedRejection(verified, ir::IrFailureKind::InputRevisionMismatch);
}

ZC_TEST("Region membership rejects a foreign borrow evidence revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  // BorrowEvidenceRevision has no public digest constructor, so a second,
  // genuinely different compilation donates a foreign-but-valid revision.
  OwnershipPipelineFixture foreign("fun entry() -> i32 { return 0; }"_zc);
  ZC_REQUIRE(fixture.builtMir().borrowEvidenceRevision().digest() !=
             foreign.builtMir().borrowEvidenceRevision().digest());
  auto candidateResult = buildRegionMemberships(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  candidate.borrowEvidenceRevision = foreign.builtMir().borrowEvidenceRevision();
  ZC_REQUIRE(candidate.borrowEvidenceRevision.digest() !=
             fixture.builtMir().borrowEvidenceRevision().digest());

  auto verified = facts::RegionMembershipVerifier::verify(
      zc::mv(candidate), fixture.inputs().flow(), fixture.inputs().loans(), fixture.builtMir(),
      fixture.overlay());
  expectPublishedRejection(verified, ir::IrFailureKind::InputRevisionMismatch);
}

// The verifier independently reconstructs the expected membership inventory. A
// candidate that carries a membership the reconstruction does not derive (a
// spurious row on an otherwise empty scalar function) must be rejected.

ZC_TEST("Region membership rejects a spurious membership on a scalar function") {
  OwnershipPipelineFixture fixture("fun entry() -> i32 { return 0; }"_zc);
  auto candidateResult = buildRegionMemberships(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.memberships.size() == 0);

  // Fabricate a well-formed membership the reconstruction cannot derive.
  const auto owner = fixture.builtMir().functions()[0].owner;
  const auto entry = flowEntryPoint(fixture.inputs().flow());
  candidate.memberships.add(
      facts::RegionMembership{facts::RegionKey::staticRegion(owner), facts::OwnershipPoint{entry}});

  auto verified = facts::RegionMembershipVerifier::verify(
      zc::mv(candidate), fixture.inputs().flow(), fixture.inputs().loans(), fixture.builtMir(),
      fixture.overlay());
  expectPublishedRejection(verified, ir::IrFailureKind::InvalidOwnershipProof);
}

// The verifier independently reconstructs the expected membership inventory. A
// candidate that drops every membership the reconstruction derives (an empty
// candidate on a reborrow function) must be rejected.

ZC_TEST("Region membership rejects a missing membership on a reborrow function") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  auto candidateResult = buildRegionMemberships(fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.memberships.size() != 0);
  candidate.memberships.clear();

  auto verified = facts::RegionMembershipVerifier::verify(
      zc::mv(candidate), fixture.inputs().flow(), fixture.inputs().loans(), fixture.builtMir(),
      fixture.overlay());
  expectPublishedRejection(verified, ir::IrFailureKind::InvalidOwnershipProof);
}

}  // namespace zomlang::compiler::ownership
