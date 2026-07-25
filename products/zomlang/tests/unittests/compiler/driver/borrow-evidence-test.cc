// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/borrow-evidence.h"

#include "zc/core/encoding.h"
#include "zc/core/time.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/driver/compiler-session.h"
#include "zomlang/compiler/driver/package/manifest-parser.h"
#include "zomlang/compiler/driver/package/source-record.h"

namespace zomlang::compiler::driver::borrow_evidence {
namespace {

namespace package = driver::package;

identity::Sha256Digest repeatedDigest(uint8_t byte) {
  uint8_t bytes[32];
  for (auto& value : bytes) value = byte;
  ZC_IF_SOME(digest, identity::Sha256Digest::fromBytes(zc::arrayPtr(bytes))) { return digest; }
  ZC_FAIL_REQUIRE("invalid borrow-evidence digest fixture");
}

bool expandedModuleLess(identity::ModuleId left, identity::ModuleId right,
                        const identity::SemanticIdentityRegistrySet& registries) {
  auto leftKey = registries.modules().lookup(left);
  auto rightKey = registries.modules().lookup(right);
  ZC_REQUIRE(leftKey != zc::none);
  ZC_REQUIRE(rightKey != zc::none);
  ZC_IF_SOME(leftValue, leftKey) {
    ZC_IF_SOME(rightValue, rightKey) {
      const auto leftBytes = leftValue.encode();
      const auto rightBytes = rightValue.encode();
      const size_t shared =
          leftBytes.size() < rightBytes.size() ? leftBytes.size() : rightBytes.size();
      for (size_t index = 0; index < shared; ++index) {
        if (leftBytes[index] != rightBytes[index]) return leftBytes[index] < rightBytes[index];
      }
      return leftBytes.size() < rightBytes.size();
    }
  }
  ZC_UNREACHABLE
}

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto result = Scalar::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid borrow-evidence scalar fixture");
}

identity::SortedFeatureSet emptyFeatures() {
  zc::Vector<identity::FeatureName> values;
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid borrow-evidence feature fixture");
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

identity::CanonicalRelativePath detachedPath(zc::StringPtr filename) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(scalar<identity::CanonicalPathSegment>("src"_zc));
  segments.add(scalar<identity::CanonicalPathSegment>(filename));
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
  ZC_FAIL_REQUIRE("invalid borrow-evidence target projection");
}

package::RegisteredTargetProfileName targetProfileName() {
  auto result = package::RegisteredTargetProfileName::from("host"_zc);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid borrow-evidence target profile");
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
  ZC_FAIL_REQUIRE("invalid borrow-evidence target registry");
}

package::RegisteredTargetSelection targetSelection(const ir::TargetRegistrySnapshot& registry) {
  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  ZC_IF_SOME(targets, service) {
    auto result = targets.select(zc::none, package::PackagePanicStrategy::Unwind);
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("invalid borrow-evidence target selection");
}

ir::VerifiedTargetSelection verifiedTargetSelection(const ir::TargetRegistrySnapshot& registry) {
  auto result = registry.verify(targetSelection(registry));
  ZC_REQUIRE(result.is<ir::VerifiedTargetSelection>());
  return zc::mv(result.get<ir::VerifiedTargetSelection>());
}

package::VerifiedPackageCompilationRequest compilationRequest(
    const ir::TargetRegistrySnapshot& registry, uint32_t detachedRootCount = 0) {
  ZC_REQUIRE(detachedRootCount <= 2);
  zc::Vector<package::VerifiedCompilationRoot> roots;
  roots.add(package::VerifiedCompilationRoot::from(packageKey(), identity::CrateTargetKind::Binary,
                                                   scalar<identity::TargetName>("app"_zc), 2026,
                                                   false, mainPath()));
  if (detachedRootCount >= 1) {
    roots.add(package::VerifiedCompilationRoot::from(
        packageKey(), identity::CrateTargetKind::Library,
        scalar<identity::TargetName>("detached"_zc), 2026, false, detachedPath("detached.zom"_zc)));
  }
  if (detachedRootCount == 2) {
    roots.add(
        package::VerifiedCompilationRoot::from(packageKey(), identity::CrateTargetKind::Library,
                                               scalar<identity::TargetName>("independent"_zc), 2026,
                                               false, detachedPath("independent.zom"_zc)));
  }
  auto result = package::VerifiedPackageCompilationRequest::from(
      zc::mv(roots), targetSelection(registry), targetSelection(registry),
      package::SelectedLanguageOptions{}, package::PackageLockMode::PreferLocked);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid borrow-evidence compilation request");
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

package::DigestVerifiedSourceSnapshot sourceSnapshot(uint32_t importCount,
                                                     uint32_t detachedModuleCount = 0) {
  ZC_REQUIRE(importCount <= 2);
  ZC_REQUIRE(detachedModuleCount <= 2);
  auto sourceDirectory = zc::newInMemoryDirectory(zc::nullClock());
  const auto mainSource = importCount == 0 ? "let root = 0;"_zc
                          : importCount == 1
                              ? "import app::first;\nlet root = 0;"_zc
                              : "import app::first;\nimport app::second;\nlet root = 0;"_zc;
  sourceDirectory
      ->openFile(zc::Path({"src"_zc, "main.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll(mainSource);
  if (importCount >= 1) {
    sourceDirectory->openFile(zc::Path({"src"_zc, "first.zom"_zc}), zc::WriteMode::CREATE)
        ->writeAll("module first;\nlet first_value = 1;"_zc);
  }
  if (importCount == 2) {
    sourceDirectory->openFile(zc::Path({"src"_zc, "second.zom"_zc}), zc::WriteMode::CREATE)
        ->writeAll("module second;\nlet second_value = 2;"_zc);
  }
  if (detachedModuleCount >= 1) {
    sourceDirectory->openFile(zc::Path({"src"_zc, "detached.zom"_zc}), zc::WriteMode::CREATE)
        ->writeAll("module detached;\nlet detached_value = 3;"_zc);
  }
  if (detachedModuleCount == 2) {
    sourceDirectory->openFile(zc::Path({"src"_zc, "independent.zom"_zc}), zc::WriteMode::CREATE)
        ->writeAll("module independent;\nlet independent_value = 4;"_zc);
  }
  MemoryFreshDirectoryFactory factory;
  package::SourceDirectoryMaterializer materializer;
  auto result = materializer.materialize(*sourceDirectory, factory);
  ZC_REQUIRE(result.is<package::DigestVerifiedSourceSnapshot>());
  return zc::mv(result.get<package::DigestVerifiedSourceSnapshot>());
}

package::ResolutionOutput resolution(zc::MemoryResource& resource, uint32_t importCount,
                                     uint32_t detachedModuleCount = 0) {
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
                                                  sourceSnapshot(importCount, detachedModuleCount));
  ZC_REQUIRE(record != zc::none);
  zc::Vector<package::ResolverRelease> releases;
  ZC_IF_SOME(value, record) { releases.add(package::ResolverRelease::fromLocal(value)); }
  zc::Vector<package::ResolverRoot> roots;
  roots.add(package::ResolverRoot::from(packageBase(), emptyFeatures(), false, false));
  auto result = package::PackageResolver::resolve(resource, roots, releases);
  ZC_REQUIRE(result.is<package::ResolutionOutput>());
  return zc::mv(result.get<package::ResolutionOutput>());
}

zc::Vector<package::ResolvedPackageSourceSnapshot> resolvedSnapshots(
    uint32_t importCount, uint32_t detachedModuleCount = 0) {
  zc::Vector<package::ResolvedPackageSourceSnapshot> snapshots;
  snapshots.add(package::ResolvedPackageSourceSnapshot::from(
      packageBase(), sourceSnapshot(importCount, detachedModuleCount)));
  return snapshots;
}

class BorrowEvidenceFixture final {
public:
  explicit BorrowEvidenceFixture(uint32_t importCount = 2)
      : session(contextFactory, languageOptions, compilerOptions) {
    auto registry = targetRegistry();
    auto input = driver::VerifiedPackageSessionInput::from(
        compilationRequest(registry), verifiedTargetSelection(registry),
        verifiedTargetSelection(registry),
        resolution(session.getPackageResolutionMemoryResource(), importCount),
        resolvedSnapshots(importCount));
    ZC_REQUIRE(input != zc::none);
    ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }
    const auto roots = session.getFinalizedCompilationRoots();
    ZC_REQUIRE(roots.size() == 1);
    ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
    ZC_REQUIRE(session.parseSources());
    ZC_REQUIRE(session.bindSources());
    ZC_REQUIRE(!session.getDiagnosticEngine().hasErrors());

    const auto boundModules = session.getVerifiedBoundModules();
    ZC_REQUIRE(boundModules.size() == importCount + 1);
    ZC_IF_SOME(registries, session.getIdentityRegistries()) {
      ZC_IF_SOME(fingerprint, session.getSemanticContextFingerprint()) {
        ZC_IF_SOME(constSemanticTypes, session.getSemanticTypeStore()) {
          // Checker construction is intentionally transactional in CompilerSession. This fixture
          // executes the same public verified-builder sequence before body checking, so the test
          // can isolate borrow-evidence verification from later HIR failures.
          auto& semanticTypes = const_cast<type::SemanticTypeStore&>(constSemanticTypes);
          zc::Vector<checker::signature::MarkerShapeModuleInput> markerInputs(boundModules.size());
          for (const auto& bound : boundModules) {
            markerInputs.add(checker::signature::MarkerShapeModuleInput{bound});
          }
          auto shapeResult = checker::signature::MarkerShapeInventoryBuilder::build(
              session.getSemanticContextBrand(), fingerprint, markerInputs.asPtr(), registries);
          ZC_REQUIRE(shapeResult.is<checker::signature::VerifiedMarkerShapeInventory>());
          markerShapes =
              zc::mv(shapeResult).get<checker::signature::VerifiedMarkerShapeInventory>();

          auto policyConfiguration = checker::signature::MarkerPolicyConfiguration::explicitOnly();
          zc::Vector<identity::ModuleId> authorizedPreludeModules;
          ZC_IF_SOME(shapes, markerShapes) {
            auto policyResult = checker::signature::MarkerPolicyRegistryBuilder::build(
                policyConfiguration, shapes, authorizedPreludeModules.asPtr(), registries);
            ZC_REQUIRE(policyResult.is<checker::signature::VerifiedMarkerPolicyRegistry>());
            markerPolicies =
                zc::mv(policyResult).get<checker::signature::VerifiedMarkerPolicyRegistry>();
          }

          ZC_IF_SOME(shapes, markerShapes) {
            ZC_IF_SOME(policies, markerPolicies) {
              for (const auto& bound : boundModules) {
                auto signatureResult = checker::signature::SignatureFactsBuilder::build(
                    checker::signature::SignatureFactsBuildInput{bound, registries, semanticTypes,
                                                                 shapes, policies});
                ZC_REQUIRE(signatureResult.is<checker::signature::VerifiedSignatureFacts>());
                signatureFacts.add(
                    zc::mv(signatureResult).get<checker::signature::VerifiedSignatureFacts>());

                zc::Vector<checker::cross_module::ImportedSignatureModule> importedModules;
                for (const auto& source : moduleInterfaces) {
                  bool requested = false;
                  for (const auto& import : bound.resolvedImports()) {
                    if (import.sourceModule() == source.module() &&
                        import.sourceRevision().digest() ==
                            source.bindingSurface().revision().digest()) {
                      requested = true;
                      break;
                    }
                  }
                  if (!requested) { continue; }
                  auto projected = source.projectImportedSignatures(
                      bound, checker::cross_module::SignatureViewOrigin::ExplicitImport,
                      zc::ArrayPtr<
                          const checker::cross_module::ImportedDefinitionBindingSelection>(),
                      zc::ArrayPtr<const checker::cross_module::ImportedModuleTargetSelection>(),
                      registries, semanticTypes);
                  ZC_REQUIRE(projected != zc::none);
                  ZC_IF_SOME(module, projected) { importedModules.add(zc::mv(module)); }
                }
                auto imported = checker::cross_module::ImportedSignatureViewBuilder::build(
                    session.getSemanticContextBrand(), fingerprint, bound.module(),
                    zc::mv(importedModules), registries);
                ZC_REQUIRE(imported != zc::none);
                ZC_IF_SOME(view, imported) { importedViews.add(zc::mv(view)); }

                const auto& signatures = signatureFacts.back();
                const auto& importedView = importedViews.back();
                auto borrowResult = checker::borrow::BorrowInterfaceBuilder::build(
                    checker::borrow::BorrowInterfaceBuildInput{
                        session.getSemanticContextBrand(), fingerprint, bound.module(),
                        signatures.revision(), importedView.revision(), signatures.signatures(),
                        zc::ArrayPtr<const checker::signature::SemanticSignature>(), registries,
                        semanticTypes});
                ZC_REQUIRE(borrowResult.is<checker::borrow::VerifiedBorrowInterfaceSurface>());
                auto interfaceResult = ModuleInterfaceVerifier::build(ModuleInterfaceBuildInput{
                    bound, signatures, importedView, policies,
                    zc::mv(borrowResult).get<checker::borrow::VerifiedBorrowInterfaceSurface>(),
                    registries, semanticTypes});
                ZC_REQUIRE(interfaceResult.is<VerifiedModuleInterface>());
                moduleInterfaces.add(zc::mv(interfaceResult).get<VerifiedModuleInterface>());
              }
            }
          }
        }
      }
    }
    ZC_REQUIRE(signatureFacts.size() == importCount + 1);
    ZC_REQUIRE(importedViews.size() == importCount + 1);
    ZC_REQUIRE(moduleInterfaces.size() == importCount + 1);
    requester = 0;
    for (size_t index = 0; index < importedViews.size(); ++index) {
      if (importedViews[index].modules().size() == importCount) requester = index;
    }
    ZC_REQUIRE(importedViews[requester].modules().size() == importCount);
  }

  BorrowEvidenceBuildInput input() const {
    ZC_REQUIRE(session.getIdentityRegistries() != zc::none);
    ZC_IF_SOME(registries, session.getIdentityRegistries()) {
      return BorrowEvidenceBuildInput{signatureFacts[requester], importedViews[requester],
                                      moduleInterfaces[requester], moduleInterfaces.asPtr(),
                                      registries};
    }
    ZC_UNREACHABLE
  }

  BorrowEvidenceCandidate candidate() const {
    auto result = BorrowEvidenceBuilder::build(input());
    ZC_REQUIRE(result.is<BorrowEvidenceCandidate>());
    return zc::mv(result.get<BorrowEvidenceCandidate>());
  }

  VerifiedBorrowEvidence evidence() const {
    auto result = BorrowEvidenceVerifier::verify(candidate(), input());
    ZC_REQUIRE(result.is<VerifiedBorrowEvidence>());
    return zc::mv(result.get<VerifiedBorrowEvidence>());
  }

private:
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  driver::CompilerSession session;
  zc::Maybe<checker::signature::VerifiedMarkerShapeInventory> markerShapes;
  zc::Maybe<checker::signature::VerifiedMarkerPolicyRegistry> markerPolicies;
  zc::Vector<checker::signature::VerifiedSignatureFacts> signatureFacts;
  zc::Vector<checker::cross_module::ImportedSignatureView> importedViews;
  zc::Vector<VerifiedModuleInterface> moduleInterfaces;
  size_t requester = 0;
};

class RepositoryEvidenceFixture final {
public:
  RepositoryEvidenceFixture() : session(contextFactory, languageOptions, compilerOptions) {
    auto registry = targetRegistry();
    auto input = driver::VerifiedPackageSessionInput::from(
        compilationRequest(registry, 2), verifiedTargetSelection(registry),
        verifiedTargetSelection(registry),
        resolution(session.getPackageResolutionMemoryResource(), 0, 2), resolvedSnapshots(0, 2));
    ZC_REQUIRE(input != zc::none);
    ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }
    const auto roots = session.getFinalizedCompilationRoots();
    ZC_REQUIRE(roots.size() == 3);
    for (const auto& root : roots) { ZC_REQUIRE(session.addVerifiedPackageRoot(root) != zc::none); }
    ZC_REQUIRE(session.parseSources());
    ZC_REQUIRE(session.bindSources());
    ZC_REQUIRE(session.checkSources());
    ZC_REQUIRE(!session.getDiagnosticEngine().hasErrors());
    ZC_REQUIRE(session.getVerifiedSignatureFacts().size() == 3);
    ZC_REQUIRE(session.getImportedSignatureViews().size() == 3);
    ZC_REQUIRE(session.getVerifiedModuleInterfaces().size() == 3);
  }

  BorrowEvidenceBuildInput input(size_t index = 0) const {
    ZC_REQUIRE(index < session.getVerifiedSignatureFacts().size());
    ZC_IF_SOME(registries, session.getIdentityRegistries()) {
      return BorrowEvidenceBuildInput{session.getVerifiedSignatureFacts()[index],
                                      session.getImportedSignatureViews()[index],
                                      session.getVerifiedModuleInterfaces()[index],
                                      session.getVerifiedModuleInterfaces(), registries};
    }
    ZC_UNREACHABLE
  }

  VerifiedBorrowEvidence evidence(size_t index = 0) const {
    auto candidate = BorrowEvidenceBuilder::build(input(index));
    ZC_REQUIRE(candidate.is<BorrowEvidenceCandidate>());
    auto result = BorrowEvidenceVerifier::verify(zc::mv(candidate).get<BorrowEvidenceCandidate>(),
                                                 input(index));
    ZC_REQUIRE(result.is<VerifiedBorrowEvidence>());
    return zc::mv(result).get<VerifiedBorrowEvidence>();
  }

  identity::RegistryBrand substitutionBrand() const {
    return checkedFacts().substitutionStore().issuer();
  }

  identity::RegistryBrand witnessBrand() const { return checkedFacts().witnessStore().issuer(); }

  identity::SemanticContextBrand context() const noexcept {
    return session.getSemanticContextBrand();
  }

  const identity::SemanticIdentityRegistrySet& registries() const {
    ZC_IF_SOME(value, session.getIdentityRegistries()) { return value; }
    ZC_UNREACHABLE
  }

  size_t evidenceCount() const noexcept { return session.getVerifiedSignatureFacts().size(); }

private:
  const checker::checked::VerifiedCheckedFacts& checkedFacts() const {
    auto repository = session.getCheckedFactsRepository();
    ZC_REQUIRE(repository != zc::none);
    const auto leases = session.getCheckedEvidenceLeases();
    ZC_REQUIRE(leases.size() == 3);
    ZC_IF_SOME(value, repository) {
      auto facts = value.lookup(leases[0]);
      ZC_IF_SOME(result, facts) { return result; }
    }
    ZC_FAIL_REQUIRE("missing checked facts for borrow-evidence repository fixture");
  }

  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  driver::CompilerSession session;
};

const BorrowEvidenceInvariantFact& rejected(const BorrowEvidenceVerificationResult& result) {
  ZC_REQUIRE(result.is<BorrowEvidenceInvariantRejected>());
  const auto& rejection = result.get<BorrowEvidenceInvariantRejected>();
  ZC_REQUIRE(rejection.failures.size() != 0);
  return rejection.failures[0];
}

}  // namespace

ZC_TEST("BorrowEvidenceRevision reproduces the normative 176-byte empty oracle") {
  const uint8_t module[] = {0xa1};
  const zc::ArrayPtr<const LocalBorrowSummaryRevisionFrame> noSummaries;
  const zc::ArrayPtr<const ImportedBorrowRevisionFrame> noImports;
  auto encoded = BorrowEvidenceCanonicalCodec::encodeFramed(
      repeatedDigest(0x00), module, repeatedDigest(0x22), noSummaries, repeatedDigest(0x33),
      repeatedDigest(0x44), noImports);
  ZC_REQUIRE(encoded != zc::none);
  ZC_IF_SOME(bytes, encoded) { ZC_EXPECT(bytes.size() == 176); }
  auto revision = BorrowEvidenceRevision::computeFramed(
      repeatedDigest(0x00), module, repeatedDigest(0x22), noSummaries, repeatedDigest(0x33),
      repeatedDigest(0x44), noImports);
  ZC_REQUIRE(revision != zc::none);
  ZC_IF_SOME(value, revision) {
    ZC_EXPECT(zc::encodeHex(value.digest().bytes()) ==
              "a4b1178e2b47c87d5805e76aec3b2949ce24b08df62430da439a2feedfe61242"_zc);
  }
}

ZC_TEST("BorrowEvidenceCanonicalCodec orders summaries by expanded callable key") {
  const uint8_t module[] = {0xa1};
  const uint8_t shorterKey[] = {0x02};
  const uint8_t longerKey[] = {0x01, 0xff};
  const uint8_t recordForShorter[] = {0x00};
  const uint8_t recordForLonger[] = {0xff};
  const zc::ArrayPtr<const ImportedBorrowRevisionFrame> noImports;

  const LocalBorrowSummaryRevisionFrame keyOrdered[] = {
      {longerKey, recordForLonger},
      {shorterKey, recordForShorter},
  };
  auto accepted = BorrowEvidenceCanonicalCodec::encodeFramed(
      repeatedDigest(0x00), module, repeatedDigest(0x22), keyOrdered, repeatedDigest(0x33),
      repeatedDigest(0x44), noImports);
  ZC_EXPECT(accepted != zc::none);
  ZC_IF_SOME(bytes, accepted) { ZC_EXPECT(bytes.size() == 194); }

  const LocalBorrowSummaryRevisionFrame recordOrdered[] = {
      {shorterKey, recordForShorter},
      {longerKey, recordForLonger},
  };
  auto rejected = BorrowEvidenceCanonicalCodec::encodeFramed(
      repeatedDigest(0x00), module, repeatedDigest(0x22), recordOrdered, repeatedDigest(0x33),
      repeatedDigest(0x44), noImports);
  ZC_EXPECT(rejected == zc::none);
}

ZC_TEST("BorrowEvidenceCanonicalCodec rejects an empty imported module key") {
  const uint8_t module[] = {0xa1};
  const zc::ArrayPtr<const LocalBorrowSummaryRevisionFrame> noSummaries;
  const ImportedBorrowRevisionFrame emptyKeyImport[] = {
      {zc::ArrayPtr<const uint8_t>(), repeatedDigest(0x33), repeatedDigest(0x44)},
  };
  auto rejected = BorrowEvidenceCanonicalCodec::encodeFramed(
      repeatedDigest(0x00), module, repeatedDigest(0x22), noSummaries, repeatedDigest(0x33),
      repeatedDigest(0x44), emptyKeyImport);
  ZC_EXPECT(rejected == zc::none);
}

ZC_TEST("BorrowEvidenceVerifier accepts exact local and imported inventories") {
  for (uint32_t importCount = 0; importCount <= 2; ++importCount) {
    BorrowEvidenceFixture fixture(importCount);
    auto candidate = fixture.candidate();
    ZC_EXPECT(candidate.localSummaries.size() == 0);
    ZC_EXPECT(candidate.importedSurfaces.size() == importCount);
    auto result = BorrowEvidenceVerifier::verify(zc::mv(candidate), fixture.input());
    ZC_REQUIRE(result.is<VerifiedBorrowEvidence>());
    const auto& evidence = result.get<VerifiedBorrowEvidence>();
    ZC_EXPECT(evidence.localSummaries().size() == 0);
    ZC_EXPECT(evidence.importedSurfaces().size() == importCount);
  }
}

ZC_TEST("BorrowEvidenceVerifier rejects counts order duplicates and malformed bytes") {
  BorrowEvidenceFixture fixture;

  auto missing = fixture.candidate();
  missing.importedSurfaces.removeLast();
  auto missingResult = BorrowEvidenceVerifier::verify(zc::mv(missing), fixture.input());
  ZC_EXPECT(rejected(missingResult).kind == ir::IrFailureKind::MissingRequiredFact);
  ZC_EXPECT(missingResult.get<BorrowEvidenceInvariantRejected>().failures.size() == 1);

  auto missingBoth = fixture.candidate();
  missingBoth.importedSurfaces.removeLast();
  missingBoth.importedSurfaces.removeLast();
  auto missingBothResult = BorrowEvidenceVerifier::verify(zc::mv(missingBoth), fixture.input());
  ZC_EXPECT(rejected(missingBothResult).kind == ir::IrFailureKind::MissingRequiredFact);
  ZC_EXPECT(missingBothResult.get<BorrowEvidenceInvariantRejected>().failures.size() == 2);

  BorrowEvidenceFixture foreignFixture;
  auto foreign = foreignFixture.candidate();
  auto replaced = fixture.candidate();
  replaced.importedSurfaces[0].module = foreign.module;
  replaced.importedSurfaces[1].module = foreign.importedSurfaces[0].module;
  auto replacedResult = BorrowEvidenceVerifier::verify(zc::mv(replaced), fixture.input());
  ZC_EXPECT(rejected(replacedResult).kind == ir::IrFailureKind::MissingRequiredFact);
  ZC_EXPECT(replacedResult.get<BorrowEvidenceInvariantRejected>().failures.size() == 2);

  auto additionalBoth = fixture.candidate();
  additionalBoth.importedSurfaces.add(ImportedBorrowSurfaceCandidate{
      foreign.module, additionalBoth.importedSurfaces[0].surface.clone()});
  additionalBoth.importedSurfaces.add(ImportedBorrowSurfaceCandidate{
      foreign.importedSurfaces[0].module, additionalBoth.importedSurfaces[1].surface.clone()});
  auto additionalBothResult =
      BorrowEvidenceVerifier::verify(zc::mv(additionalBoth), fixture.input());
  ZC_EXPECT(rejected(additionalBothResult).kind == ir::IrFailureKind::AdditionalFact);
  ZC_EXPECT(additionalBothResult.get<BorrowEvidenceInvariantRejected>().failures.size() == 2);

  auto duplicate = fixture.candidate();
  duplicate.importedSurfaces.add(ImportedBorrowSurfaceCandidate{
      duplicate.importedSurfaces[0].module, duplicate.importedSurfaces[0].surface.clone()});
  auto duplicateResult = BorrowEvidenceVerifier::verify(zc::mv(duplicate), fixture.input());
  ZC_EXPECT(rejected(duplicateResult).kind == ir::IrFailureKind::AdditionalFact);

  auto duplicateBoth = fixture.candidate();
  duplicateBoth.importedSurfaces.add(ImportedBorrowSurfaceCandidate{
      duplicateBoth.importedSurfaces[0].module, duplicateBoth.importedSurfaces[0].surface.clone()});
  duplicateBoth.importedSurfaces.add(ImportedBorrowSurfaceCandidate{
      duplicateBoth.importedSurfaces[1].module, duplicateBoth.importedSurfaces[1].surface.clone()});
  auto duplicateBothResult = BorrowEvidenceVerifier::verify(zc::mv(duplicateBoth), fixture.input());
  ZC_EXPECT(rejected(duplicateBothResult).kind == ir::IrFailureKind::AdditionalFact);
  ZC_EXPECT(duplicateBothResult.get<BorrowEvidenceInvariantRejected>().failures.size() == 2);

  auto reversed = fixture.candidate();
  auto first = zc::mv(reversed.importedSurfaces[0]);
  reversed.importedSurfaces[0] = zc::mv(reversed.importedSurfaces[1]);
  reversed.importedSurfaces[1] = zc::mv(first);
  auto reversedResult = BorrowEvidenceVerifier::verify(zc::mv(reversed), fixture.input());
  ZC_EXPECT(rejected(reversedResult).kind == ir::IrFailureKind::CanonicalCodecMismatch);

  auto malformed = fixture.candidate();
  ZC_REQUIRE(malformed.canonicalRecord.size() != 0);
  malformed.canonicalRecord[0] ^= 0x01;
  auto malformedResult = BorrowEvidenceVerifier::verify(zc::mv(malformed), fixture.input());
  ZC_EXPECT(rejected(malformedResult).kind == ir::IrFailureKind::CanonicalCodecMismatch);
}

ZC_TEST("BorrowEvidenceVerifier rejects stale revisions and mismatched embedded surfaces") {
  BorrowEvidenceFixture fixture;

  auto stale = fixture.candidate();
  stale.ownInterfaceRevision = stale.importedSurfaces[0].surface.interfaceRevision();
  auto staleResult = BorrowEvidenceVerifier::verify(zc::mv(stale), fixture.input());
  ZC_EXPECT(rejected(staleResult).kind == ir::IrFailureKind::InputRevisionMismatch);

  auto mismatchedKey = fixture.candidate();
  mismatchedKey.importedSurfaces[0].surface = ImportedBorrowSurface(
      mismatchedKey.module, mismatchedKey.importedSurfaces[0].surface.interfaceRevision(),
      mismatchedKey.importedSurfaces[0].surface.surface().clone());
  auto mismatchedKeyResult = BorrowEvidenceVerifier::verify(zc::mv(mismatchedKey), fixture.input());
  ZC_EXPECT(rejected(mismatchedKeyResult).kind == ir::IrFailureKind::InvalidFact);

  auto mismatchedSurface = fixture.candidate();
  const auto& ownInterface = fixture.input().ownInterface;
  mismatchedSurface.importedSurfaces[0].surface =
      ImportedBorrowSurface(mismatchedSurface.importedSurfaces[0].module,
                            mismatchedSurface.importedSurfaces[0].surface.interfaceRevision(),
                            ownInterface.borrowSurface().clone());
  auto mismatchedSurfaceResult =
      BorrowEvidenceVerifier::verify(zc::mv(mismatchedSurface), fixture.input());
  ZC_EXPECT(rejected(mismatchedSurfaceResult).kind == ir::IrFailureKind::InvalidFact);
}

ZC_TEST("BorrowEvidenceRepository rejects duplicate foreign and swapped leases") {
  RepositoryEvidenceFixture fixture;
  ZC_REQUIRE(fixture.evidenceCount() == 3);
  size_t smallest = 0;
  size_t largest = 0;
  for (size_t index = 1; index < fixture.evidenceCount(); ++index) {
    const auto module = fixture.input(index).localSignatureFacts.module();
    if (expandedModuleLess(module, fixture.input(smallest).localSignatureFacts.module(),
                           fixture.registries())) {
      smallest = index;
    }
    if (expandedModuleLess(fixture.input(largest).localSignatureFacts.module(), module,
                           fixture.registries())) {
      largest = index;
    }
  }
  ZC_REQUIRE(smallest != largest);
  size_t remaining = 0;
  while (remaining == smallest || remaining == largest) ++remaining;
  ZC_REQUIRE(remaining < fixture.evidenceCount());

  auto firstRepository =
      BorrowEvidenceRepository::create(fixture.context(), fixture.substitutionBrand(), 2);
  auto secondRepository =
      BorrowEvidenceRepository::create(fixture.context(), fixture.witnessBrand(), 2);
  ZC_REQUIRE(firstRepository != zc::none);
  ZC_REQUIRE(secondRepository != zc::none);

  ZC_IF_SOME(first, firstRepository) {
    auto adopted = first.adopt(fixture.evidence(largest), fixture.registries());
    ZC_REQUIRE(adopted.is<VerifiedBorrowEvidenceLease>());
    const auto& lease = adopted.get<VerifiedBorrowEvidenceLease>();
    auto retainedLookup = first.lookup(lease);
    ZC_REQUIRE(retainedLookup.isResolved());
    const auto& retained = retainedLookup.evidence();
    const auto retainedModule = retained.module();
    const auto retainedRevision = retained.revision();

    auto insertedBefore = first.adopt(fixture.evidence(smallest), fixture.registries());
    ZC_REQUIRE(insertedBefore.is<VerifiedBorrowEvidenceLease>());
    ZC_EXPECT(retained.module() == retainedModule);
    ZC_EXPECT(retained.revision().digest() == retainedRevision.digest());
    ZC_EXPECT(first.lookup(lease).isResolved());

    auto reissued = first.lease(lease.key().module, lease.key().revision);
    ZC_REQUIRE(reissued != zc::none);
    ZC_IF_SOME(value, reissued) { ZC_EXPECT(first.lookup(value).isResolved()); }
    ZC_IF_SOME(second, secondRepository) {
      auto swapped = second.lookup(lease);
      ZC_EXPECT(!swapped.isResolved());
      ZC_EXPECT(swapped.rejectionKind() == ir::IrFailureKind::InvalidFact);
    }

    auto duplicate = first.adopt(fixture.evidence(largest), fixture.registries());
    ZC_REQUIRE(duplicate.is<BorrowEvidenceRepositoryRejected>());
    ZC_EXPECT(duplicate.get<BorrowEvidenceRepositoryRejected>().kind ==
              ir::IrFailureKind::AdditionalFact);

    auto overCapacity = first.adopt(fixture.evidence(remaining), fixture.registries());
    ZC_REQUIRE(overCapacity.is<BorrowEvidenceRepositoryRejected>());
    ZC_EXPECT(overCapacity.get<BorrowEvidenceRepositoryRejected>().kind ==
              ir::IrFailureKind::AdditionalFact);

    RepositoryEvidenceFixture foreignFixture;
    auto foreignRepository = BorrowEvidenceRepository::create(
        foreignFixture.context(), foreignFixture.substitutionBrand(), 1);
    ZC_REQUIRE(foreignRepository != zc::none);
    ZC_IF_SOME(foreign, foreignRepository) {
      auto rejectedLease = foreign.lookup(lease);
      ZC_EXPECT(!rejectedLease.isResolved());
      ZC_EXPECT(rejectedLease.rejectionKind() == ir::IrFailureKind::InvalidFact);
    }
  }
}

}  // namespace zomlang::compiler::driver::borrow_evidence
