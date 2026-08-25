// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zc/core/filesystem.h"
#include "zc/core/time.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/checker/body/body-checker.h"
#include "zomlang/compiler/checker/checker-identity-authority.h"
#include "zomlang/compiler/checker/facts/checked-facts-repository.h"
#include "zomlang/compiler/checker/facts/dispatch-facts.h"
#include "zomlang/compiler/checker/inference/checked-facts.h"
#include "zomlang/compiler/checker/operator-kind.h"
#include "zomlang/compiler/driver/interface/coherence-builder.h"
#include "zomlang/compiler/driver/interface/imported-signature-view-projector.h"
#include "zomlang/compiler/driver/interface/module-interface.h"
#include "zomlang/compiler/driver/package/manifest-parser.h"
#include "zomlang/compiler/driver/package/source-record.h"
#include "zomlang/compiler/driver/session/compiler-session.h"
#include "zomlang/compiler/ownership/surface-admission.h"
#include "zomlang/tests/unittests/compiler/driver/core/core-library-test-fixture.h"

namespace zomlang::compiler::checker::body {
namespace {

namespace package = driver::package;

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto result = Scalar::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid primitive-binary fixture scalar");
}

identity::SortedFeatureSet emptyFeatures() {
  zc::Vector<identity::FeatureName> values;
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid primitive-binary fixture feature set");
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
  ZC_FAIL_REQUIRE("invalid primitive-binary target projection");
}

package::RegisteredTargetProfileName targetProfileName() {
  auto result = package::RegisteredTargetProfileName::from("host"_zc);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid primitive-binary target profile name");
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
  ZC_FAIL_REQUIRE("invalid primitive-binary target registry");
}

package::RegisteredTargetSelection targetSelection(const ir::TargetRegistrySnapshot& registry) {
  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  ZC_IF_SOME(targets, service) {
    auto result = targets.select(zc::none, package::PackagePanicStrategy::Unwind);
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("invalid primitive-binary target selection");
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
  ZC_FAIL_REQUIRE("invalid primitive-binary compilation request");
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

/// \brief Drives the checker fact chain up to the body checker for one user module.
///
/// This deliberately stops before ownership surface admission / MIR: the goal is
/// to observe the body checker's published `CheckedFactsCandidate` for a scalar
/// equality comparison of two parameters, not to lower it.
class PrimitiveBinaryFixture final {
public:
  explicit PrimitiveBinaryFixture(zc::StringPtr sourceText)
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
    ZC_REQUIRE(!session.getDiagnosticEngine().hasErrors());
    identityAuthority = session.materializeCheckerIdentityAuthority();
    ZC_REQUIRE(identityAuthority != zc::none);
    const auto& identities = ZC_REQUIRE_NONNULL(identityAuthority);
    coreLibrary = driver::core_library_test::materializeCoreLibrary(session, identities);
    ZC_REQUIRE(coreLibrary != zc::none);
    ZC_REQUIRE(driver::core_library_test::userBoundModuleCount(identities) == 1);
    userModule = driver::core_library_test::soleUserBoundModule(identities).module();

    zc::Vector<ownership::OwnershipAdmittedBoundModule> admittedModules(
        identities.modules().size());
    zc::Vector<signature::MarkerShapeModuleInput> shapeInputs(identities.modules().size());
    for (const auto& candidate : identities.modules()) {
      auto admission = ownership::OwnershipSurfaceAdmissionBuilder::admit(candidate.retain());
      ZC_REQUIRE(admission.is<ownership::OwnershipAdmittedBoundModule>());
      admittedModules.add(zc::mv(admission).get<ownership::OwnershipAdmittedBoundModule>());
      shapeInputs.add(signature::MarkerShapeModuleInput{admittedModules.back()});
    }
    auto shapeResult = signature::MarkerShapeInventoryBuilder::build(
        session.getSemanticContextBrand(), contextFingerprint(), userModule, shapeInputs.asPtr(),
        identities);
    ZC_REQUIRE(shapeResult.is<signature::VerifiedMarkerShapeInventory>());
    markerShapes = zc::mv(shapeResult).get<signature::VerifiedMarkerShapeInventory>();

    const auto configuration = signature::MarkerPolicyConfiguration::explicitOnly();
    zc::Vector<identity::ModuleId> authorizedPreludeModules;
    auto policyResult = signature::MarkerPolicyRegistryBuilder::build(
        userModule, configuration, markerShapeInventory(), authorizedPreludeModules.asPtr(),
        identities);
    ZC_REQUIRE(policyResult.is<signature::VerifiedMarkerPolicyRegistry>());
    markerPolicies = zc::mv(policyResult).get<signature::VerifiedMarkerPolicyRegistry>();

    zc::Vector<signature::VerifiedSignatureFacts> moduleSignatures;
    zc::Vector<cross_module::ImportedSignatureView> moduleImportedViews;
    zc::Vector<driver::VerifiedModuleInterface> interfaces;
    zc::Maybe<size_t> userModuleIndex;
    for (size_t candidateIndex = 0; candidateIndex < identities.modules().size();
         ++candidateIndex) {
      const auto& candidate = identities.modules()[candidateIndex];
      auto signatureResult =
          signature::SignatureFactsBuilder::build(signature::SignatureFactsBuildInput{
              admittedModules[candidateIndex], semanticTypes(), markerShapeInventory(),
              markerPolicyRegistry(), identities});
      ZC_REQUIRE(signatureResult.is<signature::VerifiedSignatureFacts>());
      moduleSignatures.add(zc::mv(signatureResult).get<signature::VerifiedSignatureFacts>());

      zc::Vector<driver::VerifiedInterfaceSource> interfaceSources(interfaces.size());
      for (const auto& interface : interfaces) {
        interfaceSources.add(
            driver::VerifiedInterfaceSource(driver::UserVerifiedInterfaceSource{interface}));
      }
      auto importedResult = driver::ImportedSignatureViewProjector::build(
          admittedModules[candidateIndex], interfaceSources.asPtr(), semanticTypes(), identities);
      ZC_REQUIRE(importedResult != zc::none);
      ZC_IF_SOME(value, importedResult) { moduleImportedViews.add(zc::mv(value)); }

      const auto& signatures = moduleSignatures.back();
      const auto& imported = moduleImportedViews.back();
      auto borrowResult = borrow::BorrowInterfaceBuilder::build(borrow::BorrowInterfaceBuildInput{
          session.getSemanticContextBrand(), contextFingerprint(), candidate.module(),
          signatures.revision(), imported.revision(), signatures.signatures(),
          zc::ArrayPtr<const signature::SemanticSignature>(), identities, semanticTypes()});
      ZC_REQUIRE(borrowResult.is<borrow::VerifiedBorrowInterfaceSurface>());
      auto interfaceResult =
          driver::ModuleInterfaceVerifier::build(driver::ModuleInterfaceBuildInput{
              admittedModules[candidateIndex], signatures, imported, markerPolicyRegistry(),
              zc::mv(borrowResult).get<borrow::VerifiedBorrowInterfaceSurface>(), semanticTypes(),
              identities});
      ZC_REQUIRE(interfaceResult.is<driver::VerifiedModuleInterface>());
      interfaces.add(zc::mv(interfaceResult).get<driver::VerifiedModuleInterface>());
      if (candidate.module() == userModule) {
        ZC_REQUIRE(userModuleIndex == zc::none);
        userModuleIndex = interfaces.size() - 1;
      }
    }
    ZC_REQUIRE(interfaces.size() == identities.modules().size());
    ZC_REQUIRE(userModuleIndex != zc::none);
    auto coherenceResult = driver::CoherenceBuilder::build(
        driver::CoherenceBuildInput{session.getSemanticContextBrand(), contextFingerprint(),
                                    markerPolicyRegistry(), interfaces.asPtr(), identities});
    ZC_REQUIRE(coherenceResult.is<coherence::CoherenceFrozen>());
    coherenceFacts = zc::mv(coherenceResult).get<coherence::CoherenceFrozen>();
    ZC_IF_SOME(index, userModuleIndex) {
      signatureFacts = zc::mv(moduleSignatures[index]);
      importedSignatures = zc::mv(moduleImportedViews[index]);
    }

    auto inventoryResult = BodyFactRequirementInventoryBuilder::build(boundModule());
    ZC_REQUIRE(inventoryResult.is<VerifiedBodyFactRequirementInventory>());
    bodyRequirements = zc::mv(inventoryResult).get<VerifiedBodyFactRequirementInventory>();
  }

  BodyCheckingResult runBodyChecker() {
    BodyChecker checker;
    return checker.check(bodyInput(), factStoreBrands());
  }

  /// \brief Runs the body checker, verifies the checked facts, and returns the
  /// adopted verified facts plus lease so dispatch projection can be exercised.
  const checked::VerifiedCheckedFacts& adoptVerifiedFacts() {
    auto result = runBodyChecker();
    ZC_REQUIRE(result.is<checked::CheckedFactsCandidate>());
    auto candidate = zc::mv(result).get<checked::CheckedFactsCandidate>();
    auto verified = checked::CheckedFactsVerifier::verify(zc::mv(candidate), verificationInput());
    ZC_REQUIRE(verified.is<checked::VerifiedCheckedFacts>());
    repository = zc::heap<checked::CheckedFactsRepository>(session.getSemanticContextBrand());
    auto adoption = repository->adopt(zc::mv(verified).get<checked::VerifiedCheckedFacts>());
    ZC_REQUIRE(adoption.is<checked::CheckedEvidenceLease>());
    lease = zc::heap<checked::CheckedEvidenceLease>(
        zc::mv(adoption).get<checked::CheckedEvidenceLease>());
    auto facts = repository->lookup(*lease);
    ZC_REQUIRE(facts != zc::none);
    ZC_IF_SOME(value, facts) { return value; }
    ZC_UNREACHABLE
  }

  dispatch::VerifiedDispatchFacts buildDispatchFacts(const checked::VerifiedCheckedFacts& facts) {
    auto inventoryResult =
        dispatch::DispatchSiteInventoryBuilder::build(boundModule(), bodyRequirementInventory());
    ZC_REQUIRE(inventoryResult.is<dispatch::VerifiedDispatchSiteInventory>());
    auto inventory = zc::mv(inventoryResult).get<dispatch::VerifiedDispatchSiteInventory>();
    auto build = dispatch::DispatchFactsBuilder::build(inventory, contextFingerprint(), *lease,
                                                       facts, ZC_REQUIRE_NONNULL(identityAuthority),
                                                       semanticTypes());
    ZC_REQUIRE(build.is<dispatch::DispatchFactsCandidate>());
    auto verification = dispatch::DispatchFactsVerifier::verify(
        zc::mv(build).get<dispatch::DispatchFactsCandidate>(),
        dispatch::DispatchFactsVerificationInput{
            contextFingerprint(), userModule, boundModule().parsedModule().source(),
            inventory.requirements(), inventory.nodeProjections(), *lease, facts,
            ZC_REQUIRE_NONNULL(identityAuthority), semanticTypes()});
    ZC_REQUIRE(verification.is<dispatch::VerifiedDispatchFacts>());
    return zc::mv(verification).get<dispatch::VerifiedDispatchFacts>();
  }

  type::SemanticTypeStore& semanticTypes() {
    ZC_IF_SOME(value, session.getSemanticTypeStore()) {
      return const_cast<type::SemanticTypeStore&>(value);
    }
    ZC_UNREACHABLE
  }

  identity::SemanticTypeId primitive(type::semantic::PrimitiveKind kind) {
    auto canonical = semanticTypes().canonicalizeClosed(
        type::semantic::TypeData(type::semantic::PrimitiveTypeData{kind}));
    ZC_REQUIRE(canonical.is<type::semantic::CanonicalTypeData>());
    auto result =
        semanticTypes().intern(zc::mv(canonical).get<type::semantic::CanonicalTypeData>());
    ZC_REQUIRE(result.is<type::SemanticTypeInterned>());
    return result.get<type::SemanticTypeInterned>().id;
  }

private:
  BodyCheckingInput bodyInput() {
    auto crateEntry = ZC_REQUIRE_NONNULL(identityAuthority).crate(boundModule().crate());
    ZC_REQUIRE(crateEntry != zc::none);
    ZC_IF_SOME(crate, crateEntry) {
      return BodyCheckingInput{boundModule().retain(),
                               ZC_REQUIRE_NONNULL(identityAuthority),
                               markerPolicyRegistry(),
                               standardMarkers(),
                               verifiedSignatureFacts(),
                               importedSignatureView(),
                               coherenceView(),
                               semanticTypes(),
                               bodyRequirementInventory(),
                               crate.key().semanticOptions()};
    }
    ZC_UNREACHABLE
  }

  checked::CheckedFactsVerificationInput verificationInput() {
    zc::Vector<identity::DefId> importedDefinitions;
    for (const auto& module : importedSignatureView().modules()) {
      for (const auto& definition : module.lookupDefinitions()) {
        bool duplicate = false;
        for (const auto existing : importedDefinitions) {
          if (existing == definition.definition) {
            duplicate = true;
            break;
          }
        }
        if (!duplicate) { importedDefinitions.add(definition.definition); }
      }
    }
    coherentImpls.clear();
    for (const auto& implementation : coherenceView().implHeads()) {
      coherentImpls.add(implementation.impl);
    }
    verificationImportedDefinitions = zc::mv(importedDefinitions);
    verificationSemanticOptions =
        zc::heap<identity::SemanticCompilerOptionsKey>(crateSemanticOptions());
    return checked::CheckedFactsVerificationInput{
        session.getSemanticContextBrand(),
        contextFingerprint(),
        userModule,
        boundModule().parsedModule().source(),
        boundModule().parsedModule().contentDigest(),
        boundModule().parsedModule().receipt(),
        verifiedSignatureFacts().revision(),
        importedSignatureView().revision(),
        coherenceView().revision(),
        *verificationSemanticOptions,
        bodyRequirementInventory().nodeRequirements(),
        bodyRequirementInventory().definitionRequirements(),
        bodyRequirementInventory().captureRequirements(),
        verificationImportedDefinitions.asPtr(),
        coherentImpls.asPtr(),
        zc::ArrayPtr<const checked::CheckerFailureRef>(),
        boundModule().definitions().ownerLocalBindings(),
        boundModule().definitions().anonymousEntities(),
        ZC_REQUIRE_NONNULL(identityAuthority),
        semanticTypes()};
  }

  identity::SemanticCompilerOptionsKey crateSemanticOptions() {
    auto crateEntry = ZC_REQUIRE_NONNULL(identityAuthority).crate(boundModule().crate());
    ZC_REQUIRE(crateEntry != zc::none);
    ZC_IF_SOME(crate, crateEntry) { return crate.key().semanticOptions(); }
    ZC_UNREACHABLE
  }

  const identity::RegistryBrandIssuer& factStoreBrands() const {
    auto issuer = session.getFactStoreBrandIssuer();
    ZC_REQUIRE(issuer != zc::none);
    ZC_IF_SOME(value, issuer) { return value; }
    ZC_UNREACHABLE
  }

  const driver::module_graph_query::CheckerBoundModuleView& boundModule() const {
    return ZC_REQUIRE_NONNULL(ZC_REQUIRE_NONNULL(identityAuthority).boundModule(userModule));
  }

  const identity::ContextFingerprint& contextFingerprint() const {
    return ZC_REQUIRE_NONNULL(identityAuthority).fingerprint();
  }

  const signature::VerifiedMarkerShapeInventory& markerShapeInventory() const {
    ZC_IF_SOME(value, markerShapes) { return value; }
    ZC_UNREACHABLE
  }

  const signature::VerifiedMarkerPolicyRegistry& markerPolicyRegistry() const {
    ZC_IF_SOME(value, markerPolicies) { return value; }
    ZC_UNREACHABLE
  }

  const driver::core::VerifiedCoreStandardMarkerAuthority& standardMarkers() const {
    return ZC_REQUIRE_NONNULL(coreLibrary).authorityLease().capability().authority();
  }

  const signature::VerifiedSignatureFacts& verifiedSignatureFacts() const {
    ZC_IF_SOME(value, signatureFacts) { return value; }
    ZC_UNREACHABLE
  }

  const cross_module::ImportedSignatureView& importedSignatureView() const {
    ZC_IF_SOME(value, importedSignatures) { return value; }
    ZC_UNREACHABLE
  }

  const coherence::FrozenCoherenceView& coherenceView() const {
    ZC_IF_SOME(value, coherenceFacts) { return value.view; }
    ZC_UNREACHABLE
  }

  const VerifiedBodyFactRequirementInventory& bodyRequirementInventory() const {
    ZC_IF_SOME(value, bodyRequirements) { return value; }
    ZC_UNREACHABLE
  }

  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  driver::CompilerSession session;
  identity::ModuleId userModule;
  zc::Maybe<CheckerIdentityAuthority> identityAuthority;
  zc::Maybe<driver::core::VerifiedCoreLibrary> coreLibrary;
  zc::Maybe<signature::VerifiedMarkerShapeInventory> markerShapes;
  zc::Maybe<signature::VerifiedMarkerPolicyRegistry> markerPolicies;
  zc::Maybe<signature::VerifiedSignatureFacts> signatureFacts;
  zc::Maybe<cross_module::ImportedSignatureView> importedSignatures;
  zc::Maybe<coherence::CoherenceFrozen> coherenceFacts;
  zc::Maybe<VerifiedBodyFactRequirementInventory> bodyRequirements;
  zc::Own<checked::CheckedFactsRepository> repository;
  zc::Own<checked::CheckedEvidenceLease> lease;
  zc::Vector<identity::DefId> verificationImportedDefinitions;
  zc::Vector<identity::ImplId> coherentImpls;
  zc::Own<identity::SemanticCompilerOptionsKey> verificationSemanticOptions;
};

const checked::TypedCallFact& soleEqualityCall(const checked::VerifiedCheckedFacts& facts) {
  zc::Maybe<const checked::TypedCallFact&> found;
  for (const auto& entry : facts.calls().entries()) {
    const auto& selected = entry.value.invocation.selected.variant();
    ZC_REQUIRE(selected.is<checked::PrimitiveCallable>());
    ZC_REQUIRE(found == zc::none);
    found = entry.value;
  }
  ZC_IF_SOME(value, found) { return value; }
  ZC_FAIL_REQUIRE("expected exactly one primitive equality call fact");
}

}  // namespace

ZC_TEST("PrimitiveBinaryOperation.EmitsPrimitiveEqualityCallFactForScalarParameters") {
  PrimitiveBinaryFixture fixture(
      "fun eq(a: i32, b: i32) -> bool { if (a == b) { return true; } else { return false; } }\n"_zc);
  const auto& facts = fixture.adoptVerifiedFacts();

  ZC_REQUIRE(facts.calls().entries().size() == 1);
  const auto& call = soleEqualityCall(facts);
  const auto& selected = call.invocation.selected.variant();
  ZC_REQUIRE(selected.is<checked::PrimitiveCallable>());
  ZC_EXPECT(selected.get<checked::PrimitiveCallable>().operation == PrimitiveOperation::Eq);

  const auto i32 = fixture.primitive(type::semantic::PrimitiveKind::I32);
  const auto boolType = fixture.primitive(type::semantic::PrimitiveKind::Bool);
  ZC_EXPECT(call.invocation.calleeType == i32);
  ZC_EXPECT(call.invocation.successType == boolType);
  ZC_EXPECT(call.invocation.resultType == boolType);
  ZC_EXPECT(call.invocation.receiver == zc::none);
  ZC_REQUIRE(call.invocation.arguments.size() == 2);
  ZC_EXPECT(call.invocation.arguments[0].sourceType == i32);
  ZC_EXPECT(call.invocation.arguments[0].parameterType == i32);
  ZC_EXPECT(call.invocation.arguments[1].sourceType == i32);
  ZC_EXPECT(call.invocation.arguments[1].parameterType == i32);

  bool binaryNodeIsBool = false;
  for (const auto& entry : facts.nodeTypes().entries()) {
    if (entry.key == call.node) binaryNodeIsBool = entry.value == boolType;
  }
  ZC_EXPECT(binaryNodeIsBool);
}

ZC_TEST("PrimitiveBinaryOperation.ProjectsBinaryOperatorDispatchSite") {
  PrimitiveBinaryFixture fixture(
      "fun eq(a: i32, b: i32) -> bool { if (a == b) { return true; } else { return false; } }\n"_zc);
  const auto& facts = fixture.adoptVerifiedFacts();
  auto dispatch = fixture.buildDispatchFacts(facts);

  zc::Maybe<const dispatch::VerifiedDispatchFact&> primitive;
  for (const auto& fact : dispatch.facts()) {
    if (!fact.fact.target.variant().is<dispatch::PrimitiveTarget>()) continue;
    ZC_REQUIRE(primitive == zc::none);
    primitive = fact;
  }
  ZC_REQUIRE(primitive != zc::none);
  ZC_IF_SOME(value, primitive) {
    ZC_EXPECT(value.fact.target.variant().get<dispatch::PrimitiveTarget>().operation ==
              PrimitiveOperation::Eq);
    ZC_EXPECT(value.fact.arguments.size() == 2);
  }
}

ZC_TEST("PrimitiveBinaryOperation.EmitsPrimitiveOrderingCallFactForScalarParameters") {
  PrimitiveBinaryFixture fixture(
      "fun lt(a: i32, b: i32) -> bool { if (a < b) { return true; } else { return false; } }\n"_zc);
  const auto& facts = fixture.adoptVerifiedFacts();

  ZC_REQUIRE(facts.calls().entries().size() == 1);
  const auto& call = soleEqualityCall(facts);
  const auto& selected = call.invocation.selected.variant();
  ZC_REQUIRE(selected.is<checked::PrimitiveCallable>());
  ZC_EXPECT(selected.get<checked::PrimitiveCallable>().operation == PrimitiveOperation::Lt);

  const auto i32 = fixture.primitive(type::semantic::PrimitiveKind::I32);
  const auto boolType = fixture.primitive(type::semantic::PrimitiveKind::Bool);
  ZC_EXPECT(call.invocation.calleeType == i32);
  ZC_EXPECT(call.invocation.successType == boolType);
  ZC_EXPECT(call.invocation.resultType == boolType);
}

ZC_TEST("PrimitiveBinaryOperation.RejectsNonComparisonScalarBinaryOperation") {
  PrimitiveBinaryFixture fixture(
      "fun add(a: i32, b: i32) -> bool { if (a + b) { return true; } else { return false; } }\n"_zc);
  auto result = fixture.runBodyChecker();
  ZC_EXPECT(result.is<checked::CheckedFactsInvariantRejected>());
}

ZC_TEST("PrimitiveBinaryOperation.RejectsMismatchedScalarOperandTypes") {
  PrimitiveBinaryFixture fixture(
      "fun eq(a: i32, b: i64) -> bool { if (a == b) { return true; } else { return false; } }\n"_zc);
  auto result = fixture.runBodyChecker();
  ZC_EXPECT(result.is<checked::CheckedFactsInvariantRejected>());
}

ZC_TEST("PrimitiveBinaryOperation.EmitsPrimitiveEqualityCallFactForParameterAndLiteral") {
  PrimitiveBinaryFixture fixture(
      "fun eq(a: i32) -> bool { if (a == 0) { return true; } else { return false; } }\n"_zc);
  const auto& facts = fixture.adoptVerifiedFacts();

  ZC_REQUIRE(facts.calls().entries().size() == 1);
  const auto& call = soleEqualityCall(facts);
  const auto& selected = call.invocation.selected.variant();
  ZC_REQUIRE(selected.is<checked::PrimitiveCallable>());
  ZC_EXPECT(selected.get<checked::PrimitiveCallable>().operation == PrimitiveOperation::Eq);

  const auto i32 = fixture.primitive(type::semantic::PrimitiveKind::I32);
  const auto boolType = fixture.primitive(type::semantic::PrimitiveKind::Bool);
  ZC_EXPECT(call.invocation.calleeType == i32);
  ZC_EXPECT(call.invocation.resultType == boolType);
  ZC_REQUIRE(call.invocation.arguments.size() == 2);
  ZC_EXPECT(call.invocation.arguments[0].sourceType == i32);
  ZC_EXPECT(call.invocation.arguments[1].sourceType == i32);

  // The literal operand carries a literal-backed CheckedArgumentFact whose node
  // resolves to a scalar-literal node with a matching literal fact.
  bool literalArgumentBacked = false;
  for (const auto& literal : facts.literals().entries()) {
    if (literal.value.node == call.invocation.arguments[1].sourceNode &&
        literal.value.type == i32) {
      literalArgumentBacked = true;
    }
  }
  ZC_EXPECT(literalArgumentBacked);
}

ZC_TEST("PrimitiveBinaryOperation.EmitsPrimitiveEqualityCallFactForLiteralAndParameter") {
  PrimitiveBinaryFixture fixture(
      "fun eq(a: i32) -> bool { if (0 == a) { return true; } else { return false; } }\n"_zc);
  const auto& facts = fixture.adoptVerifiedFacts();

  ZC_REQUIRE(facts.calls().entries().size() == 1);
  const auto& call = soleEqualityCall(facts);
  const auto& selected = call.invocation.selected.variant();
  ZC_REQUIRE(selected.is<checked::PrimitiveCallable>());
  ZC_EXPECT(selected.get<checked::PrimitiveCallable>().operation == PrimitiveOperation::Eq);

  const auto i32 = fixture.primitive(type::semantic::PrimitiveKind::I32);
  ZC_REQUIRE(call.invocation.arguments.size() == 2);
  bool literalArgumentBacked = false;
  for (const auto& literal : facts.literals().entries()) {
    if (literal.value.node == call.invocation.arguments[0].sourceNode &&
        literal.value.type == i32) {
      literalArgumentBacked = true;
    }
  }
  ZC_EXPECT(literalArgumentBacked);
}

ZC_TEST("PrimitiveBinaryOperation.EmitsPrimitiveOrderingCallFactForParameterAndLiteral") {
  PrimitiveBinaryFixture fixture(
      "fun lt(a: i32) -> bool { if (a < 5) { return true; } else { return false; } }\n"_zc);
  const auto& facts = fixture.adoptVerifiedFacts();

  ZC_REQUIRE(facts.calls().entries().size() == 1);
  const auto& call = soleEqualityCall(facts);
  const auto& selected = call.invocation.selected.variant();
  ZC_REQUIRE(selected.is<checked::PrimitiveCallable>());
  ZC_EXPECT(selected.get<checked::PrimitiveCallable>().operation == PrimitiveOperation::Lt);
}

ZC_TEST("PrimitiveBinaryOperation.RejectsMismatchedLiteralOperandType") {
  // The parameter is i32 while the literal is a float; the operand types differ
  // so the comparison fails closed.
  PrimitiveBinaryFixture fixture(
      "fun eq(a: i32) -> bool { if (a == 1.0) { return true; } else { return false; } }\n"_zc);
  auto result = fixture.runBodyChecker();
  ZC_EXPECT(result.is<checked::CheckedFactsInvariantRejected>());
}

ZC_TEST("PrimitiveBinaryOperation.EmitsPrimitiveComparisonCallFactForReturnPositionComparison") {
  // The checker keys on the BinaryExpr node, not its syntactic position, so a
  // `return a < b` body produces the same PrimitiveCallable fact as the
  // conditional-condition form.
  PrimitiveBinaryFixture fixture("fun lt(a: i32, b: i32) -> bool { return a < b; }\n"_zc);
  const auto& facts = fixture.adoptVerifiedFacts();

  ZC_REQUIRE(facts.calls().entries().size() == 1);
  const auto& call = soleEqualityCall(facts);
  const auto& selected = call.invocation.selected.variant();
  ZC_REQUIRE(selected.is<checked::PrimitiveCallable>());
  ZC_EXPECT(selected.get<checked::PrimitiveCallable>().operation == PrimitiveOperation::Lt);

  const auto i32 = fixture.primitive(type::semantic::PrimitiveKind::I32);
  const auto boolType = fixture.primitive(type::semantic::PrimitiveKind::Bool);
  ZC_EXPECT(call.invocation.calleeType == i32);
  ZC_EXPECT(call.invocation.successType == boolType);
  ZC_EXPECT(call.invocation.resultType == boolType);
  ZC_EXPECT(call.invocation.receiver == zc::none);
  ZC_REQUIRE(call.invocation.arguments.size() == 2);
  ZC_EXPECT(call.invocation.arguments[0].sourceType == i32);
  ZC_EXPECT(call.invocation.arguments[1].sourceType == i32);

  bool binaryNodeIsBool = false;
  for (const auto& entry : facts.nodeTypes().entries()) {
    if (entry.key == call.node) binaryNodeIsBool = entry.value == boolType;
  }
  ZC_EXPECT(binaryNodeIsBool);
}

ZC_TEST("PrimitiveBinaryOperation.EmitsPrimitiveArithmeticCallFactWithOperandResultType") {
  // `return a + b` for two i32 parameters produces a PrimitiveCallable{Add}
  // whose result type is the operand type (i32), NOT bool. This is the key
  // difference from a comparison, whose result is always bool.
  PrimitiveBinaryFixture fixture("fun add(a: i32, b: i32) -> i32 { return a + b; }\n"_zc);
  const auto& facts = fixture.adoptVerifiedFacts();

  ZC_REQUIRE(facts.calls().entries().size() == 1);
  const auto& call = soleEqualityCall(facts);
  const auto& selected = call.invocation.selected.variant();
  ZC_REQUIRE(selected.is<checked::PrimitiveCallable>());
  ZC_EXPECT(selected.get<checked::PrimitiveCallable>().operation == PrimitiveOperation::Add);

  const auto i32 = fixture.primitive(type::semantic::PrimitiveKind::I32);
  const auto boolType = fixture.primitive(type::semantic::PrimitiveKind::Bool);
  ZC_EXPECT(call.invocation.calleeType == i32);
  // The result type is the operand type i32, not bool.
  ZC_EXPECT(call.invocation.successType == i32);
  ZC_EXPECT(call.invocation.resultType == i32);
  ZC_EXPECT(call.invocation.resultType != boolType);
  ZC_REQUIRE(call.invocation.arguments.size() == 2);
  ZC_EXPECT(call.invocation.arguments[0].sourceType == i32);
  ZC_EXPECT(call.invocation.arguments[1].sourceType == i32);

  bool binaryNodeIsOperandType = false;
  for (const auto& entry : facts.nodeTypes().entries()) {
    if (entry.key == call.node) binaryNodeIsOperandType = entry.value == i32;
  }
  ZC_EXPECT(binaryNodeIsOperandType);
}

ZC_TEST("PrimitiveBinaryOperation.EmitsPrimitiveBitwiseCallFactWithOperandResultType") {
  // A bitwise operator behaves like an arithmetic operator: the result is the
  // operand type, not bool.
  PrimitiveBinaryFixture fixture("fun band(a: i32, b: i32) -> i32 { return a & b; }\n"_zc);
  const auto& facts = fixture.adoptVerifiedFacts();

  ZC_REQUIRE(facts.calls().entries().size() == 1);
  const auto& call = soleEqualityCall(facts);
  const auto& selected = call.invocation.selected.variant();
  ZC_REQUIRE(selected.is<checked::PrimitiveCallable>());
  ZC_EXPECT(selected.get<checked::PrimitiveCallable>().operation == PrimitiveOperation::BitAnd);

  const auto i32 = fixture.primitive(type::semantic::PrimitiveKind::I32);
  ZC_EXPECT(call.invocation.resultType == i32);
}

ZC_TEST("PrimitiveBinaryOperation.RejectsArithmeticConditionAsNonBool") {
  // An arithmetic result is not bool, so it cannot drive an `if` discriminant;
  // the arithmetic operator stays unsupported in condition position and the body
  // is rejected exactly as `if (a + b)` was before arithmetic returns landed.
  PrimitiveBinaryFixture fixture(
      "fun add(a: i32, b: i32) -> bool { if (a + b) { return true; } else { return false; } }\n"_zc);
  auto result = fixture.runBodyChecker();
  ZC_EXPECT(result.is<checked::CheckedFactsInvariantRejected>());
}

ZC_TEST("PrimitiveBinaryOperation.RejectsLogicalShortCircuitBinaryOperation") {
  // Logical `&&` has short-circuit semantics and is not a primitive binary
  // operation; it stays rejected in this slice.
  PrimitiveBinaryFixture fixture("fun conj(a: bool, b: bool) -> bool { return a && b; }\n"_zc);
  auto result = fixture.runBodyChecker();
  ZC_EXPECT(result.is<checked::CheckedFactsInvariantRejected>());
}

}  // namespace zomlang::compiler::checker::body
