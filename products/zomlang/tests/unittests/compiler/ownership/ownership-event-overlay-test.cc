// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/ownership/ownership-event-overlay.h"

#include "zc/core/encoding.h"
#include "zc/core/time.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/driver/compiler-session.h"
#include "zomlang/compiler/driver/package/manifest-parser.h"
#include "zomlang/compiler/driver/package/source-record.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/sha256.h"
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
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].slots.size() == 6);
  const auto owner = candidate.functions[0].owner;
  const auto block = builtMir.functions()[0].blocks[0].id;
  const auto& candidateSlots = candidate.functions[0].slots;

  ZC_EXPECT(candidateSlots[0].key.location.owner == owner);
  ZC_EXPECT(candidateSlots[0].key.location.point.kind() == MirPointKind::BeforeStatement);
  ZC_EXPECT(candidateSlots[0].key.location.point.beforeStatementValue().block == block);
  ZC_EXPECT(candidateSlots[0].key.location.point.beforeStatementValue().ordinal == 0);
  ZC_EXPECT(candidateSlots[0].key.operandOrdinal == 0);
  ZC_EXPECT(candidateSlots[0].stage == OwnershipEventStage::Effect);
  ZC_REQUIRE(candidateSlots[0].roles.size() == 2);
  ZC_EXPECT(candidateSlots[0].roles[0] == OwnershipEventRole::Operation);
  ZC_EXPECT(candidateSlots[0].roles[1] == OwnershipEventRole::StorageLive);

  for (size_t index = 1; index <= 3; ++index) {
    ZC_EXPECT(candidateSlots[index].key.location.owner == owner);
    ZC_EXPECT(candidateSlots[index].key.location.point.kind() == MirPointKind::BeforeStatement);
    ZC_EXPECT(candidateSlots[index].key.location.point.beforeStatementValue().block == block);
    ZC_EXPECT(candidateSlots[index].key.location.point.beforeStatementValue().ordinal == 1);
    ZC_EXPECT(candidateSlots[index].key.operandOrdinal == index - 1);
  }
  ZC_EXPECT(candidateSlots[1].stage == OwnershipEventStage::Source);
  ZC_REQUIRE(candidateSlots[1].roles.size() == 1);
  ZC_EXPECT(candidateSlots[1].roles[0] == OwnershipEventRole::ConstantOperand);
  ZC_EXPECT(candidateSlots[2].stage == OwnershipEventStage::Effect);
  ZC_REQUIRE(candidateSlots[2].roles.size() == 1);
  ZC_EXPECT(candidateSlots[2].roles[0] == OwnershipEventRole::Operation);
  ZC_EXPECT(candidateSlots[3].stage == OwnershipEventStage::Commit);
  ZC_REQUIRE(candidateSlots[3].roles.size() == 1);
  ZC_EXPECT(candidateSlots[3].roles[0] == OwnershipEventRole::DestinationWrite);

  for (size_t index = 4; index <= 5; ++index) {
    ZC_EXPECT(candidateSlots[index].key.location.owner == owner);
    ZC_EXPECT(candidateSlots[index].key.location.point.kind() == MirPointKind::BeforeTerminator);
    ZC_EXPECT(candidateSlots[index].key.location.point.beforeTerminatorValue().block == block);
    ZC_EXPECT(candidateSlots[index].key.operandOrdinal == index - 4);
  }
  ZC_EXPECT(candidateSlots[4].stage == OwnershipEventStage::Source);
  ZC_REQUIRE(candidateSlots[4].roles.size() == 2);
  ZC_EXPECT(candidateSlots[4].roles[0] == OwnershipEventRole::OperandRead);
  ZC_EXPECT(candidateSlots[4].roles[1] == OwnershipEventRole::OperandMove);
  ZC_EXPECT(candidateSlots[5].stage == OwnershipEventStage::Effect);
  ZC_REQUIRE(candidateSlots[5].roles.size() == 1);
  ZC_EXPECT(candidateSlots[5].roles[0] == OwnershipEventRole::Operation);

  auto verifiedResult =
      OwnershipEventOverlayVerifier::verify(zc::mv(candidate), builtMir, registries);
  ZC_REQUIRE(verifiedResult.isVerified());
  auto overlay = zc::mv(verifiedResult).takeVerified();
  ZC_EXPECT(overlay.semanticContext() == builtMir.semanticContext());
  ZC_EXPECT(overlay.module() == builtMir.module());
  ZC_EXPECT(overlay.builtRevision().digest() == builtMir.revision().digest());
  ZC_EXPECT(overlay.functions().size() == 1);
  ZC_EXPECT(overlay.functions()[0].owner == builtMir.functions()[0].owner);
  ZC_EXPECT(overlay.functions()[0].slots.size() == 6);
}

ZC_TEST("Ownership event overlay verifier rejects a tampered function slot count") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);
  const auto& builtMir = fixture.builtMir();
  const auto& registries = fixture.registries();

  auto candidateResult = OwnershipEventOverlayBuilder::build(builtMir, registries);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  const auto& first = candidate.functions[0].slots[0];
  MirEventSlot extra{
      MirEventKey{MirLocation{candidate.functions[0].owner, first.key.location.point}, 999},
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

ZC_TEST("Ownership event overlay verifier rejects a foreign event owner") {
  OwnershipPipelineFixture fixture("let value = 0; let other = 1;"_zc);
  const auto& builtMir = fixture.builtMir();
  const auto& registries = fixture.registries();

  auto candidateResult = OwnershipEventOverlayBuilder::build(builtMir, registries);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 2);
  ZC_REQUIRE(candidate.functions[0].slots.size() != 0);
  ZC_REQUIRE(candidate.functions[0].owner != candidate.functions[1].owner);
  candidate.functions[0].slots[0].key.location.owner = candidate.functions[1].owner;

  auto verifiedResult =
      OwnershipEventOverlayVerifier::verify(zc::mv(candidate), builtMir, registries);
  ZC_EXPECT(!verifiedResult.isVerified());
}

ZC_TEST("Ownership event overlay verifier rejects a statement event at the terminator point") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);
  const auto& builtMir = fixture.builtMir();
  const auto& registries = fixture.registries();

  auto candidateResult = OwnershipEventOverlayBuilder::build(builtMir, registries);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].slots.size() != 0);
  const auto block =
      candidate.functions[0].slots[0].key.location.point.beforeStatementValue().block;
  candidate.functions[0].slots[0].key.location.point = MirPoint::beforeTerminator(block);

  auto verifiedResult =
      OwnershipEventOverlayVerifier::verify(zc::mv(candidate), builtMir, registries);
  ZC_EXPECT(!verifiedResult.isVerified());
}

ZC_TEST("Ownership event overlay verifier rejects a gapped causal ordinal") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);
  const auto& builtMir = fixture.builtMir();
  const auto& registries = fixture.registries();

  auto candidateResult = OwnershipEventOverlayBuilder::build(builtMir, registries);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  ZC_REQUIRE(candidate.functions.size() == 1);
  ZC_REQUIRE(candidate.functions[0].slots.size() != 0);
  candidate.functions[0].slots[0].key.operandOrdinal = 1;

  auto verifiedResult =
      OwnershipEventOverlayVerifier::verify(zc::mv(candidate), builtMir, registries);
  ZC_EXPECT(!verifiedResult.isVerified());
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

zc::Array<uint8_t> scalarInitializerFunctionOracle(zc::ArrayPtr<const uint8_t> owner,
                                                   uint32_t block) {
  const uint8_t storageLiveRoles[] = {0x01, 0x0a};
  const uint8_t constantRoles[] = {0x06};
  const uint8_t operationRoles[] = {0x01};
  const uint8_t destinationRoles[] = {0x07};
  const uint8_t moveRoles[] = {0x03, 0x05};

  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(owner);
  encoder.encodeSequenceSize(6);
  auto append = [&](zc::Array<uint8_t>&& key, uint8_t stage, zc::ArrayPtr<const uint8_t> roles) {
    auto slot = encodeEventSlotOracle(key.asPtr(), stage, roles);
    encoder.encodeByteString(key.asPtr());
    encoder.encodeByteString(slot.asPtr());
  };
  append(encodeStatementEventKeyOracle(owner, block, 0, 0), 0x02, zc::arrayPtr(storageLiveRoles));
  append(encodeStatementEventKeyOracle(owner, block, 1, 0), 0x01, zc::arrayPtr(constantRoles));
  append(encodeStatementEventKeyOracle(owner, block, 1, 1), 0x02, zc::arrayPtr(operationRoles));
  append(encodeStatementEventKeyOracle(owner, block, 1, 2), 0x03, zc::arrayPtr(destinationRoles));
  append(encodeTerminatorEventKeyOracle(owner, block, 0), 0x01, zc::arrayPtr(moveRoles));
  append(encodeTerminatorEventKeyOracle(owner, block, 1), 0x02, zc::arrayPtr(operationRoles));
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

ZC_TEST("Ownership event overlay production revision matches the independent function oracle") {
  OwnershipPipelineFixture fixture("let value = 0;"_zc);
  const auto& builtMir = fixture.builtMir();
  const auto& registries = fixture.registries();
  auto candidateResult = OwnershipEventOverlayBuilder::build(builtMir, registries);
  ZC_REQUIRE(candidateResult.isVerified());
  auto candidate = zc::mv(candidateResult).takeVerified();
  auto verifiedResult =
      OwnershipEventOverlayVerifier::verify(zc::mv(candidate), builtMir, registries);
  ZC_REQUIRE(verifiedResult.isVerified());
  auto overlay = zc::mv(verifiedResult).takeVerified();
  ZC_REQUIRE(overlay.functions().size() == 1);
  ZC_REQUIRE(overlay.functions()[0].slots.size() == 6);
  ZC_REQUIRE(builtMir.functions().size() == 1);
  ZC_REQUIRE(builtMir.functions()[0].blocks.size() == 1);

  auto ownerKey = registries.definitions().lookup(overlay.functions()[0].owner);
  auto moduleKey = registries.modules().lookup(overlay.module());
  ZC_REQUIRE(ownerKey != zc::none);
  ZC_REQUIRE(moduleKey != zc::none);
  ZC_IF_SOME(owner, ownerKey) {
    ZC_IF_SOME(module, moduleKey) {
      auto expandedOwner = owner.encode();
      auto expandedModule = module.encode();
      zc::Vector<zc::Array<uint8_t>> functions;
      functions.add(scalarInitializerFunctionOracle(
          expandedOwner.asPtr(), builtMir.functions()[0].blocks[0].id.ordinal()));
      auto preimage =
          encodeOverlayOracle(overlay.contextFingerprint().digest(), expandedModule.asPtr(),
                              overlay.checkedFactsRevision().digest(),
                              overlay.builtRevision().digest(), functions.asPtr());
      auto revision = identity::sha256(preimage.asPtr());
      ZC_REQUIRE(revision != zc::none);
      ZC_IF_SOME(value, revision) { ZC_EXPECT(value == overlay.revision().digest()); }
    }
  }
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

}  // namespace
}  // namespace zomlang::compiler::ownership
