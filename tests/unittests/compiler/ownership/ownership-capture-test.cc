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
#include "compiler/driver/interface/borrow-evidence.h"
#include "compiler/driver/package/manifest-parser.h"
#include "compiler/driver/package/source-record.h"
#include "compiler/driver/session/compiler-session.h"
#include "compiler/identity/crypto/sha256.h"
#include "compiler/ir/ir-failure.h"
#include "compiler/ir/target-registry.h"
#include "compiler/mir/built-mir.h"
#include "compiler/ownership/facts/capture.h"
#include "compiler/ownership/facts/inputs.h"
#include "compiler/ownership/facts/paths.h"
#include "compiler/ownership/ownership-event-overlay.h"
#include "tests/unittests/compiler/driver/core/core-library-test-fixture.h"

namespace zomlang::compiler::ownership {
namespace {

namespace driver = zomlang::compiler::driver;
namespace package = driver::package;
namespace facts = zomlang::compiler::ownership::facts;

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto result = Scalar::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid capture fixture scalar");
}

identity::SortedFeatureSet emptyFeatures() {
  zc::Vector<identity::FeatureName> values;
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid capture fixture feature set");
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
  ZC_FAIL_REQUIRE("invalid capture fixture target projection");
}

package::RegisteredTargetProfileName targetProfileName() {
  auto result = package::RegisteredTargetProfileName::from("host"_zc);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid capture fixture target profile name");
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
  ZC_FAIL_REQUIRE("invalid capture fixture target registry");
}

package::RegisteredTargetSelection targetSelection(const ir::TargetRegistrySnapshot& registry) {
  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  ZC_IF_SOME(targets, service) {
    auto result = targets.select(zc::none, package::PackagePanicStrategy::Unwind);
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("invalid capture fixture target selection");
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
  ZC_FAIL_REQUIRE("invalid capture fixture compilation request");
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

class CapturePipelineFixture final {
public:
  explicit CapturePipelineFixture(zc::StringPtr sourceText)
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

  const facts::VerifiedOwnershipInputs& inputs() const {
    return session.getOwnershipCheckedMirModules()[0].facts();
  }

  const VerifiedOwnershipEventOverlay& overlay() const {
    return session.getOwnershipCheckedMirModules()[0].eventOverlay();
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

// Closures are not yet admitted by surface admission, so every admitted
// function produces an empty capture inventory. The builder derives the empty
// inventory and the verifier independently confirms it; the session pipeline
// publishes it through the verified ownership inputs bundle.

ZC_TEST("Ownership capture derivation produces no captures for a scalar function") {
  CapturePipelineFixture fixture("fun entry() -> i32 { return 0; }"_zc);
  const auto& inputs = fixture.inputs();

  ZC_EXPECT(inputs.captures().captures().size() == 0);
}

ZC_TEST("Ownership capture derivation produces no captures for a reborrow function") {
  CapturePipelineFixture fixture("fun reborrow(value: &i32) -> &i32 { return &*value; }"_zc);
  const auto& inputs = fixture.inputs();

  // The reborrow fixture carries references and escapes but no closures, so
  // the capture inventory stays empty alongside the non-empty escape inventory.
  ZC_EXPECT(inputs.escapes().escapes().size() == 1);
  ZC_EXPECT(inputs.captures().captures().size() == 0);
}

// The verifier independently reconstructs the expected (empty) inventory. A
// candidate that carries a capture the reconstruction cannot derive must be
// rejected as an invalid ownership proof.

ZC_TEST("Ownership capture derivation rejects a spurious capture fact") {
  CapturePipelineFixture fixture("fun entry() -> i32 { let x = 42; return x; }"_zc);
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = fixture.overlay();
  const auto& inputs = fixture.inputs();

  auto candidateResult = facts::CaptureBuilder::build(inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.captures.size() == 0);

  // Fabricate a well-formed capture the reconstruction cannot derive. Donate a
  // real move-path key from the fixture to keep the fabricated row well-formed.
  const auto& movePathFunctions = inputs.movePaths().functions();
  ZC_REQUIRE(movePathFunctions.size() != 0);
  ZC_REQUIRE(movePathFunctions[0].facts.size() != 0);
  facts::MovePathKey closure{movePathFunctions[0].facts[0].key.owner,
                             movePathFunctions[0].facts[0].key.place.clone()};
  facts::MovePathKey captured{movePathFunctions[0].facts[0].key.owner,
                              movePathFunctions[0].facts[0].key.place.clone()};
  const auto construction = MirEventKey{MirLocation{identity::DefId{}, MirPoint::entry()}, 0};
  candidate.captures.add(facts::CaptureFact{
      construction, zc::mv(closure), zc::mv(captured),
      facts::RegionKey::closureValueRegion(construction,
                                           facts::MovePathKey{movePathFunctions[0].facts[0].key.owner,
                                                              movePathFunctions[0].facts[0].key.place
                                                                  .clone()}),
      facts::RegionKey::staticRegion(identity::DefId{})});

  auto verifiedResult =
      facts::CaptureVerifier::verify(zc::mv(candidate), inputs.movePaths(), builtMir, overlay);
  expectPublishedRejection(verifiedResult, ir::IrFailureKind::InvalidOwnershipProof);
}

// Every ownership candidate carries the semantic-context brand, context
// fingerprint, module identity, built/overlay revisions, and borrow-evidence
// revision that bind it to its inputs. Tampering any one lineage field must
// make the independent verifier reject the candidate with an input revision
// mismatch, publishing the rejection instead of an ownership output.

ZC_TEST("Ownership capture derivation rejects a foreign semantic context brand") {
  CapturePipelineFixture fixture("fun entry() -> i32 { return 0; }"_zc);
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = fixture.overlay();
  const auto& inputs = fixture.inputs();

  auto candidateResult = facts::CaptureBuilder::build(inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  candidate.semanticContext = identity::SemanticContextBrand{};
  ZC_REQUIRE(candidate.semanticContext != builtMir.semanticContext());

  auto verifiedResult =
      facts::CaptureVerifier::verify(zc::mv(candidate), inputs.movePaths(), builtMir, overlay);
  expectPublishedRejection(verifiedResult, ir::IrFailureKind::InputRevisionMismatch);
}

ZC_TEST("Ownership capture derivation rejects a foreign context fingerprint") {
  CapturePipelineFixture fixture("fun entry() -> i32 { return 0; }"_zc);
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = fixture.overlay();
  const auto& inputs = fixture.inputs();

  auto candidateResult = facts::CaptureBuilder::build(inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  candidate.contextFingerprint =
      identity::ContextFingerprint::fromCanonicalDigest(identity::Sha256Digest{});
  ZC_REQUIRE(candidate.contextFingerprint.digest() != builtMir.contextFingerprint().digest());

  auto verifiedResult =
      facts::CaptureVerifier::verify(zc::mv(candidate), inputs.movePaths(), builtMir, overlay);
  expectPublishedRejection(verifiedResult, ir::IrFailureKind::InputRevisionMismatch);
}

ZC_TEST("Ownership capture derivation rejects a foreign module identity") {
  CapturePipelineFixture fixture("fun entry() -> i32 { return 0; }"_zc);
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = fixture.overlay();
  const auto& inputs = fixture.inputs();

  auto candidateResult = facts::CaptureBuilder::build(inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  candidate.module = identity::ModuleId{};
  ZC_REQUIRE(candidate.module != builtMir.module());

  auto verifiedResult =
      facts::CaptureVerifier::verify(zc::mv(candidate), inputs.movePaths(), builtMir, overlay);
  expectPublishedRejection(verifiedResult, ir::IrFailureKind::InputRevisionMismatch);
}

ZC_TEST("Ownership capture derivation rejects a foreign built revision") {
  CapturePipelineFixture fixture("fun entry() -> i32 { return 0; }"_zc);
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = fixture.overlay();
  const auto& inputs = fixture.inputs();

  auto candidateResult = facts::CaptureBuilder::build(inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  candidate.builtRevision = mir::MirRevisionId::fromDigest(identity::Sha256Digest{});
  ZC_REQUIRE(candidate.builtRevision.digest() != builtMir.revision().digest());

  auto verifiedResult =
      facts::CaptureVerifier::verify(zc::mv(candidate), inputs.movePaths(), builtMir, overlay);
  expectPublishedRejection(verifiedResult, ir::IrFailureKind::InputRevisionMismatch);
}

ZC_TEST("Ownership capture derivation rejects a foreign overlay revision") {
  CapturePipelineFixture fixture("fun entry() -> i32 { return 0; }"_zc);
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = fixture.overlay();
  const auto& inputs = fixture.inputs();

  auto candidateResult = facts::CaptureBuilder::build(inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  candidate.overlayRevision = OwnershipEventOverlayRevision::fromDigest(identity::Sha256Digest{});
  ZC_REQUIRE(candidate.overlayRevision.digest() != overlay.revision().digest());

  auto verifiedResult =
      facts::CaptureVerifier::verify(zc::mv(candidate), inputs.movePaths(), builtMir, overlay);
  expectPublishedRejection(verifiedResult, ir::IrFailureKind::InputRevisionMismatch);
}

ZC_TEST("Ownership capture derivation rejects a foreign borrow evidence revision") {
  CapturePipelineFixture fixture("fun entry() -> i32 { return 0; }"_zc);
  // BorrowEvidenceRevision has no public digest constructor, so a second,
  // genuinely different compilation donates a foreign-but-valid revision.
  CapturePipelineFixture foreign("fun other() -> i32 { return 1; }"_zc);
  ZC_REQUIRE(fixture.builtMir().borrowEvidenceRevision().digest() !=
             foreign.builtMir().borrowEvidenceRevision().digest());
  const auto& builtMir = fixture.builtMir();
  const auto& overlay = fixture.overlay();
  const auto& inputs = fixture.inputs();

  auto candidateResult = facts::CaptureBuilder::build(inputs.movePaths(), builtMir, overlay);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  candidate.borrowEvidenceRevision = foreign.builtMir().borrowEvidenceRevision();
  ZC_REQUIRE(candidate.borrowEvidenceRevision.digest() !=
             builtMir.borrowEvidenceRevision().digest());

  auto verifiedResult =
      facts::CaptureVerifier::verify(zc::mv(candidate), inputs.movePaths(), builtMir, overlay);
  expectPublishedRejection(verifiedResult, ir::IrFailureKind::InputRevisionMismatch);
}

}  // namespace
}  // namespace zomlang::compiler::ownership
