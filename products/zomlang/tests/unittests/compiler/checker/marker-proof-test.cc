// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/checker/marker-proof.h"

#include "zc/core/time.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/checker/body-checker.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/driver/coherence-builder.h"
#include "zomlang/compiler/driver/compiler-session.h"
#include "zomlang/compiler/driver/imported-signature-view-projector.h"
#include "zomlang/compiler/driver/module-interface.h"
#include "zomlang/compiler/driver/package/manifest-parser.h"
#include "zomlang/compiler/driver/package/source-record.h"
#include "zomlang/tests/unittests/compiler/driver/core-library-test-fixture.h"

namespace zomlang::compiler::checker::marker {
namespace {

namespace package = driver::package;

constexpr zc::StringPtr kMarkerProofSource = R"zom(
interface Structural {}
struct GenericBox<T> { pair: (T, T); }
enum GenericChoice<T> { Single(T), Pair(T, T), Empty }
struct PlainStruct { value: i32; }
enum PlainEnum { Value(i32), Empty }
struct SelfCycle { next: SelfCycle; }
struct LeftCycle { right: RightCycle; }
struct RightCycle { left: LeftCycle; }
)zom"_zc;

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto result = Scalar::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid marker-proof fixture scalar");
}

identity::SortedFeatureSet emptyFeatures() {
  zc::Vector<identity::FeatureName> values;
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid marker-proof fixture feature set");
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
  ZC_FAIL_REQUIRE("invalid marker-proof target projection");
}

package::RegisteredTargetProfileName targetProfileName() {
  auto result = package::RegisteredTargetProfileName::from("host"_zc);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid marker-proof target profile name");
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
  ZC_FAIL_REQUIRE("invalid marker-proof target registry");
}

package::RegisteredTargetSelection targetSelection(const ir::TargetRegistrySnapshot& registry) {
  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  ZC_IF_SOME(targets, service) {
    auto result = targets.select(zc::none, package::PackagePanicStrategy::Unwind);
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("invalid marker-proof target selection");
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
  ZC_FAIL_REQUIRE("invalid marker-proof compilation request");
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

class MarkerProofFixture final {
public:
  explicit MarkerProofFixture(zc::StringPtr sourceText)
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
    ZC_REQUIRE(driver::core_library_test::userBoundModuleCount(session, registries()) == 1);

    zc::Vector<signature::MarkerShapeModuleInput> shapeInputs;
    for (const auto& candidate : session.getVerifiedBoundModules()) {
      shapeInputs.add(signature::MarkerShapeModuleInput{candidate});
    }
    auto shapeResult = signature::MarkerShapeInventoryBuilder::build(
        session.getSemanticContextBrand(), contextFingerprint(), shapeInputs.asPtr(), registries());
    ZC_REQUIRE(shapeResult.is<signature::VerifiedMarkerShapeInventory>());
    markerShapes = zc::mv(shapeResult).get<signature::VerifiedMarkerShapeInventory>();

    auto markerKey = registries().definitions().lookup(definition("Structural"_zc));
    ZC_REQUIRE(markerKey != zc::none);
    ZC_IF_SOME(key, markerKey) {
      zc::Vector<signature::MarkerStructuralSubject> structuralSubjects;
      structuralSubjects.add(signature::MarkerStructuralSubject::Tuple);
      structuralSubjects.add(signature::MarkerStructuralSubject::NominalStruct);
      structuralSubjects.add(signature::MarkerStructuralSubject::NominalEnum);
      zc::Vector<signature::PrimitiveKind> builtinPrimitives;
      builtinPrimitives.add(signature::PrimitiveKind::I32);
      zc::Vector<signature::MarkerPolicyReferenceConfiguration> referenceRequirements;
      zc::Vector<signature::MarkerPolicyConfigurationEntry> policyEntries;
      policyEntries.add(signature::MarkerPolicyConfigurationEntry{
          key.clone(), zc::mv(structuralSubjects), zc::mv(builtinPrimitives),
          zc::mv(referenceRequirements)});
      auto configuration = signature::MarkerPolicyConfiguration::from(zc::mv(policyEntries));
      ZC_REQUIRE(configuration != zc::none);
      ZC_IF_SOME(value, configuration) {
        zc::Vector<identity::ModuleId> authorizedPreludeModules;
        authorizedPreludeModules.add(boundModule().module());
        auto policyResult = signature::MarkerPolicyRegistryBuilder::build(
            value, markerShapeInventory(), authorizedPreludeModules.asPtr(), registries());
        ZC_REQUIRE(policyResult.is<signature::VerifiedMarkerPolicyRegistry>());
        markerPolicies = zc::mv(policyResult).get<signature::VerifiedMarkerPolicyRegistry>();
      }
    }

    zc::Vector<signature::VerifiedSignatureFacts> moduleSignatures;
    zc::Vector<cross_module::ImportedSignatureView> moduleImportedViews;
    zc::Vector<driver::VerifiedModuleInterface> interfaces;
    zc::Maybe<size_t> userModuleIndex;
    for (const auto& candidate : session.getVerifiedBoundModules()) {
      auto signatureResult = signature::SignatureFactsBuilder::build(
          signature::SignatureFactsBuildInput{candidate, registries(), semanticTypes(),
                                              markerShapeInventory(), markerPolicyRegistry()});
      ZC_REQUIRE(signatureResult.is<signature::VerifiedSignatureFacts>());
      moduleSignatures.add(zc::mv(signatureResult).get<signature::VerifiedSignatureFacts>());

      auto importedResult = driver::ImportedSignatureViewProjector::build(
          candidate, interfaces.asPtr(), registries(), semanticTypes());
      ZC_REQUIRE(importedResult != zc::none);
      ZC_IF_SOME(value, importedResult) { moduleImportedViews.add(zc::mv(value)); }

      const auto& signatures = moduleSignatures.back();
      const auto& imported = moduleImportedViews.back();
      auto borrowResult = borrow::BorrowInterfaceBuilder::build(borrow::BorrowInterfaceBuildInput{
          session.getSemanticContextBrand(), contextFingerprint(), candidate.module(),
          signatures.revision(), imported.revision(), signatures.signatures(),
          zc::ArrayPtr<const signature::SemanticSignature>(), registries(), semanticTypes()});
      ZC_REQUIRE(borrowResult.is<borrow::VerifiedBorrowInterfaceSurface>());
      auto interfaceResult =
          driver::ModuleInterfaceVerifier::build(driver::ModuleInterfaceBuildInput{
              candidate, signatures, imported, markerPolicyRegistry(),
              zc::mv(borrowResult).get<borrow::VerifiedBorrowInterfaceSurface>(), registries(),
              semanticTypes()});
      ZC_REQUIRE(interfaceResult.is<driver::VerifiedModuleInterface>());
      interfaces.add(zc::mv(interfaceResult).get<driver::VerifiedModuleInterface>());
      if (candidate.module() == boundModule().module()) {
        ZC_REQUIRE(userModuleIndex == zc::none);
        userModuleIndex = interfaces.size() - 1;
      }
    }
    ZC_REQUIRE(interfaces.size() == registries().modules().size());
    ZC_REQUIRE(userModuleIndex != zc::none);
    auto coherenceResult = driver::CoherenceBuilder::build(
        driver::CoherenceBuildInput{session.getSemanticContextBrand(), contextFingerprint(),
                                    markerPolicyRegistry(), interfaces.asPtr(), registries()});
    ZC_REQUIRE(coherenceResult.is<coherence::CoherenceFrozen>());
    coherenceFacts = zc::mv(coherenceResult).get<coherence::CoherenceFrozen>();
    ZC_IF_SOME(index, userModuleIndex) {
      signatureFacts = zc::mv(moduleSignatures[index]);
      importedSignatures = zc::mv(moduleImportedViews[index]);
    }

    auto inventoryResult = body::BodyFactRequirementInventoryBuilder::build(boundModule());
    ZC_REQUIRE(inventoryResult.is<body::VerifiedBodyFactRequirementInventory>());
    bodyRequirements = zc::mv(inventoryResult).get<body::VerifiedBodyFactRequirementInventory>();
  }

  identity::DefId definition(zc::StringPtr name) const {
    for (const auto& entry : boundModule().definitions().definitions()) {
      auto record = registries().definitions().lookupRecord(entry.definition);
      ZC_IF_SOME(value, record) {
        if (value.name() == name) return entry.definition;
      }
    }
    ZC_FAIL_REQUIRE("missing marker-proof definition fixture");
  }

  identity::SemanticTypeId primitive(type::semantic::PrimitiveKind kind) {
    return intern(type::semantic::TypeData(type::semantic::PrimitiveTypeData{kind}));
  }

  identity::SemanticTypeId tuple(identity::SemanticTypeId first, identity::SemanticTypeId second) {
    zc::Vector<identity::SemanticTypeId> elements;
    elements.add(first);
    elements.add(second);
    return intern(type::semantic::TypeData(type::semantic::TupleTypeData{zc::mv(elements)}));
  }

  identity::SemanticTypeId nominal(zc::StringPtr name,
                                   zc::ArrayPtr<const identity::SemanticTypeId> arguments = {}) {
    zc::Vector<identity::SemanticTypeId> values(arguments.size());
    values.addAll(arguments);
    return intern(type::semantic::TypeData(
        type::semantic::NominalTypeData{definition(name), zc::mv(values)}));
  }

  MarkerProofResult prove(identity::SemanticTypeId subject) {
    auto crateKey = registries().crates().lookup(boundModule().crate());
    ZC_REQUIRE(crateKey != zc::none);
    ZC_IF_SOME(crate, crateKey) {
      body::BodyCheckingInput bodyInput{boundModule(),
                                        verifiedSignatureFacts(),
                                        importedSignatureView(),
                                        coherenceView(),
                                        registries(),
                                        semanticTypes(),
                                        bodyRequirementInventory(),
                                        crate.semanticOptions()};
      auto input = MarkerProofInput::from(bodyInput, markerPolicyRegistry());
      ZC_REQUIRE(input != zc::none);
      ZC_IF_SOME(value, input) {
        MarkerProofEngine engine(zc::mv(value));
        return engine.prove(definition("Structural"_zc), subject);
      }
    }
    ZC_UNREACHABLE
  }

  size_t semanticTypeCount() { return semanticTypes().size(); }

private:
  const binder::VerifiedBoundModuleInput& boundModule() const {
    return driver::core_library_test::soleUserBoundModule(session, registries());
  }

  const identity::SemanticIdentityRegistrySet& registries() const {
    ZC_IF_SOME(value, session.getIdentityRegistries()) { return value; }
    ZC_UNREACHABLE
  }

  const identity::SemanticContextFingerprint& contextFingerprint() const {
    ZC_IF_SOME(value, session.getSemanticContextFingerprint()) { return value; }
    ZC_UNREACHABLE
  }

  type::SemanticTypeStore& semanticTypes() {
    ZC_IF_SOME(value, session.getSemanticTypeStore()) {
      return const_cast<type::SemanticTypeStore&>(value);
    }
    ZC_UNREACHABLE
  }

  identity::SemanticTypeId intern(type::semantic::TypeData&& data) {
    auto canonical = semanticTypes().canonicalizeClosed(zc::mv(data));
    ZC_REQUIRE(canonical.is<type::semantic::CanonicalTypeData>());
    auto result =
        semanticTypes().intern(zc::mv(canonical).get<type::semantic::CanonicalTypeData>());
    ZC_REQUIRE(result.is<type::SemanticTypeInterned>());
    return result.get<type::SemanticTypeInterned>().id;
  }

  const signature::VerifiedMarkerShapeInventory& markerShapeInventory() const {
    ZC_IF_SOME(value, markerShapes) { return value; }
    ZC_UNREACHABLE
  }

  const signature::VerifiedMarkerPolicyRegistry& markerPolicyRegistry() const {
    ZC_IF_SOME(value, markerPolicies) { return value; }
    ZC_UNREACHABLE
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

  const body::VerifiedBodyFactRequirementInventory& bodyRequirementInventory() const {
    ZC_IF_SOME(value, bodyRequirements) { return value; }
    ZC_UNREACHABLE
  }

  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  driver::CompilerSession session;
  zc::Maybe<signature::VerifiedMarkerShapeInventory> markerShapes;
  zc::Maybe<signature::VerifiedMarkerPolicyRegistry> markerPolicies;
  zc::Maybe<signature::VerifiedSignatureFacts> signatureFacts;
  zc::Maybe<cross_module::ImportedSignatureView> importedSignatures;
  zc::Maybe<coherence::CoherenceFrozen> coherenceFacts;
  zc::Maybe<body::VerifiedBodyFactRequirementInventory> bodyRequirements;
};

const signature::StructuralMarkerEvidence& structuralEvidence(const MarkerProofResult& result) {
  ZC_REQUIRE(result.is<MarkerProofPositive>());
  const auto& evidence = result.get<MarkerProofPositive>().proof.evidence.variant();
  ZC_REQUIRE(evidence.is<signature::StructuralMarkerEvidence>());
  return evidence.get<signature::StructuralMarkerEvidence>();
}

}  // namespace

ZC_TEST("MarkerProofEngine substitutes and interns generic nominal components") {
  MarkerProofFixture fixture(kMarkerProofSource);
  const auto i32 = fixture.primitive(type::semantic::PrimitiveKind::I32);
  const auto i32Pair = fixture.tuple(i32, i32);
  const identity::SemanticTypeId arguments[] = {i32};
  const auto genericBox = fixture.nominal("GenericBox"_zc, zc::arrayPtr(arguments));
  const auto genericChoice = fixture.nominal("GenericChoice"_zc, zc::arrayPtr(arguments));
  const auto typeCountBeforeProof = fixture.semanticTypeCount();

  auto boxResult = fixture.prove(genericBox);
  const auto& boxEvidence = structuralEvidence(boxResult);
  ZC_REQUIRE(boxEvidence.components.size() == 1);
  ZC_EXPECT(boxEvidence.components[0].componentType == i32Pair);
  ZC_EXPECT(boxEvidence.components[0].supportingFact.subject == i32Pair);
  ZC_REQUIRE(boxEvidence.components[0].path.size() == 1);
  ZC_EXPECT(boxEvidence.components[0].path[0].variant().is<signature::NominalFieldStep>());

  auto choiceResult = fixture.prove(genericChoice);
  const auto& choiceEvidence = structuralEvidence(choiceResult);
  ZC_REQUIRE(choiceEvidence.components.size() == 3);
  for (const auto& component : choiceEvidence.components) {
    ZC_EXPECT(component.componentType == i32);
    ZC_EXPECT(component.supportingFact.subject == i32);
    ZC_REQUIRE(component.path.size() == 1);
    ZC_EXPECT(component.path[0].variant().is<signature::EnumVariantPayloadStep>());
  }
  ZC_EXPECT(fixture.semanticTypeCount() == typeCountBeforeProof);
}

ZC_TEST("MarkerProofEngine proves nominal structs and enums structurally") {
  MarkerProofFixture fixture(kMarkerProofSource);
  const auto i32 = fixture.primitive(type::semantic::PrimitiveKind::I32);
  const auto plainStruct = fixture.nominal("PlainStruct"_zc);
  const auto plainEnum = fixture.nominal("PlainEnum"_zc);

  auto structResult = fixture.prove(plainStruct);
  const auto& structEvidence = structuralEvidence(structResult);
  ZC_REQUIRE(structEvidence.components.size() == 1);
  ZC_EXPECT(structEvidence.components[0].componentType == i32);
  ZC_REQUIRE(structEvidence.components[0].path.size() == 1);
  ZC_EXPECT(structEvidence.components[0].path[0].variant().is<signature::NominalFieldStep>());

  auto enumResult = fixture.prove(plainEnum);
  const auto& enumEvidence = structuralEvidence(enumResult);
  ZC_REQUIRE(enumEvidence.components.size() == 1);
  ZC_EXPECT(enumEvidence.components[0].componentType == i32);
  ZC_REQUIRE(enumEvidence.components[0].path.size() == 1);
  ZC_EXPECT(enumEvidence.components[0].path[0].variant().is<signature::EnumVariantPayloadStep>());
}

ZC_TEST("MarkerProofEngine rejects pure self and mutual nominal cycles") {
  MarkerProofFixture fixture(kMarkerProofSource);
  const auto selfCycle = fixture.nominal("SelfCycle"_zc);
  const auto leftCycle = fixture.nominal("LeftCycle"_zc);
  const auto rightCycle = fixture.nominal("RightCycle"_zc);

  ZC_EXPECT(fixture.prove(selfCycle).is<MarkerProofUnsatisfied>());
  ZC_EXPECT(fixture.prove(selfCycle).is<MarkerProofUnsatisfied>());
  ZC_EXPECT(fixture.prove(leftCycle).is<MarkerProofUnsatisfied>());
  ZC_EXPECT(fixture.prove(rightCycle).is<MarkerProofUnsatisfied>());
}

}  // namespace zomlang::compiler::checker::marker
