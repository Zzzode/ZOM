// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/checker/marker-proof.h"

#include "zc/core/time.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/checker/body-checker.h"
#include "zomlang/compiler/checker/checker-identity-authority.h"
#include "zomlang/compiler/diagnostics/core/diagnostic-engine.h"
#include "zomlang/compiler/driver/coherence-builder.h"
#include "zomlang/compiler/driver/compiler-session.h"
#include "zomlang/compiler/driver/imported-signature-view-projector.h"
#include "zomlang/compiler/driver/module-interface.h"
#include "zomlang/compiler/driver/package/manifest-parser.h"
#include "zomlang/compiler/driver/package/source-record.h"
#include "zomlang/compiler/ownership/surface-admission.h"
#include "zomlang/tests/unittests/compiler/driver/core-library-test-fixture.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::checker::marker {
namespace {

namespace package = driver::package;

constexpr zc::StringPtr kMarkerProofSource = R"zom(
struct GenericBox<T> { pair: (T, T); }
enum GenericChoice<T> { Single(T), Pair(T, T), Empty }
struct GenericContainers<T> { fixed: [T; 4]; dynamic: T[]; }
struct NestedBox<T> { value: GenericBox<T>; }
struct PlainStruct { value: i32; }
enum PlainEnum { Value(i32), Empty }
interface LocalBehavior { fun act(); }
impl LocalBehavior for PlainStruct {}
interface EnumBehavior { fun act(); }
impl EnumBehavior for PlainEnum {}
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

identity::SourceSpan codecSpan(uint64_t start, uint64_t end) {
  auto snapshot = identity::ImmutableSourceSnapshot::from(tests::test_identity_detail::source(),
                                                          zc::heapArray("module-interface"_zcb));
  ZC_REQUIRE(snapshot != zc::none);
  ZC_IF_SOME(value, snapshot) {
    auto span = value.span(start, end);
    ZC_REQUIRE(span != zc::none);
    return zc::mv(ZC_REQUIRE_NONNULL(span));
  }
  ZC_UNREACHABLE
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

    zc::Maybe<identity::DefinitionKey> markerKey;
    for (const auto& boundModule : identities.modules()) {
      for (const auto& definition : boundModule.definitions().definitions()) {
        if (definition.record.name() != "Copy"_zc) continue;
        ZC_REQUIRE(markerKey == zc::none);
        markerKey = definition.key.clone();
      }
    }
    ZC_REQUIRE(markerKey != zc::none);
    ZC_IF_SOME(key, markerKey) {
      auto markerEntry = identities.definition(key);
      ZC_REQUIRE(markerEntry != zc::none);
      identity::DefId authorityMarker;
      zc::Maybe<identity::ModuleId> authorityOwner;
      ZC_IF_SOME(entry, markerEntry) {
        authorityMarker = entry.handle();
        auto owner = identities.module(entry.record().module());
        ZC_REQUIRE(owner != zc::none);
        ZC_IF_SOME(value, owner) { authorityOwner = value.handle(); }
      }
      ZC_REQUIRE(authorityMarker.isValid());
      ZC_REQUIRE(authorityOwner != zc::none);
      ZC_REQUIRE(markerShapeInventory().shape(authorityMarker) ==
                 signature::InterfaceMarkerShape::ClosedMarker);
      zc::Vector<signature::MarkerStructuralSubject> structuralSubjects;
      structuralSubjects.add(signature::MarkerStructuralSubject::Tuple);
      structuralSubjects.add(signature::MarkerStructuralSubject::Object);
      structuralSubjects.add(signature::MarkerStructuralSubject::FixedArray);
      structuralSubjects.add(signature::MarkerStructuralSubject::NominalStruct);
      structuralSubjects.add(signature::MarkerStructuralSubject::NominalEnum);
      zc::Vector<signature::PrimitiveKind> builtinPrimitives;
      builtinPrimitives.add(signature::PrimitiveKind::I32);
      zc::Vector<signature::MarkerPolicyReferenceConfiguration> referenceRequirements;
      referenceRequirements.add(signature::MarkerPolicyReferenceConfiguration{
          type::semantic::Mutability::Const, key.clone()});
      zc::Vector<type::semantic::Mutability> rawPointerMutabilities;
      rawPointerMutabilities.add(type::semantic::Mutability::Const);
      rawPointerMutabilities.add(type::semantic::Mutability::Mutable);
      zc::Vector<signature::MarkerPolicyConfigurationEntry> policyEntries;
      policyEntries.add(signature::MarkerPolicyConfigurationEntry{
          key.clone(), zc::mv(structuralSubjects), zc::mv(builtinPrimitives),
          zc::mv(referenceRequirements), zc::mv(rawPointerMutabilities)});
      auto configuration = signature::MarkerPolicyConfiguration::from(zc::mv(policyEntries));
      ZC_REQUIRE(configuration != zc::none);
      ZC_IF_SOME(value, configuration) {
        zc::Vector<identity::ModuleId> authorizedPreludeModules;
        ZC_IF_SOME(owner, authorityOwner) { authorizedPreludeModules.add(owner); }
        auto policyResult = signature::MarkerPolicyRegistryBuilder::build(
            userModule, value, markerShapeInventory(), authorizedPreludeModules.asPtr(),
            identities);
        ZC_REQUIRE(policyResult.is<signature::VerifiedMarkerPolicyRegistry>());
        markerPolicies = zc::mv(policyResult).get<signature::VerifiedMarkerPolicyRegistry>();
      }
    }

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
      if (candidate.module() == boundModule().module()) {
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
    moduleInterfaces = zc::mv(interfaces);
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
      if (entry.record.name() == name) return entry.definition;
    }
    ZC_FAIL_REQUIRE("missing marker-proof definition fixture");
  }

  identity::DefId coreDefinition(zc::StringPtr name) const {
    for (const auto& module : ZC_REQUIRE_NONNULL(identityAuthority).modules()) {
      for (const auto& definition : module.definitions().definitions()) {
        if (definition.record.name() == name) return definition.definition;
      }
    }
    ZC_FAIL_REQUIRE("missing marker-proof core definition fixture");
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

  identity::SemanticTypeId fixedArray(identity::SemanticTypeId element, uint64_t length) {
    return intern(type::semantic::TypeData(type::semantic::FixedArrayTypeData{element, length}));
  }

  identity::SemanticTypeId object(identity::SemanticTypeId fieldType) {
    zc::Vector<type::semantic::ObjectFieldData> fields;
    fields.add(type::semantic::ObjectFieldData{scalar<identity::SemanticIdentifier>("field"_zc),
                                               fieldType, type::semantic::Mutability::Const,
                                               type::semantic::FieldPresence::Required});
    return intern(type::semantic::TypeData(type::semantic::ObjectTypeData{zc::mv(fields)}));
  }

  identity::SemanticTypeId reference(type::semantic::Mutability mutability,
                                     identity::SemanticTypeId referent) {
    return intern(
        type::semantic::TypeData(type::semantic::ReferenceTypeData{mutability, referent}));
  }

  identity::SemanticTypeId rawPointer(type::semantic::Mutability mutability,
                                      identity::SemanticTypeId pointee) {
    return intern(
        type::semantic::TypeData(type::semantic::RawPointerTypeData{mutability, pointee}));
  }

  MarkerProofResult prove(identity::SemanticTypeId subject) {
    return prove(coreDefinition("Copy"_zc), subject);
  }

  MarkerProofInput proofInput() {
    auto input = proofInputFor(standardMarkers());
    ZC_REQUIRE(input != zc::none);
    return zc::mv(ZC_REQUIRE_NONNULL(input));
  }

  zc::Maybe<MarkerProofInput> proofInputFor(
      const driver::core::VerifiedCoreStandardMarkerAuthority& authority) {
    auto crateEntry = ZC_REQUIRE_NONNULL(identityAuthority).crate(boundModule().crate());
    ZC_REQUIRE(crateEntry != zc::none);
    ZC_IF_SOME(crate, crateEntry) {
      body::BodyCheckingInput bodyInput{boundModule().retain(),
                                        ZC_REQUIRE_NONNULL(identityAuthority),
                                        markerPolicyRegistry(),
                                        authority,
                                        verifiedSignatureFacts(),
                                        importedSignatureView(),
                                        coherenceView(),
                                        semanticTypes(),
                                        bodyRequirementInventory(),
                                        crate.key().semanticOptions()};
      return MarkerProofInput::from(bodyInput);
    }
    ZC_UNREACHABLE
  }

  const driver::core::VerifiedCoreStandardMarkerAuthority& standardMarkerAuthority() const {
    return standardMarkers();
  }

  MarkerProofResult prove(identity::DefId marker, identity::SemanticTypeId subject) {
    MarkerProofEngine engine(proofInput());
    return engine.prove(marker, subject);
  }

  size_t semanticTypeCount() { return semanticTypes().size(); }

  zc::Maybe<zc::Array<uint8_t>> encodeModuleExport() {
    auto name = binder::BindingNameKey::from(
        binder::Namespace::Module, scalar<identity::DeclaredDefinitionName>("dependency"_zc));
    ZC_REQUIRE(name != zc::none);
    driver::ExportedBinding binding{
        binder::BindingTarget::module(userModule),
        zc::mv(ZC_REQUIRE_NONNULL(name)),
        driver::TypeEnrichedBindingTarget(driver::ModuleTypeEnrichedTarget{
            userModule, boundModule().bindingSurface().revision()}),
        binder::VisibilityEnvelope::external(),
        codecSpan(0, 1),
        codecSpan(1, 3),
        zc::Maybe<identity::SourceSpan>(codecSpan(3, 5)),
        codecSpan(3, 5)};
    return driver::ModuleInterfaceCanonicalCodec::encodeExportedBinding(
        binding, ZC_REQUIRE_NONNULL(identityAuthority), semanticTypes());
  }

  zc::Maybe<zc::Array<uint8_t>> encodeSignatureRoot() {
    const auto definitionValue = definition("GenericBox"_zc);
    module_interface::SignatureRootAuthorization root{
        binder::BindingTarget::definition(definitionValue),
        definitionValue,
        binder::VisibilityEnvelope::external(),
        userModule,
        module_interface::ImportedBindingSurfaceRevision(
            module_interface::UserImportedBindingSurfaceRevision{
                boundModule().bindingSurface().revision()}),
        module_interface::SignatureAuthorizationOrigin(
            module_interface::LocalSignatureAuthorization{})};
    return driver::ModuleInterfaceCanonicalCodec::encodeSignatureRoot(
        root, ZC_REQUIRE_NONNULL(identityAuthority));
  }

  zc::ArrayPtr<const signature::ImplHead> implHeads() const {
    return verifiedSignatureFacts().implHeads();
  }

  zc::Maybe<zc::Array<uint8_t>> encodeImplHead(size_t index) {
    const auto heads = implHeads();
    ZC_REQUIRE(index < heads.size());
    return signature::SignatureFactsCanonicalCodec::encodeImplHead(
        heads[index], ZC_REQUIRE_NONNULL(identityAuthority), semanticTypes());
  }

  zc::Maybe<signature::CanonicalTypeHead> canonicalImplHead(size_t index) {
    const auto heads = implHeads();
    ZC_REQUIRE(index < heads.size());
    return signature::SignatureFactsCanonicalCodec::canonicalTypeHead(heads[index].selfType,
                                                                      semanticTypes());
  }

  const coherence::FrozenCoherenceView& frozenCoherenceView() const { return coherenceView(); }

  identity::SemanticContextBrand semanticContext() const {
    return session.getSemanticContextBrand();
  }

  identity::ModuleId requesterModule() const { return boundModule().module(); }

  zc::ArrayPtr<const driver::VerifiedModuleInterface> moduleInterfaceInputs() const {
    return moduleInterfaces.asPtr();
  }

  coherence::CoherenceBuildResult buildCoherence(
      zc::ArrayPtr<const driver::VerifiedModuleInterface> interfaces) const {
    return driver::CoherenceBuilder::build(driver::CoherenceBuildInput{
        session.getSemanticContextBrand(), contextFingerprint(), markerPolicyRegistry(), interfaces,
        ZC_REQUIRE_NONNULL(identityAuthority)});
  }

  coherence::CoherenceBuildResult buildCoherenceWithRepeatedInterface(
      zc::ArrayPtr<const driver::VerifiedModuleInterface> interfaces) const {
    ZC_REQUIRE(interfaces.size() > 1);
    zc::Vector<coherence::CoherenceModuleInput> modules(interfaces.size());
    modules.add(interfaces[0].projectCoherenceInput());
    modules.add(interfaces[0].projectCoherenceInput());
    for (size_t index = 1; index + 1 < interfaces.size(); ++index) {
      modules.add(interfaces[index].projectCoherenceInput());
    }
    coherence::CoherenceCandidate candidate{session.getSemanticContextBrand(),
                                            contextFingerprint().clone(),
                                            markerPolicyRegistry().revision(), zc::mv(modules)};
    return coherence::CoherenceVerifier::verify(zc::mv(candidate), markerPolicyRegistry(),
                                                ZC_REQUIRE_NONNULL(identityAuthority));
  }

  zc::Maybe<cross_module::ImportedSignatureView> buildImportedSignatures(
      zc::ArrayPtr<const driver::VerifiedModuleInterface> interfaces) {
    zc::Vector<driver::VerifiedInterfaceSource> interfaceSources(interfaces.size());
    for (const auto& interface : interfaces) {
      interfaceSources.add(
          driver::VerifiedInterfaceSource(driver::UserVerifiedInterfaceSource{interface}));
    }
    auto admission = ownership::OwnershipSurfaceAdmissionBuilder::admit(boundModule().retain());
    ZC_REQUIRE(admission.is<ownership::OwnershipAdmittedBoundModule>());
    auto admitted = zc::mv(admission).get<ownership::OwnershipAdmittedBoundModule>();
    return driver::ImportedSignatureViewProjector::build(
        admitted, interfaceSources.asPtr(), semanticTypes(), ZC_REQUIRE_NONNULL(identityAuthority));
  }

private:
  const driver::module_graph_query::CheckerBoundModuleView& boundModule() const {
    return ZC_REQUIRE_NONNULL(ZC_REQUIRE_NONNULL(identityAuthority).boundModule(userModule));
  }

  const identity::SemanticContextFingerprint& contextFingerprint() const {
    return ZC_REQUIRE_NONNULL(identityAuthority).fingerprint();
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

  const body::VerifiedBodyFactRequirementInventory& bodyRequirementInventory() const {
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
  zc::Vector<driver::VerifiedModuleInterface> moduleInterfaces;
  zc::Maybe<body::VerifiedBodyFactRequirementInventory> bodyRequirements;
};

const signature::StructuralMarkerEvidence& structuralEvidence(const MarkerProofResult& result) {
  ZC_REQUIRE(result.is<MarkerProofPositive>());
  const auto& evidence = result.get<MarkerProofPositive>().proof.evidence.variant();
  ZC_REQUIRE(evidence.is<signature::StructuralMarkerEvidence>());
  return evidence.get<signature::StructuralMarkerEvidence>();
}

}  // namespace

ZC_TEST("CoherenceVerifier freezes complete module-interface projections") {
  MarkerProofFixture fixture(kMarkerProofSource);
  const auto interfaces = fixture.moduleInterfaceInputs();
  const auto& view = fixture.frozenCoherenceView();

  ZC_REQUIRE(interfaces.size() > 1);
  ZC_EXPECT(view.semanticContext() == fixture.semanticContext());
  ZC_EXPECT(view.moduleInterfaceRevisions().size() == interfaces.size());
  ZC_EXPECT(view.implHeads().size() == 2);
  ZC_EXPECT(view.markerFacts().size() == 0);
  for (size_t index = 0; index < interfaces.size(); ++index) {
    ZC_EXPECT(view.moduleInterfaceRevisions()[index].module == interfaces[index].module());
  }
}

ZC_TEST("CoherenceVerifier retains implementation lookup membership") {
  MarkerProofFixture fixture(kMarkerProofSource);
  const auto& view = fixture.frozenCoherenceView();
  const auto heads = view.implHeads();
  ZC_REQUIRE(heads.size() == 2);

  auto first = view.implementation(heads[0].impl);
  auto second = view.implementation(heads[1].impl);
  auto missing = view.implementation(identity::ImplId());
  ZC_REQUIRE(first != zc::none);
  ZC_REQUIRE(second != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(first).impl == heads[0].impl);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(second).impl == heads[1].impl);
  ZC_EXPECT(missing == zc::none);
}

ZC_TEST("SignatureFactsBuilder retains and re-encodes behavior implementation heads") {
  MarkerProofFixture fixture(kMarkerProofSource);
  const auto heads = fixture.implHeads();

  ZC_REQUIRE(heads.size() == 2);
  for (size_t index = 0; index < heads.size(); ++index) {
    ZC_EXPECT(heads[index].safety == signature::ImplSafety::Safe);
    ZC_EXPECT(heads[index].head.variant().is<signature::NominalTypeHead>());

    auto canonicalHead = fixture.canonicalImplHead(index);
    ZC_REQUIRE(canonicalHead != zc::none);
    ZC_IF_SOME(value, canonicalHead) {
      ZC_EXPECT(value.variant().is<signature::NominalTypeHead>());
      ZC_EXPECT(value.variant().get<signature::NominalTypeHead>().definition ==
                heads[index].head.variant().get<signature::NominalTypeHead>().definition);
    }

    auto encoded = fixture.encodeImplHead(index);
    ZC_REQUIRE(encoded != zc::none);
    ZC_IF_SOME(value, encoded) { ZC_EXPECT(value.size() != 0); }
  }
}

ZC_TEST("SignatureFactsBuilder canonicalizes non-nominal behavior implementation heads") {
  auto source = zc::str(kMarkerProofSource,
                        "interface IntegerBehavior { fun act(); }\n"
                        "impl IntegerBehavior for i32 {}\n"
                        "interface TupleBehavior { fun act(); }\n"
                        "impl TupleBehavior for (i32, bool) {}\n"
                        "interface ArrayBehavior { fun act(); }\n"
                        "impl ArrayBehavior for i32[] {}\n"
                        "interface FixedArrayBehavior { fun act(); }\n"
                        "impl FixedArrayBehavior for [i32; 4] {}\n"
                        "interface ReferenceBehavior { fun act(); }\n"
                        "impl ReferenceBehavior for &i32 {}\n"
                        "interface PointerBehavior { fun act(); }\n"
                        "impl PointerBehavior for *const i32 {}\n"
                        "interface UnionBehavior { fun act(); }\n"
                        "impl UnionBehavior for i32 | bool {}\n"
                        "interface IntersectionBehavior { fun act(); }\n"
                        "impl IntersectionBehavior for i32 & bool {}\n"_zc);
  MarkerProofFixture fixture(source);
  const auto heads = fixture.implHeads();

  ZC_REQUIRE(heads.size() == 10);
  size_t primitiveHeads = 0;
  size_t tupleHeads = 0;
  size_t arrayHeads = 0;
  size_t fixedArrayHeads = 0;
  size_t referenceHeads = 0;
  size_t pointerHeads = 0;
  size_t unionHeads = 0;
  size_t intersectionHeads = 0;
  for (size_t index = 0; index < heads.size(); ++index) {
    if (!heads[index].head.variant().is<signature::PrimitiveTypeHead>()) { continue; }
    ++primitiveHeads;
    auto canonicalHead = fixture.canonicalImplHead(index);
    ZC_REQUIRE(canonicalHead != zc::none);
    ZC_IF_SOME(value, canonicalHead) {
      ZC_REQUIRE(value.variant().is<signature::PrimitiveTypeHead>());
      ZC_EXPECT(value.variant().get<signature::PrimitiveTypeHead>().primitive ==
                signature::PrimitiveKind::I32);
    }
    ZC_EXPECT(fixture.encodeImplHead(index) != zc::none);
  }
  for (const auto& head : heads) {
    if (head.head.variant().is<signature::TupleTypeHead>()) { ++tupleHeads; }
    if (head.head.variant().is<signature::DynamicArrayTypeHead>()) { ++arrayHeads; }
    if (head.head.variant().is<signature::FixedArrayTypeHead>()) { ++fixedArrayHeads; }
    if (head.head.variant().is<signature::ReferenceTypeHead>()) { ++referenceHeads; }
    if (head.head.variant().is<signature::RawPointerTypeHead>()) { ++pointerHeads; }
    if (head.head.variant().is<signature::UnionTypeHead>()) { ++unionHeads; }
    if (head.head.variant().is<signature::IntersectionTypeHead>()) { ++intersectionHeads; }
  }
  ZC_EXPECT(primitiveHeads == 1);
  ZC_EXPECT(tupleHeads == 1);
  ZC_EXPECT(arrayHeads == 1);
  ZC_EXPECT(fixedArrayHeads == 1);
  ZC_EXPECT(referenceHeads == 1);
  ZC_EXPECT(pointerHeads == 1);
  ZC_EXPECT(unionHeads == 1);
  ZC_EXPECT(intersectionHeads == 1);
}

ZC_TEST("CoherenceVerifier rejects an incomplete interface receipt") {
  MarkerProofFixture fixture(kMarkerProofSource);
  const auto interfaces = fixture.moduleInterfaceInputs();
  ZC_REQUIRE(interfaces.size() > 1);

  const auto result = fixture.buildCoherence(interfaces.first(interfaces.size() - 1));
  ZC_REQUIRE(result.is<coherence::CoherenceInvariantRejected>());
  const auto& rejection = result.get<coherence::CoherenceInvariantRejected>();
  ZC_REQUIRE(rejection.failures.size() == 1);
  const auto& failure = rejection.failures[0].variant();
  ZC_REQUIRE(failure.is<signature::CheckerInvariantFact>());
  ZC_EXPECT(failure.get<signature::CheckerInvariantFact>().kind ==
            signature::CheckerInvariantKind::InputReceiptMismatch);
  ZC_EXPECT(failure.get<signature::CheckerInvariantFact>().stage ==
            signature::CheckerInvariantStage::Coherence);

  for (size_t index = 0; index < interfaces.size(); ++index) {
    if (interfaces[index].module() != fixture.requesterModule()) { continue; }
    ZC_EXPECT(fixture.buildImportedSignatures(interfaces.slice(index, index + 1)) == zc::none);
    return;
  }
  ZC_FAIL_REQUIRE("missing requester module interface");
}

ZC_TEST("CoherenceVerifier rejects a repeated module receipt") {
  MarkerProofFixture fixture(kMarkerProofSource);
  const auto interfaces = fixture.moduleInterfaceInputs();
  const auto result = fixture.buildCoherenceWithRepeatedInterface(interfaces);
  ZC_REQUIRE(result.is<coherence::CoherenceInvariantRejected>());
  const auto& rejection = result.get<coherence::CoherenceInvariantRejected>();
  ZC_REQUIRE(rejection.failures.size() == 1);
  const auto& failure = rejection.failures[0].variant();
  ZC_REQUIRE(failure.is<signature::CheckerInvariantFact>());
  ZC_EXPECT(failure.get<signature::CheckerInvariantFact>().kind ==
            signature::CheckerInvariantKind::AdditionalFact);
  ZC_EXPECT(failure.get<signature::CheckerInvariantFact>().stage ==
            signature::CheckerInvariantStage::Coherence);
}

ZC_TEST("MarkerProofEngine substitutes and interns generic nominal components") {
  auto source = zc::str(kMarkerProofSource,
                        "struct GenericPointer<T> { value: *const T; }\n"
                        "struct GenericUnion<T> { value: T | i32; }\n"_zc);
  MarkerProofFixture fixture(source);
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

  const auto nested = fixture.nominal("NestedBox"_zc, zc::arrayPtr(arguments));
  auto nestedResult = fixture.prove(nested);
  const auto& nestedEvidence = structuralEvidence(nestedResult);
  ZC_REQUIRE(nestedEvidence.components.size() == 1);
  ZC_REQUIRE(nestedEvidence.components[0].path.size() == 1);
  ZC_EXPECT(nestedEvidence.components[0].path[0].variant().is<signature::NominalFieldStep>());

  const auto containers = fixture.nominal("GenericContainers"_zc, zc::arrayPtr(arguments));
  ZC_EXPECT(fixture.prove(containers).is<MarkerProofUnsatisfied>());

  const auto pointer = fixture.nominal("GenericPointer"_zc, zc::arrayPtr(arguments));
  ZC_EXPECT(fixture.prove(pointer).is<MarkerProofPositive>());

  const auto unionValue = fixture.nominal("GenericUnion"_zc, zc::arrayPtr(arguments));
  ZC_EXPECT(fixture.prove(unionValue).is<MarkerProofInvariantRejected>());
  ZC_EXPECT(fixture.encodeModuleExport() != zc::none);
  ZC_EXPECT(fixture.encodeSignatureRoot() != zc::none);

  auto retainedInput = fixture.proofInput();
  retainedInput = fixture.proofInput();
  MarkerProofEngine reassignedEngine(zc::mv(retainedInput));
  ZC_EXPECT(reassignedEngine.prove(fixture.coreDefinition("Copy"_zc), genericBox)
                .is<MarkerProofPositive>());
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

ZC_TEST("MarkerProofEngine proves configured arrays and references structurally") {
  MarkerProofFixture fixture(kMarkerProofSource);
  const auto i32 = fixture.primitive(type::semantic::PrimitiveKind::I32);
  const auto array = fixture.fixedArray(i32, 4);
  const auto object = fixture.object(i32);
  const auto reference = fixture.reference(type::semantic::Mutability::Const, i32);
  const auto mutableReference = fixture.reference(type::semantic::Mutability::Mutable, i32);

  auto arrayResult = fixture.prove(array);
  const auto& arrayEvidence = structuralEvidence(arrayResult);
  ZC_REQUIRE(arrayEvidence.components.size() == 1);
  ZC_EXPECT(arrayEvidence.components[0].componentType == i32);
  ZC_REQUIRE(arrayEvidence.components[0].path.size() == 1);
  ZC_EXPECT(arrayEvidence.components[0].path[0].variant().is<signature::ArrayElementStep>());

  auto objectResult = fixture.prove(object);
  const auto& objectEvidence = structuralEvidence(objectResult);
  ZC_REQUIRE(objectEvidence.components.size() == 1);
  ZC_EXPECT(objectEvidence.components[0].componentType == i32);
  ZC_REQUIRE(objectEvidence.components[0].path.size() == 1);
  ZC_EXPECT(objectEvidence.components[0].path[0].variant().is<signature::ObjectFieldStep>());

  auto referenceResult = fixture.prove(reference);
  const auto& referenceEvidence = structuralEvidence(referenceResult);
  ZC_REQUIRE(referenceEvidence.components.size() == 1);
  ZC_EXPECT(referenceEvidence.components[0].componentType == i32);
  ZC_REQUIRE(referenceEvidence.components[0].path.size() == 1);
  ZC_EXPECT(
      referenceEvidence.components[0].path[0].variant().is<signature::ReferenceReferentStep>());

  ZC_EXPECT(fixture.prove(mutableReference).is<MarkerProofUnsatisfied>());
}

ZC_TEST("MarkerProofEngine proves configured raw pointers") {
  MarkerProofFixture fixture(kMarkerProofSource);
  const auto i32 = fixture.primitive(type::semantic::PrimitiveKind::I32);
  const auto constPointer = fixture.rawPointer(type::semantic::Mutability::Const, i32);
  const auto mutablePointer = fixture.rawPointer(type::semantic::Mutability::Mutable, i32);

  auto constResult = fixture.prove(constPointer);
  auto mutableResult = fixture.prove(mutablePointer);
  const auto& constEvidence = structuralEvidence(constResult);
  const auto& mutableEvidence = structuralEvidence(mutableResult);
  ZC_EXPECT(constEvidence.components.empty());
  ZC_EXPECT(mutableEvidence.components.empty());
}

ZC_TEST("MarkerProofEngine rejects an unknown marker identity") {
  MarkerProofFixture fixture(kMarkerProofSource);
  const auto i32 = fixture.primitive(type::semantic::PrimitiveKind::I32);
  auto result = fixture.prove(identity::DefId(), i32);
  ZC_REQUIRE(result.is<MarkerProofInvariantRejected>());
  ZC_REQUIRE(result.get<MarkerProofInvariantRejected>().failures.size() == 1);
  const auto& failure = result.get<MarkerProofInvariantRejected>().failures[0].variant();
  ZC_REQUIRE(failure.is<signature::CheckerInvariantFact>());
  ZC_EXPECT(failure.get<signature::CheckerInvariantFact>().kind ==
            signature::CheckerInvariantKind::InvalidFact);
}

ZC_TEST("MarkerProofInput rejects a standard marker authority from another session") {
  MarkerProofFixture fixture(kMarkerProofSource);
  MarkerProofFixture foreign(kMarkerProofSource);
  ZC_EXPECT(fixture.proofInputFor(foreign.standardMarkerAuthority()) == zc::none);
}

ZC_TEST("MarkerProofInput retains its bound-module lease after body input destruction") {
  MarkerProofFixture fixture(kMarkerProofSource);
  const auto i32 = fixture.primitive(type::semantic::PrimitiveKind::I32);
  auto input = fixture.proofInput();
  MarkerProofEngine engine(zc::mv(input));
  ZC_EXPECT(engine.prove(fixture.coreDefinition("Copy"_zc), i32).is<MarkerProofPositive>());
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
