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
#include "zomlang/compiler/ownership/facts/inputs.h"
#include "zomlang/compiler/ownership/facts/loans.h"
#include "zomlang/compiler/ownership/facts/region-key.h"
#include "zomlang/compiler/ownership/facts/region-membership.h"
#include "zomlang/compiler/ownership/facts/region-outlives.h"
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
  ZC_FAIL_REQUIRE("invalid region-outlives fixture scalar");
}

identity::SortedFeatureSet emptyFeatures() {
  zc::Vector<identity::FeatureName> values;
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid region-outlives fixture feature set");
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
  ZC_FAIL_REQUIRE("invalid region-outlives fixture target projection");
}

package::RegisteredTargetProfileName targetProfileName() {
  auto result = package::RegisteredTargetProfileName::from("host"_zc);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid region-outlives fixture target profile name");
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
  ZC_FAIL_REQUIRE("invalid region-outlives fixture target registry");
}

package::RegisteredTargetSelection targetSelection(const ir::TargetRegistrySnapshot& registry) {
  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  ZC_IF_SOME(targets, service) {
    auto result = targets.select(zc::none, package::PackagePanicStrategy::Unwind);
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("invalid region-outlives fixture target selection");
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
  ZC_FAIL_REQUIRE("invalid region-outlives fixture compilation request");
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

/// Builds a region-outlives candidate from verified memberships.
ir::IrOperationResult<facts::RegionOutlivesCandidate> buildRegionOutlives(
    const facts::VerifiedRegionMemberships& memberships, const OwnershipPipelineFixture& fixture) {
  return facts::RegionOutlivesBuilder::build(memberships, fixture.builtMir(), fixture.overlay());
}

/// Builds and independently verifies region outlives from the fixture.
ir::IrOperationResult<facts::VerifiedRegionOutlives> buildAndVerifyRegionOutlives(
    const OwnershipPipelineFixture& fixture) {
  auto memberships = buildAndVerifyRegionMemberships(fixture);
  ZC_REQUIRE(memberships.isVerified());
  auto candidateResult = buildRegionOutlives(memberships.verifiedValue(), fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  return facts::RegionOutlivesVerifier::verify(zc::mv(candidate), memberships.verifiedValue(),
                                               fixture.builtMir(), fixture.overlay());
}

/// Returns true when the outlives inventory contains the (from, to) relation.
bool hasOutlives(zc::ArrayPtr<const facts::RegionOutlivesFact> outlives,
                 const facts::RegionKey& from, const facts::RegionKey& to) {
  for (const auto& fact : outlives) {
    if (fact.from == from && fact.to == to) return true;
  }
  return false;
}

// Pure-derivation helpers for the subset semantics.

MirEventKey makeEventKey(uint32_t operandOrdinal = 0) {
  return MirEventKey{MirLocation{identity::DefId{}, MirPoint::entry()}, operandOrdinal};
}

facts::RegionKey makeLoanRegion(uint32_t operandOrdinal = 0) {
  return facts::RegionKey::loanRegion(LoanKey{makeEventKey(operandOrdinal)});
}

facts::RegionMembership makeMembership(facts::RegionKey region, facts::OwnershipPoint point) {
  return facts::RegionMembership{zc::mv(region), zc::mv(point)};
}

}  // namespace

// A scalar function has no reference parameters and no loans, so the region
// liveness dataflow produces an empty membership inventory and the outlives
// relation is empty as well.

ZC_TEST("Region outlives produces empty inventory for a scalar function") {
  OwnershipPipelineFixture fixture("fun entry() -> i32 { return 0; }"_zc);
  auto verified = buildAndVerifyRegionOutlives(fixture);
  ZC_REQUIRE(verified.isVerified());
  ZC_EXPECT(verified.verifiedValue().outlives().size() == 0);
}

// A parameter reborrow introduces one Input region live at every reachable
// point and one Loan region live from its borrow issue onward. The loan's
// live-point set is a subset of the input's, so the input outlives the loan.

ZC_TEST("Region outlives derives input outlives loan for a parameter reborrow") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  auto verified = buildAndVerifyRegionOutlives(fixture);
  ZC_REQUIRE(verified.isVerified());
  const auto& outlives = verified.verifiedValue().outlives();
  ZC_EXPECT(outlives.size() != 0);

  const auto owner = fixture.builtMir().functions()[0].owner;
  const auto inputRegion =
      facts::RegionKey::inputRegion(owner, facts::BorrowInputKey::parameter(0));
  ZC_REQUIRE(fixture.inputs().loans().loans().size() == 1);
  const auto loanRegion =
      facts::RegionKey::loanRegion(LoanKey{fixture.inputs().loans().loans()[0].issue});

  ZC_EXPECT(hasOutlives(outlives, inputRegion, loanRegion));
  // The loan is not live at the function entry, so it cannot outlive the input.
  ZC_EXPECT(!hasOutlives(outlives, loanRegion, inputRegion));
}

// A region live at every point outlives every region whose live-point set is a
// subset. A Static region is live for the entire program, so it outlives every
// loan. The pure derivation is exercised directly because the production
// membership dataflow does not seed Static regions yet.

ZC_TEST("Region outlives static region outlives every region") {
  const auto staticRegion = facts::RegionKey::staticRegion(identity::DefId{});
  const auto loanA = makeLoanRegion(0);
  const auto loanB = makeLoanRegion(1);

  const auto p1 = facts::OwnershipPoint::cfg(MirPoint::entry());
  const auto p2 = facts::OwnershipPoint::beforeEvent(makeEventKey(0));
  const auto p3 = facts::OwnershipPoint::afterEvent(makeEventKey(1));
  const auto p4 = facts::OwnershipPoint::beforeEvent(makeEventKey(2));

  zc::Vector<facts::RegionMembership> memberships;
  // Static is live at every point.
  memberships.add(makeMembership(staticRegion.clone(), p1));
  memberships.add(makeMembership(staticRegion.clone(), p2));
  memberships.add(makeMembership(staticRegion.clone(), p3));
  memberships.add(makeMembership(staticRegion.clone(), p4));
  // Loan A is live at the first two points only.
  memberships.add(makeMembership(loanA.clone(), p1));
  memberships.add(makeMembership(loanA.clone(), p2));
  // Loan B is live at the last two points only.
  memberships.add(makeMembership(loanB.clone(), p3));
  memberships.add(makeMembership(loanB.clone(), p4));

  auto outlives = facts::RegionOutlivesBuilder::derive(memberships.asPtr());
  ZC_EXPECT(hasOutlives(outlives.asPtr(), staticRegion, loanA));
  ZC_EXPECT(hasOutlives(outlives.asPtr(), staticRegion, loanB));
  // Neither loan outlives the static region or the other loan.
  ZC_EXPECT(!hasOutlives(outlives.asPtr(), loanA, staticRegion));
  ZC_EXPECT(!hasOutlives(outlives.asPtr(), loanB, staticRegion));
  ZC_EXPECT(!hasOutlives(outlives.asPtr(), loanA, loanB));
  ZC_EXPECT(!hasOutlives(outlives.asPtr(), loanB, loanA));
}

// Two loans with disjoint live-point sets have no subset relation in either
// direction, so the outlives inventory records no edge between them.

ZC_TEST("Region outlives unrelated loans have no relation") {
  const auto loanA = makeLoanRegion(0);
  const auto loanB = makeLoanRegion(1);

  const auto p1 = facts::OwnershipPoint::cfg(MirPoint::entry());
  const auto p2 = facts::OwnershipPoint::beforeEvent(makeEventKey(0));
  const auto p3 = facts::OwnershipPoint::afterEvent(makeEventKey(1));
  const auto p4 = facts::OwnershipPoint::beforeEvent(makeEventKey(2));

  zc::Vector<facts::RegionMembership> memberships;
  memberships.add(makeMembership(loanA.clone(), p1));
  memberships.add(makeMembership(loanA.clone(), p2));
  memberships.add(makeMembership(loanB.clone(), p3));
  memberships.add(makeMembership(loanB.clone(), p4));

  auto outlives = facts::RegionOutlivesBuilder::derive(memberships.asPtr());
  ZC_EXPECT(outlives.size() == 0);
}

// Every region-outlives candidate carries lineage fields that bind it to the
// Built MIR, event overlay, and borrow-evidence composition. Tampering any one
// field must make the independent verifier reject the candidate with an input
// revision mismatch.

ZC_TEST("Region outlives rejects a foreign semantic context brand") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  auto memberships = buildAndVerifyRegionMemberships(fixture);
  ZC_REQUIRE(memberships.isVerified());
  auto candidateResult = buildRegionOutlives(memberships.verifiedValue(), fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  candidate.semanticContext = identity::SemanticContextBrand{};
  ZC_REQUIRE(candidate.semanticContext != fixture.builtMir().semanticContext());

  auto verified = facts::RegionOutlivesVerifier::verify(
      zc::mv(candidate), memberships.verifiedValue(), fixture.builtMir(), fixture.overlay());
  expectPublishedRejection(verified, ir::IrFailureKind::InputRevisionMismatch);
}

ZC_TEST("Region outlives rejects a foreign context fingerprint") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  auto memberships = buildAndVerifyRegionMemberships(fixture);
  ZC_REQUIRE(memberships.isVerified());
  auto candidateResult = buildRegionOutlives(memberships.verifiedValue(), fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  candidate.contextFingerprint =
      identity::ContextFingerprint::fromCanonicalDigest(identity::Sha256Digest{});
  ZC_REQUIRE(candidate.contextFingerprint.digest() !=
             fixture.builtMir().contextFingerprint().digest());

  auto verified = facts::RegionOutlivesVerifier::verify(
      zc::mv(candidate), memberships.verifiedValue(), fixture.builtMir(), fixture.overlay());
  expectPublishedRejection(verified, ir::IrFailureKind::InputRevisionMismatch);
}

ZC_TEST("Region outlives rejects a foreign module identity") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  auto memberships = buildAndVerifyRegionMemberships(fixture);
  ZC_REQUIRE(memberships.isVerified());
  auto candidateResult = buildRegionOutlives(memberships.verifiedValue(), fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  candidate.module = identity::ModuleId{};
  ZC_REQUIRE(candidate.module != fixture.builtMir().module());

  auto verified = facts::RegionOutlivesVerifier::verify(
      zc::mv(candidate), memberships.verifiedValue(), fixture.builtMir(), fixture.overlay());
  expectPublishedRejection(verified, ir::IrFailureKind::InputRevisionMismatch);
}

ZC_TEST("Region outlives rejects a foreign built revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  auto memberships = buildAndVerifyRegionMemberships(fixture);
  ZC_REQUIRE(memberships.isVerified());
  auto candidateResult = buildRegionOutlives(memberships.verifiedValue(), fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  candidate.builtRevision = mir::MirRevisionId::fromDigest(identity::Sha256Digest{});
  ZC_REQUIRE(candidate.builtRevision.digest() != fixture.builtMir().revision().digest());

  auto verified = facts::RegionOutlivesVerifier::verify(
      zc::mv(candidate), memberships.verifiedValue(), fixture.builtMir(), fixture.overlay());
  expectPublishedRejection(verified, ir::IrFailureKind::InputRevisionMismatch);
}

ZC_TEST("Region outlives rejects a foreign overlay revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  auto memberships = buildAndVerifyRegionMemberships(fixture);
  ZC_REQUIRE(memberships.isVerified());
  auto candidateResult = buildRegionOutlives(memberships.verifiedValue(), fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  candidate.overlayRevision = OwnershipEventOverlayRevision::fromDigest(identity::Sha256Digest{});
  ZC_REQUIRE(candidate.overlayRevision.digest() != fixture.overlay().revision().digest());

  auto verified = facts::RegionOutlivesVerifier::verify(
      zc::mv(candidate), memberships.verifiedValue(), fixture.builtMir(), fixture.overlay());
  expectPublishedRejection(verified, ir::IrFailureKind::InputRevisionMismatch);
}

ZC_TEST("Region outlives rejects a foreign borrow evidence revision") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  // BorrowEvidenceRevision has no public digest constructor, so a second,
  // genuinely different compilation donates a foreign-but-valid revision.
  OwnershipPipelineFixture foreign("fun entry() -> i32 { return 0; }"_zc);
  ZC_REQUIRE(fixture.builtMir().borrowEvidenceRevision().digest() !=
             foreign.builtMir().borrowEvidenceRevision().digest());
  auto memberships = buildAndVerifyRegionMemberships(fixture);
  ZC_REQUIRE(memberships.isVerified());
  auto candidateResult = buildRegionOutlives(memberships.verifiedValue(), fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  candidate.borrowEvidenceRevision = foreign.builtMir().borrowEvidenceRevision();
  ZC_REQUIRE(candidate.borrowEvidenceRevision.digest() !=
             fixture.builtMir().borrowEvidenceRevision().digest());

  auto verified = facts::RegionOutlivesVerifier::verify(
      zc::mv(candidate), memberships.verifiedValue(), fixture.builtMir(), fixture.overlay());
  expectPublishedRejection(verified, ir::IrFailureKind::InputRevisionMismatch);
}

// The verifier independently reconstructs the expected outlives inventory. A
// candidate that carries a relation the reconstruction cannot derive (a
// spurious edge on an otherwise empty scalar function) must be rejected.

ZC_TEST("Region outlives rejects a spurious fact on a scalar function") {
  OwnershipPipelineFixture fixture("fun entry() -> i32 { return 0; }"_zc);
  auto memberships = buildAndVerifyRegionMemberships(fixture);
  ZC_REQUIRE(memberships.isVerified());
  auto candidateResult = buildRegionOutlives(memberships.verifiedValue(), fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.outlives.size() == 0);

  // Fabricate a well-formed outlives fact the reconstruction cannot derive.
  const auto owner = fixture.builtMir().functions()[0].owner;
  candidate.outlives.add(facts::RegionOutlivesFact{
      facts::RegionKey::staticRegion(owner),
      facts::RegionKey::inputRegion(owner, facts::BorrowInputKey::parameter(0))});

  auto verified = facts::RegionOutlivesVerifier::verify(
      zc::mv(candidate), memberships.verifiedValue(), fixture.builtMir(), fixture.overlay());
  expectPublishedRejection(verified, ir::IrFailureKind::InvalidOwnershipProof);
}

// The verifier independently reconstructs the expected outlives inventory. A
// candidate that drops every relation the reconstruction derives (an empty
// candidate on a reborrow function) must be rejected.

ZC_TEST("Region outlives rejects a missing fact on a reborrow function") {
  OwnershipPipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  auto memberships = buildAndVerifyRegionMemberships(fixture);
  ZC_REQUIRE(memberships.isVerified());
  auto candidateResult = buildRegionOutlives(memberships.verifiedValue(), fixture);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.outlives.size() != 0);
  candidate.outlives.clear();

  auto verified = facts::RegionOutlivesVerifier::verify(
      zc::mv(candidate), memberships.verifiedValue(), fixture.builtMir(), fixture.overlay());
  expectPublishedRejection(verified, ir::IrFailureKind::InvalidOwnershipProof);
}

}  // namespace zomlang::compiler::ownership
