// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/hir/hir-module.h"

#include "zc/core/time.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/checker/checker-identity-authority.h"
#include "zomlang/compiler/diagnostics/core/diagnostic-engine.h"
#include "zomlang/compiler/driver/compiler-session.h"
#include "zomlang/compiler/driver/package/manifest-parser.h"
#include "zomlang/compiler/driver/package/source-record.h"
#include "zomlang/compiler/hir/checked-module.h"
#include "zomlang/compiler/ownership/surface-admission.h"
#include "zomlang/tests/unittests/compiler/driver/core-library-test-fixture.h"

namespace zomlang::compiler::hir {
namespace {

namespace package = driver::package;

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto result = Scalar::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid HIR fixture scalar");
}

identity::SortedFeatureSet emptyFeatures() {
  zc::Vector<identity::FeatureName> values;
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid HIR fixture feature set");
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
  ZC_FAIL_REQUIRE("invalid HIR fixture target projection");
}

package::RegisteredTargetProfileName targetProfileName() {
  auto result = package::RegisteredTargetProfileName::from("host"_zc);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid HIR fixture target profile name");
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
  ZC_FAIL_REQUIRE("invalid HIR fixture target registry");
}

package::RegisteredTargetSelection targetSelection(const ir::TargetRegistrySnapshot& registry) {
  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  ZC_IF_SOME(targets, service) {
    auto result = targets.select(zc::none, package::PackagePanicStrategy::Unwind);
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("invalid HIR fixture target selection");
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
  ZC_FAIL_REQUIRE("invalid HIR fixture compilation request");
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

class HirPipelineFixture final {
public:
  explicit HirPipelineFixture(zc::StringPtr sourceText)
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
    auto identities = session.materializeCheckerIdentityAuthority();
    ZC_REQUIRE(identities != zc::none);
    ZC_REQUIRE(driver::core_library_test::userBoundModuleCount(ZC_REQUIRE_NONNULL(identities)) ==
               1);
    ZC_REQUIRE(session.getVerifiedModuleInterfaces().size() == 1);
    ZC_REQUIRE(session.getCheckedEvidenceLeases().size() == 1);
    ZC_REQUIRE(session.getBorrowEvidenceRepository() != zc::none);
    ZC_REQUIRE(session.getVerifiedHirModules().size() == 1);
  }

  const VerifiedHirModule& hirModule() const { return session.getVerifiedHirModules()[0]; }

  driver::CompilerSession& compilerSession() noexcept { return session; }

private:
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  driver::CompilerSession session;
};

const driver::module_graph_query::CheckerBoundModuleView& checkerBoundModule(
    const checker::CheckerIdentityAuthority& authority, identity::ModuleId module) {
  auto bound = authority.boundModule(module);
  ZC_REQUIRE(bound != zc::none);
  return ZC_REQUIRE_NONNULL(bound);
}

ZC_TEST("CheckedModuleBuilder rejects a foreign checker identity authority") {
  HirPipelineFixture sourceFixture(""_zc);
  HirPipelineFixture foreignFixture(""_zc);
  auto sourceAuthority = sourceFixture.compilerSession().materializeCheckerIdentityAuthority();
  auto foreignAuthority = foreignFixture.compilerSession().materializeCheckerIdentityAuthority();
  ZC_REQUIRE(sourceAuthority != zc::none);
  ZC_REQUIRE(foreignAuthority != zc::none);
  ZC_IF_SOME(sourceIdentities, sourceAuthority) {
    ZC_IF_SOME(foreignIdentities, foreignAuthority) {
      const auto& session = sourceFixture.compilerSession();
      const auto& bound = checkerBoundModule(sourceIdentities, sourceFixture.hirModule().module());
      const auto interfaces = session.getVerifiedModuleInterfaces();
      const auto signatures = session.getVerifiedSignatureFacts();
      const auto importedViews = session.getImportedSignatureViews();
      const auto dispatchFacts = session.getVerifiedDispatchFacts();
      const auto leases = session.getCheckedEvidenceLeases();
      zc::Vector<driver::VerifiedInterfaceSource> interfaceSources(interfaces.size());
      for (const auto& interface : interfaces) {
        interfaceSources.add(
            driver::VerifiedInterfaceSource(driver::UserVerifiedInterfaceSource{interface}));
      }
      auto checkedRepository = session.getCheckedFactsRepository();
      auto borrowRepository = session.getBorrowEvidenceRepository();
      auto semanticTypes = session.getSemanticTypeStore();
      ZC_REQUIRE(interfaces.size() == 1);
      ZC_REQUIRE(signatures.size() == 1);
      ZC_REQUIRE(importedViews.size() == 1);
      ZC_REQUIRE(dispatchFacts.size() == 1);
      ZC_REQUIRE(leases.size() == 1);
      ZC_REQUIRE(checkedRepository != zc::none);
      ZC_REQUIRE(borrowRepository != zc::none);
      ZC_REQUIRE(semanticTypes != zc::none);
      ZC_IF_SOME(repository, checkedRepository) {
        auto lease = repository.lease(bound.module(), leases[0].revision());
        ZC_REQUIRE(lease != zc::none);
        ZC_IF_SOME(evidenceLease, lease) {
          ZC_IF_SOME(borrow, borrowRepository) {
            ZC_IF_SOME(types, semanticTypes) {
              auto admission = ownership::OwnershipSurfaceAdmissionBuilder::admit(bound.retain());
              ZC_REQUIRE(admission.is<ownership::OwnershipAdmittedBoundModule>());
              auto admitted = zc::mv(admission).get<ownership::OwnershipAdmittedBoundModule>();
              auto result = CheckedModuleBuilder::build(CheckedModuleBuildInput{
                  admitted, signatures[0], interfaces[0], importedViews[0],
                  interfaceSources.asPtr(), zc::mv(evidenceLease), repository, dispatchFacts[0],
                  const_cast<driver::borrow_evidence::BorrowEvidenceRepository&>(borrow),
                  foreignIdentities, types});
              ZC_EXPECT(result.isIdentityInvariantRejected());
              return;
            }
          }
        }
      }
    }
  }
  ZC_UNREACHABLE;
}

ZC_TEST("HIR pipeline publishes an exact empty module") {
  HirPipelineFixture fixture(""_zc);
  const auto& module = fixture.hirModule();
  auto authority = fixture.compilerSession().materializeCheckerIdentityAuthority();
  ZC_REQUIRE(authority != zc::none);
  const auto& bound = driver::core_library_test::soleUserBoundModule(ZC_REQUIRE_NONNULL(authority));
  const auto& checkerBound = checkerBoundModule(ZC_REQUIRE_NONNULL(authority), bound.module());
  const auto& interface = fixture.compilerSession().getVerifiedModuleInterfaces()[0];
  auto coherenceInput = interface.projectCoherenceInput();
  auto retainedInterfaceBoundModule = interface.retainBoundModule();
  ZC_EXPECT(coherenceInput.module() == interface.module());
  ZC_EXPECT(coherenceInput.interfaceRevision().digest() == interface.revision().digest());
  ZC_EXPECT(coherenceInput.markerPolicyRegistryRevision().digest() ==
            interface.markerPolicyRegistryRevision().digest());
  ZC_EXPECT(coherenceInput.implHeads().size() == interface.coherenceImplHeads().size());
  ZC_EXPECT(coherenceInput.markerFacts().size() == interface.markerFacts().size());
  ZC_EXPECT(retainedInterfaceBoundModule.semanticContext() == interface.semanticContext());
  ZC_EXPECT(retainedInterfaceBoundModule.module() == interface.module());
  ZC_EXPECT(module.semanticContext() == checkerBound.semanticContext());
  ZC_EXPECT(module.contextFingerprint().digest() == checkerBound.semanticFingerprint().digest());
  ZC_EXPECT(module.compilationUnit() == checkerBound.compilationUnit());
  ZC_EXPECT(module.crate() == checkerBound.crate());
  ZC_EXPECT(module.module() == checkerBound.module());
  ZC_EXPECT(module.sourceContentDigest() == checkerBound.parsedModule().contentDigest());
  ZC_EXPECT(module.parsedModuleReceiptDigest() == checkerBound.parsedModule().receipt().digest());
  const auto& ownInterfaceRevision = module.ownInterface().revision.variant();
  ZC_REQUIRE(ownInterfaceRevision.is<module_interface::UserImportedInterfaceRevision>());
  ZC_EXPECT(
      ownInterfaceRevision.get<module_interface::UserImportedInterfaceRevision>().value.digest() ==
      interface.revision().digest());
  ZC_EXPECT(module.checkedEvidenceLease().revision().digest() ==
            module.checkedFactsRevision().digest());
  ZC_EXPECT(module.borrowEvidenceLease().key().revision.digest() ==
            module.borrowEvidenceRevision().digest());
  auto repository = fixture.compilerSession().getCheckedFactsRepository();
  ZC_REQUIRE(repository != zc::none);
  ZC_IF_SOME(value, repository) {
    ZC_EXPECT(value.lookup(module.checkedEvidenceLease()) != zc::none);
  }
  auto borrowRepository = fixture.compilerSession().getBorrowEvidenceRepository();
  ZC_REQUIRE(borrowRepository != zc::none);
  ZC_IF_SOME(value, borrowRepository) {
    const auto capability = value.capability();
    const auto evidence = capability.lookup(module.borrowEvidenceLease());
    ZC_REQUIRE(evidence.isResolved());
    ZC_EXPECT(evidence.evidence().revision().digest() == module.borrowEvidenceRevision().digest());
  }
  ZC_REQUIRE(module.visibleImportedInterfaces().size() == 1);
  ZC_IF_SOME(prelude, checkerBound.preludeSurface()) {
    ZC_EXPECT(module.visibleImportedInterfaces()[0].module == prelude.module);
  }
  ZC_EXPECT(module.visibleImportedInterfaces()[0]
                .revision.variant()
                .is<module_interface::ToolchainCoreImportedInterfaceRevision>());
  ZC_EXPECT(module.declarations().size() == 0);
  ZC_EXPECT(module.patterns().size() == 0);
  ZC_EXPECT(module.expressions().size() == 0);
  auto dump = module.dump();
  auto repeated = module.dump();
  ZC_REQUIRE(dump != zc::none);
  ZC_REQUIRE(repeated != zc::none);
  ZC_IF_SOME(left, dump) {
    ZC_IF_SOME(right, repeated) {
      ZC_EXPECT(left == right);
      ZC_EXPECT(left.startsWith("zom.hir\n"_zc));
      ZC_EXPECT(left.contains("\ninterface user:"_zc));
      ZC_EXPECT(left.contains(" core:"_zc));
    }
  }
}

ZC_TEST("HIR pipeline lowers one module-scope scalar let without AST identity") {
  HirPipelineFixture fixture("let value = 0;"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.declarations().size() == 1);
  ZC_REQUIRE(module.patterns().size() == 1);
  ZC_REQUIRE(module.expressions().size() == 1);
  const auto& declaration = module.declarations()[0];
  const auto& pattern = module.patterns()[0];
  const auto& expression = module.expressions()[0];
  ZC_EXPECT(declaration.node.ordinal() == 1);
  ZC_EXPECT(pattern.node.ordinal() == 2);
  ZC_EXPECT(expression.node.ordinal() == 3);
  ZC_EXPECT(declaration.pattern == pattern.node);
  ZC_EXPECT(declaration.initializer == expression.node);
  ZC_EXPECT(declaration.definitionKind == identity::DefinitionKind::Static);
  ZC_EXPECT(declaration.constantValue == zc::none);
  ZC_EXPECT(expression.value.tag() == checker::signature::CanonicalConstValueTag::Integer);
  auto dump = module.dump();
  ZC_REQUIRE(dump != zc::none);
  ZC_IF_SOME(text, dump) { ZC_EXPECT(!text.contains("NodeId"_zc)); }
}

ZC_TEST("HIR pipeline retains the exact canonical scalar const value") {
  HirPipelineFixture fixture("const value = 42;"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.declarations().size() == 1);
  ZC_REQUIRE(module.expressions().size() == 1);
  const auto& declaration = module.declarations()[0];
  ZC_EXPECT(declaration.definitionKind == identity::DefinitionKind::Constant);
  ZC_EXPECT(declaration.constantValue != zc::none);
  ZC_EXPECT(module.expressions()[0].value.tag() ==
            checker::signature::CanonicalConstValueTag::Integer);
}

ZC_TEST("HIR pipeline retains verified direct calls separately from literals") {
  HirPipelineFixture fixture(
      "fun helper() -> i32 { return 0; }\n"
      "fun entry() -> i32 { return helper(); }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 2);
  ZC_REQUIRE(module.expressions().size() == 1);
  ZC_REQUIRE(module.calls().size() == 1);
  const auto& helper = module.functions()[0];
  const auto& entry = module.functions()[1];
  const auto& call = module.calls()[0];
  ZC_EXPECT(module.expressions()[0].node == module.returns()[0].value);
  ZC_EXPECT(call.node == module.returns()[1].value);
  ZC_EXPECT(call.callee == helper.definition);
  ZC_EXPECT(call.resultType == entry.resultType);
  auto repository = fixture.compilerSession().getBorrowEvidenceRepository();
  ZC_REQUIRE(repository != zc::none);
  ZC_IF_SOME(value, repository) {
    const auto capability = value.capability();
    const auto evidence = capability.lookup(module.borrowEvidenceLease());
    ZC_REQUIRE(evidence.isResolved());
    const auto summaries = evidence.evidence().localSummaries();
    ZC_REQUIRE(summaries.size() == 2);
    bool foundHelper = false;
    bool foundEntry = false;
    for (const auto& summary : summaries) {
      ZC_EXPECT(summary.directInputs.size() == 0);
      ZC_EXPECT(summary.returnRelation.tag() == checker::borrow::BorrowReturnRelationTag::None);
      if (summary.callable == helper.definition) foundHelper = true;
      if (summary.callable == entry.definition) foundEntry = true;
    }
    ZC_EXPECT(foundHelper);
    ZC_EXPECT(foundEntry);
  }
  auto dump = module.dump();
  ZC_REQUIRE(dump != zc::none);
  ZC_IF_SOME(text, dump) { ZC_EXPECT(text.contains("call h"_zc)); }
}

ZC_TEST("HIR pipeline retains verified scalar direct call arguments") {
  HirPipelineFixture fixture(
      "fun helper(value: i32) -> i32 { return value; }\n"
      "fun entry() -> i32 { return helper(7); }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 2);
  ZC_REQUIRE(module.calls().size() == 1);
  const auto& helper = module.functions()[0];
  const auto& entry = module.functions()[1];
  const auto& call = module.calls()[0];
  ZC_EXPECT(call.callee == helper.definition);
  ZC_EXPECT(call.resultType == entry.resultType);
  ZC_REQUIRE(call.arguments.size() == 1);
  ZC_EXPECT(call.arguments[0].type == helper.parameters[0].type);
  ZC_EXPECT(call.arguments[0].value.tag() == checker::signature::CanonicalConstValueTag::Integer);

  const auto builtMir = fixture.compilerSession().getVerifiedBuiltMirModules();
  ZC_REQUIRE(builtMir.size() == 1);
  zc::Maybe<const mir::MirFunction&> caller;
  for (const auto& function : builtMir[0].functions()) {
    if (function.owner == entry.definition) caller = function;
  }
  ZC_REQUIRE(caller != zc::none);
  ZC_IF_SOME(function, caller) {
    ZC_REQUIRE(function.blocks.size() == 2);
    const auto& terminator = function.blocks[0].terminator;
    ZC_REQUIRE(terminator.kind() == mir::MirTerminatorKind::Call);
    const auto& mirCall = terminator.callValue();
    ZC_REQUIRE(mirCall.arguments.size() == 1);
    ZC_EXPECT(mirCall.arguments[0].kind() == mir::MirOperandKind::Constant);
    ZC_EXPECT(mirCall.arguments[0].constantValue().type == call.arguments[0].type);
    ZC_EXPECT(mirCall.arguments[0].constantValue().value.tag() ==
              checker::signature::CanonicalConstValueTag::Integer);
  }
}

ZC_TEST("HIR pipeline retains a direct-call local initializer") {
  HirPipelineFixture fixture(
      "fun helper() -> i32 { return 0; }\n"
      "fun entry() -> i32 { let value = helper(); return value; }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 2);
  ZC_REQUIRE(module.expressions().size() == 1);
  ZC_REQUIRE(module.calls().size() == 1);
  ZC_REQUIRE(module.locals().size() == 1);
  ZC_REQUIRE(module.localReferences().size() == 1);
  const auto& helper = module.functions()[0];
  const auto& entry = module.functions()[1];
  const auto& local = module.locals()[0];
  const auto& reference = module.localReferences()[0];
  const auto& call = module.calls()[0];
  ZC_EXPECT(module.expressions()[0].node == module.returns()[0].value);
  ZC_EXPECT(call.node == local.initializer);
  ZC_EXPECT(call.node != module.returns()[1].value);
  ZC_EXPECT(call.callee == helper.definition);
  ZC_EXPECT(call.resultType == entry.resultType);
  ZC_EXPECT(local.local == reference.local);
  ZC_EXPECT(reference.node == module.returns()[1].value);
  ZC_EXPECT(reference.category == HirValueCategory::Place);
}

ZC_TEST("HIR pipeline preserves a returned function local without binder identity") {
  HirPipelineFixture fixture("fun entry() -> i32 { let value = 0; return value; }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 1);
  ZC_REQUIRE(module.blocks().size() == 1);
  ZC_REQUIRE(module.returns().size() == 1);
  ZC_REQUIRE(module.expressions().size() == 1);
  ZC_REQUIRE(module.locals().size() == 1);
  ZC_REQUIRE(module.localReferences().size() == 1);
  const auto& function = module.functions()[0];
  const auto& block = module.blocks()[0];
  const auto& returned = module.returns()[0];
  const auto& local = module.locals()[0];
  const auto& initializer = module.expressions()[0];
  const auto& reference = module.localReferences()[0];
  ZC_EXPECT(function.node.ordinal() == 1);
  ZC_EXPECT(block.node.ordinal() == 2);
  ZC_EXPECT(local.node.ordinal() == 3);
  ZC_EXPECT(initializer.node.ordinal() == 4);
  ZC_EXPECT(returned.node.ordinal() == 5);
  ZC_EXPECT(reference.node.ordinal() == 6);
  ZC_EXPECT(block.statements.size() == 2);
  ZC_EXPECT(block.statements[0] == local.node);
  ZC_EXPECT(block.statements[1] == returned.node);
  ZC_EXPECT(local.initializer == initializer.node);
  ZC_EXPECT(returned.value == reference.node);
  ZC_EXPECT(local.local == reference.local);
  ZC_EXPECT(local.type == function.resultType);
  ZC_EXPECT(reference.type == local.type);
  ZC_EXPECT(reference.category == HirValueCategory::Place);
  auto dump = module.dump();
  ZC_REQUIRE(dump != zc::none);
  ZC_IF_SOME(text, dump) {
    ZC_EXPECT(text.contains("local h3 l1"_zc));
    ZC_EXPECT(text.contains("local-ref h6 l1"_zc));
    ZC_EXPECT(!text.contains("OwnerLocalBinding"_zc));
  }
}

ZC_TEST("HIR pipeline lowers a local nominal aggregate field projection") {
  HirPipelineFixture fixture(
      "struct Cell { value: i32, }\n"
      "fun entry() -> i32 { let cell = Cell { value: 0 }; return cell.value; }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 1);
  ZC_REQUIRE(module.locals().size() == 1);
  ZC_REQUIRE(module.aggregates().size() == 1);
  ZC_REQUIRE(module.localFieldProjections().size() == 1);
  ZC_REQUIRE(module.returns().size() == 1);
  const auto& function = module.functions()[0];
  const auto& local = module.locals()[0];
  const auto& aggregate = module.aggregates()[0];
  const auto& projection = module.localFieldProjections()[0];
  const auto& returned = module.returns()[0];
  ZC_EXPECT(local.initializer == aggregate.node);
  ZC_EXPECT(projection.local == local.local);
  ZC_EXPECT(projection.receiverType == local.type);
  ZC_EXPECT(projection.type == function.resultType);
  ZC_EXPECT(projection.category == HirValueCategory::Place);
  ZC_EXPECT(returned.value == projection.node);
  ZC_EXPECT(aggregate.category == HirValueCategory::Value);
  ZC_REQUIRE(aggregate.elements.size() == 1);
  ZC_EXPECT(aggregate.elements[0].type == function.resultType);
  ZC_EXPECT(aggregate.elements[0].value.tag() ==
            checker::signature::CanonicalConstValueTag::Integer);

  const auto ownershipInputs = fixture.compilerSession().getVerifiedOwnershipInputs();
  ZC_REQUIRE(ownershipInputs.size() == 1);
  const auto& inputs = ownershipInputs[0];
  const auto& movePaths = inputs.movePaths();
  ZC_REQUIRE(movePaths.functions().size() == 1);
  const auto& paths = movePaths.functions()[0].facts;
  ZC_REQUIRE(paths.size() == 2);
  zc::Maybe<const ownership::facts::MovePathFact&> root;
  zc::Maybe<const ownership::facts::MovePathFact&> field;
  for (const auto& path : paths) {
    if (path.key.place.projections().size() == 0) root = path;
    if (path.key.place.projections().size() == 1) field = path;
  }
  ZC_REQUIRE(root != zc::none);
  ZC_REQUIRE(field != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(field).key.place.projections()[0].kind() ==
            mir::MirProjectionKind::Field);
  ZC_REQUIRE(ZC_REQUIRE_NONNULL(field).parent != zc::none);
  ZC_IF_SOME(parent, ZC_REQUIRE_NONNULL(field).parent) {
    ZC_EXPECT(parent.place.local() == ZC_REQUIRE_NONNULL(root).key.place.local());
    ZC_EXPECT(parent.place.projections().size() == 0);
  }
  ZC_EXPECT(movePaths.conflicts(ZC_REQUIRE_NONNULL(root).key, ZC_REQUIRE_NONNULL(root).key));
  ZC_EXPECT(movePaths.conflicts(ZC_REQUIRE_NONNULL(root).key, ZC_REQUIRE_NONNULL(field).key));
  ZC_EXPECT(movePaths.conflicts(ZC_REQUIRE_NONNULL(field).key, ZC_REQUIRE_NONNULL(root).key));

  const auto& initialization = inputs.initialization();
  ZC_REQUIRE(initialization.functions().size() == 1);
  const auto& initializationFacts = initialization.functions()[0].facts;
  ZC_REQUIRE(initializationFacts.size() == 14);
  size_t rootStates = 0;
  size_t fieldStates = 0;
  for (const auto& fact : initializationFacts) {
    if (fact.key.place.projections().size() == 0) ++rootStates;
    if (fact.key.place.projections().size() == 1) ++fieldStates;
  }
  ZC_EXPECT(rootStates == 7);
  ZC_EXPECT(fieldStates == 7);
}

ZC_TEST("HIR pipeline lowers a mutable local receiver call") {
  HirPipelineFixture fixture(
      "struct Cell { value: i32, mutating fun read(this, amount: i32) -> i32; }\n"
      "fun entry() -> i32 { mut cell = Cell { value: 0 }; return cell.read(1); }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 1);
  ZC_REQUIRE(module.locals().size() == 1);
  ZC_REQUIRE(module.aggregates().size() == 1);
  ZC_REQUIRE(module.localReferences().size() == 1);
  ZC_REQUIRE(module.receiverCalls().size() == 1);
  ZC_REQUIRE(module.returns().size() == 1);

  const auto& local = module.locals()[0];
  const auto& receiver = module.localReferences()[0];
  const auto& call = module.receiverCalls()[0];
  ZC_EXPECT(receiver.local == local.local);
  ZC_EXPECT(receiver.type == local.type);
  ZC_EXPECT(call.receiver == receiver.node);
  ZC_EXPECT(call.receiverSourceType == local.type);
  ZC_EXPECT(call.receiverMode == checker::checked::ReceiverMode::Mutable);
  ZC_REQUIRE(call.receiverAdjustments.size() == 1);
  ZC_EXPECT(call.receiverAdjustments[0] == checker::checked::ReceiverAdjustmentStep::BorrowMutable);
  ZC_REQUIRE(call.arguments.size() == 1);
}

ZC_TEST("HIR pipeline is deterministic across equivalent semantic contexts") {
  constexpr zc::StringPtr source = "let first = 0;\nconst second = 1;"_zc;
  HirPipelineFixture firstFixture(source);
  HirPipelineFixture secondFixture(source);
  const auto& first = firstFixture.hirModule();
  const auto& second = secondFixture.hirModule();
  ZC_REQUIRE(first.declarations().size() == 2);
  ZC_REQUIRE(second.declarations().size() == 2);
  ZC_EXPECT(first.declarations()[0].node.ordinal() == 1);
  ZC_EXPECT(first.declarations()[1].node.ordinal() == 4);
  auto firstDump = first.dump();
  auto secondDump = second.dump();
  ZC_REQUIRE(firstDump != zc::none);
  ZC_REQUIRE(secondDump != zc::none);
  ZC_IF_SOME(firstText, firstDump) {
    ZC_IF_SOME(secondText, secondDump) { ZC_EXPECT(firstText == secondText); }
  }
}

ZC_TEST("Checked-module assembly ignores interfaces outside the exact imported view") {
  HirPipelineFixture fixture(""_zc);
  auto authority = fixture.compilerSession().materializeCheckerIdentityAuthority();
  ZC_REQUIRE(authority != zc::none);
  const auto& bound = driver::core_library_test::soleUserBoundModule(ZC_REQUIRE_NONNULL(authority));
  const auto& checkerBound = checkerBoundModule(ZC_REQUIRE_NONNULL(authority), bound.module());
  ZC_REQUIRE(fixture.hirModule().visibleImportedInterfaces().size() == 1);
  ZC_IF_SOME(prelude, checkerBound.preludeSurface()) {
    ZC_EXPECT(fixture.hirModule().visibleImportedInterfaces()[0].module == prelude.module);
  }
}

}  // namespace
}  // namespace zomlang::compiler::hir
