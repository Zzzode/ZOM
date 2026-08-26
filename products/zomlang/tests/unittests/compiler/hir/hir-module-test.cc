// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/hir/hir-module.h"

#include "zc/core/encoding.h"
#include "zc/core/time.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/checker/checker-identity-authority.h"
#include "zomlang/compiler/diagnostics/core/diagnostic-engine.h"
#include "zomlang/compiler/driver/package/manifest-parser.h"
#include "zomlang/compiler/driver/package/source-record.h"
#include "zomlang/compiler/driver/session/compiler-session.h"
#include "zomlang/compiler/hir/checked-module.h"
#include "zomlang/compiler/identity/crypto/sha256.h"
#include "zomlang/compiler/ownership/surface-admission.h"
#include "zomlang/tests/unittests/compiler/driver/core/core-library-test-fixture.h"

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
  ZC_REQUIRE(call.arguments[0].value != zc::none);
  ZC_EXPECT(call.arguments[0].parameter == zc::none);
  ZC_IF_SOME(value, call.arguments[0].value) {
    ZC_EXPECT(value.tag() == checker::signature::CanonicalConstValueTag::Integer);
  }

  const auto builtMir = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(builtMir.size() == 1);
  zc::Maybe<const mir::MirFunction&> caller;
  for (const auto& function : builtMir[0].builtMir().functions()) {
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

ZC_TEST("HIR pipeline lowers a direct call parameter argument to a place operand") {
  HirPipelineFixture fixture(
      "fun helper(value: i32) -> i32 { return value; }\n"
      "fun entry(input: i32) -> i32 { return helper(input); }"_zc);
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
  // A parameter-reference argument carries a parameter key and no constant.
  ZC_EXPECT(call.arguments[0].value == zc::none);
  ZC_REQUIRE(call.arguments[0].parameter != zc::none);
  ZC_REQUIRE(entry.parameters.size() == 1);
  ZC_IF_SOME(key, call.arguments[0].parameter) { ZC_EXPECT(key == entry.parameters[0].key); }

  const auto builtMir = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(builtMir.size() == 1);
  zc::Maybe<const mir::MirFunction&> caller;
  for (const auto& function : builtMir[0].builtMir().functions()) {
    if (function.owner == entry.definition) caller = function;
  }
  ZC_REQUIRE(caller != zc::none);
  ZC_IF_SOME(function, caller) {
    ZC_REQUIRE(function.blocks.size() == 2);
    const auto& terminator = function.blocks[0].terminator;
    ZC_REQUIRE(terminator.kind() == mir::MirTerminatorKind::Call);
    const auto& mirCall = terminator.callValue();
    ZC_REQUIRE(mirCall.arguments.size() == 1);
    // The parameter argument lowers to a place-use (copy) of the leading
    // parameter local, not a constant operand.
    ZC_EXPECT(mirCall.arguments[0].kind() == mir::MirOperandKind::Copy);
    ZC_EXPECT(mirCall.arguments[0].place().local() == function.locals[0].id);
    ZC_EXPECT(mirCall.arguments[0].place().resultType() == call.arguments[0].type);
  }
}

ZC_TEST("HIR fingerprint discriminates direct call argument values") {
  HirPipelineFixture sevenFixture(
      "fun helper(value: i32) -> i32 { return value; }\n"
      "fun entry() -> i32 { return helper(7); }"_zc);
  HirPipelineFixture eightFixture(
      "fun helper(value: i32) -> i32 { return value; }\n"
      "fun entry() -> i32 { return helper(8); }"_zc);
  auto sevenDump = sevenFixture.hirModule().dump();
  auto eightDump = eightFixture.hirModule().dump();
  ZC_REQUIRE(sevenDump != zc::none);
  ZC_REQUIRE(eightDump != zc::none);
  // The two modules differ only in the call argument constant; the fingerprint
  // now encodes call arguments, so their canonical text must differ.
  ZC_IF_SOME(sevenText, sevenDump) {
    ZC_IF_SOME(eightText, eightDump) { ZC_EXPECT(sevenText != eightText); }
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

ZC_TEST("HIR pipeline lowers a three-binding sequential local body") {
  HirPipelineFixture fixture(
      "fun entry(a: i32) -> i32 { let x: i32 = a; let y: i32 = x; let z: i32 = 5; return z; }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 1);
  ZC_REQUIRE(module.blocks().size() == 1);
  ZC_REQUIRE(module.returns().size() == 1);
  // Three bindings: x copies parameter a (one parameter reference), y copies
  // local x (one local reference), z is a literal (one scalar expression). The
  // return of z adds a second local reference.
  ZC_REQUIRE(module.locals().size() == 3);
  ZC_REQUIRE(module.expressions().size() == 1);
  ZC_REQUIRE(module.parameterReferences().size() == 1);
  ZC_REQUIRE(module.localReferences().size() == 2);
  const auto& function = module.functions()[0];
  const auto& block = module.blocks()[0];
  const auto& returned = module.returns()[0];
  // Fixed-id layout: F+0 function, F+1 block, then binding i at F+2+2i local and
  // F+3+2i initializer, then return at F+2+2N and its value at F+3+2N (N = 3).
  ZC_EXPECT(function.node.ordinal() == 1);
  ZC_EXPECT(block.node.ordinal() == 2);
  ZC_EXPECT(block.statements.size() == 4);
  const auto& xLocal = module.locals()[0];
  const auto& yLocal = module.locals()[1];
  const auto& zLocal = module.locals()[2];
  ZC_EXPECT(xLocal.node.ordinal() == 3);
  ZC_EXPECT(yLocal.node.ordinal() == 5);
  ZC_EXPECT(zLocal.node.ordinal() == 7);
  ZC_EXPECT(xLocal.local.ordinal() == 1);
  ZC_EXPECT(yLocal.local.ordinal() == 2);
  ZC_EXPECT(zLocal.local.ordinal() == 3);
  ZC_EXPECT(block.statements[0] == xLocal.node);
  ZC_EXPECT(block.statements[1] == yLocal.node);
  ZC_EXPECT(block.statements[2] == zLocal.node);
  ZC_EXPECT(block.statements[3] == returned.node);
  ZC_EXPECT(returned.node.ordinal() == 9);
  ZC_EXPECT(returned.value.ordinal() == 10);
  // x's initializer is a parameter reference at node 4.
  const auto& parameterReference = module.parameterReferences()[0];
  ZC_EXPECT(parameterReference.node == xLocal.initializer);
  ZC_EXPECT(parameterReference.node.ordinal() == 4);
  ZC_EXPECT(parameterReference.category == HirValueCategory::Place);
  // y's initializer is a local reference to x (node 6); the return value is a
  // local reference to z (node 10).
  const auto& yInitializer = module.localReferences()[0];
  const auto& returnReference = module.localReferences()[1];
  ZC_EXPECT(yInitializer.node == yLocal.initializer);
  ZC_EXPECT(yInitializer.node.ordinal() == 6);
  ZC_EXPECT(yInitializer.local == xLocal.local);
  ZC_EXPECT(returnReference.node == returned.value);
  ZC_EXPECT(returnReference.node.ordinal() == 10);
  ZC_EXPECT(returnReference.local == zLocal.local);
  // z's initializer is the scalar literal at node 8.
  const auto& literal = module.expressions()[0];
  ZC_EXPECT(literal.node == zLocal.initializer);
  ZC_EXPECT(literal.node.ordinal() == 8);
}

ZC_TEST("HIR pipeline lowers a binary-initializer sequential local body") {
  HirPipelineFixture fixture(
      "fun f(a: i32, b: i32) -> i32 { let x: i32 = a + b; let y: i32 = x * b; return y; }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 1);
  ZC_REQUIRE(module.blocks().size() == 1);
  ZC_REQUIRE(module.returns().size() == 1);
  // Two binding locals, each with a primitive-binary initializer. The binary
  // operations are x = a + b (two parameter operands) and y = x * b (an
  // earlier-local operand plus a parameter operand); the return of y is a local
  // reference.
  ZC_REQUIRE(module.locals().size() == 2);
  ZC_REQUIRE(module.primitiveBinaryOperations().size() == 2);
  // Operand references: a, b (for x), b (for y) => three parameter references;
  // x (for y's left operand) and the return of y => two local references.
  ZC_REQUIRE(module.parameterReferences().size() == 3);
  ZC_REQUIRE(module.localReferences().size() == 2);
  ZC_REQUIRE(module.expressions().size() == 0);
  const auto& function = module.functions()[0];
  const auto& block = module.blocks()[0];
  const auto& returned = module.returns()[0];
  // Variable-width fixed-id layout (each binary binding is width 4): F+0
  // function, F+1 block; binding0 x: local=3, init=4, leftOp=5, rightOp=6;
  // binding1 y: local=7, init=8, leftOp=9, rightOp=10; return=11, value=12.
  ZC_EXPECT(function.node.ordinal() == 1);
  ZC_EXPECT(block.node.ordinal() == 2);
  ZC_REQUIRE(block.statements.size() == 3);
  const auto& xLocal = module.locals()[0];
  const auto& yLocal = module.locals()[1];
  ZC_EXPECT(xLocal.node.ordinal() == 3);
  ZC_EXPECT(yLocal.node.ordinal() == 7);
  ZC_EXPECT(xLocal.local.ordinal() == 1);
  ZC_EXPECT(yLocal.local.ordinal() == 2);
  ZC_EXPECT(block.statements[0] == xLocal.node);
  ZC_EXPECT(block.statements[1] == yLocal.node);
  ZC_EXPECT(block.statements[2] == returned.node);
  ZC_EXPECT(returned.node.ordinal() == 11);
  ZC_EXPECT(returned.value.ordinal() == 12);
  // Each binary op node coincides with its binding initializer node and its
  // result type is the operand type (i32, never bool).
  zc::Maybe<const HirPrimitiveBinaryExpression&> add;
  zc::Maybe<const HirPrimitiveBinaryExpression&> mul;
  for (const auto& operation : module.primitiveBinaryOperations()) {
    if (operation.operation == checker::PrimitiveOperation::Add) add = operation;
    if (operation.operation == checker::PrimitiveOperation::Mul) mul = operation;
  }
  ZC_REQUIRE(add != zc::none);
  ZC_REQUIRE(mul != zc::none);
  ZC_IF_SOME(addValue, add) {
    ZC_EXPECT(addValue.node == xLocal.initializer);
    ZC_EXPECT(addValue.type == function.resultType);
    ZC_EXPECT(addValue.type == addValue.operandType);
    ZC_EXPECT(addValue.left.ordinal() == 5);
    ZC_EXPECT(addValue.right.ordinal() == 6);
  }
  ZC_IF_SOME(mulValue, mul) {
    ZC_EXPECT(mulValue.node == yLocal.initializer);
    ZC_EXPECT(mulValue.type == mulValue.operandType);
    ZC_EXPECT(mulValue.left.ordinal() == 9);
    ZC_EXPECT(mulValue.right.ordinal() == 10);
  }
  // y's left operand (node 9) is a local reference to x; the return value (node
  // 12) is a local reference to y.
  zc::Maybe<const HirLocalReferenceExpression&> mulLeft;
  zc::Maybe<const HirLocalReferenceExpression&> returnReference;
  for (const auto& reference : module.localReferences()) {
    if (reference.node.ordinal() == 9) mulLeft = reference;
    if (reference.node == returned.value) returnReference = reference;
  }
  ZC_REQUIRE(mulLeft != zc::none);
  ZC_REQUIRE(returnReference != zc::none);
  ZC_IF_SOME(reference, mulLeft) { ZC_EXPECT(reference.local == xLocal.local); }
  ZC_IF_SOME(reference, returnReference) { ZC_EXPECT(reference.local == yLocal.local); }

  // The body lowers to a single block: for each binding StorageLive + Assign of
  // an Arithmetic rvalue, then Return of y.
  const auto builtMir = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(builtMir.size() == 1);
  zc::Maybe<const mir::MirFunction&> lowered;
  for (const auto& mirFunction : builtMir[0].builtMir().functions()) {
    if (mirFunction.owner == function.definition) lowered = mirFunction;
  }
  ZC_REQUIRE(lowered != zc::none);
  ZC_IF_SOME(mirFunction, lowered) {
    // Two parameters (locals 0,1) plus two user locals (locals 2,3).
    ZC_REQUIRE(mirFunction.locals.size() == 4);
    const auto parameterA = mirFunction.locals[0].id;
    const auto parameterB = mirFunction.locals[1].id;
    const auto localX = mirFunction.locals[2].id;
    const auto localY = mirFunction.locals[3].id;
    ZC_REQUIRE(mirFunction.blocks.size() == 1);
    const auto& mirBlock = mirFunction.blocks[0];
    // StorageLive + Assign per binding => four statements.
    ZC_REQUIRE(mirBlock.statements.size() == 4);
    ZC_EXPECT(mirBlock.statements[0].kind() == mir::MirStatementKind::StorageLive);
    ZC_EXPECT(mirBlock.statements[1].kind() == mir::MirStatementKind::Assign);
    ZC_EXPECT(mirBlock.statements[2].kind() == mir::MirStatementKind::StorageLive);
    ZC_EXPECT(mirBlock.statements[3].kind() == mir::MirStatementKind::Assign);
    const auto& xAssign = mirBlock.statements[1].assignmentValue();
    const auto& yAssign = mirBlock.statements[3].assignmentValue();
    // x = a + b: Arithmetic Add of the two parameter locals into user local x.
    ZC_REQUIRE(xAssign.value.kind() == mir::MirRvalueKind::Arithmetic);
    ZC_EXPECT(xAssign.value.arithmeticValue().op == mir::MirArithmeticOperator::Add);
    ZC_EXPECT(xAssign.destination.local() == localX);
    ZC_EXPECT(xAssign.value.arithmeticValue().left.place().local() == parameterA);
    ZC_EXPECT(xAssign.value.arithmeticValue().right.place().local() == parameterB);
    // y = x * b: Arithmetic Mul of user local x and parameter b into y.
    ZC_REQUIRE(yAssign.value.kind() == mir::MirRvalueKind::Arithmetic);
    ZC_EXPECT(yAssign.value.arithmeticValue().op == mir::MirArithmeticOperator::Mul);
    ZC_EXPECT(yAssign.destination.local() == localY);
    ZC_EXPECT(yAssign.value.arithmeticValue().left.place().local() == localX);
    ZC_EXPECT(yAssign.value.arithmeticValue().right.place().local() == parameterB);
    ZC_REQUIRE(mirBlock.terminator.kind() == mir::MirTerminatorKind::Return);
    ZC_IF_SOME(returnValue, mirBlock.terminator.returnValue().value) {
      ZC_EXPECT(returnValue.place().local() == localY);
    }
  }
}

ZC_TEST("HIR pipeline lowers a nested-operand binary sequential local body") {
  HirPipelineFixture fixture(
      "fun f(a: i32, b: i32, c: i32) -> i32 { let z: i32 = a + b * c; let w: i32 = z; return w; }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 1);
  ZC_REQUIRE(module.locals().size() == 2);
  // Two primitive binary operations: the outer `a + (b*c)` and the nested `b*c`.
  ZC_REQUIRE(module.primitiveBinaryOperations().size() == 2);
  const auto& function = module.functions()[0];
  const auto& block = module.blocks()[0];
  const auto& returned = module.returns()[0];
  // Variable-width fixed-id layout: F+0 function, F+1 block; binding0 z is a
  // binary with one nested operand => width 6: local=3, init=4, leftOp=5,
  // rightOp=6, nestedLeafLeft=7, nestedLeafRight=8; binding1 w is width 2:
  // local=9, init=10; return=11, value=12.
  ZC_EXPECT(function.node.ordinal() == 1);
  ZC_EXPECT(block.node.ordinal() == 2);
  ZC_REQUIRE(block.statements.size() == 3);
  const auto& zLocal = module.locals()[0];
  const auto& wLocal = module.locals()[1];
  ZC_EXPECT(zLocal.node.ordinal() == 3);
  ZC_EXPECT(wLocal.node.ordinal() == 9);
  ZC_EXPECT(returned.node.ordinal() == 11);
  ZC_EXPECT(returned.value.ordinal() == 12);
  // The outer Add's initializer node is z's init (node 4); its right operand is
  // the nested Mul node (node 6), whose leaves are nodes 7 and 8.
  zc::Maybe<const HirPrimitiveBinaryExpression&> add;
  zc::Maybe<const HirPrimitiveBinaryExpression&> mul;
  for (const auto& operation : module.primitiveBinaryOperations()) {
    if (operation.operation == checker::PrimitiveOperation::Add) add = operation;
    if (operation.operation == checker::PrimitiveOperation::Mul) mul = operation;
  }
  ZC_REQUIRE(add != zc::none);
  ZC_REQUIRE(mul != zc::none);
  ZC_IF_SOME(addValue, add) {
    ZC_EXPECT(addValue.node == zLocal.initializer);
    ZC_EXPECT(addValue.type == addValue.operandType);
    ZC_EXPECT(addValue.left.ordinal() == 5);
    ZC_EXPECT(addValue.right.ordinal() == 6);
  }
  ZC_IF_SOME(mulValue, mul) {
    ZC_EXPECT(mulValue.node.ordinal() == 6);
    ZC_EXPECT(mulValue.type == mulValue.operandType);
    ZC_EXPECT(mulValue.left.ordinal() == 7);
    ZC_EXPECT(mulValue.right.ordinal() == 8);
  }

  // The nested operand lowers to a synthesized Temporary local holding b*c, then
  // the outer local z = a + temp; a copy of z for w; and return w.
  const auto builtMir = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(builtMir.size() == 1);
  zc::Maybe<const mir::MirFunction&> lowered;
  for (const auto& mirFunction : builtMir[0].builtMir().functions()) {
    if (mirFunction.owner == function.definition) lowered = mirFunction;
  }
  ZC_REQUIRE(lowered != zc::none);
  ZC_IF_SOME(mirFunction, lowered) {
    // Three parameters (locals 1..3), two user locals (z=4, w=5), one temp (6).
    ZC_REQUIRE(mirFunction.locals.size() == 6);
    const auto parameterA = mirFunction.locals[0].id;
    const auto parameterB = mirFunction.locals[1].id;
    const auto parameterC = mirFunction.locals[2].id;
    const auto localZ = mirFunction.locals[3].id;
    const auto localW = mirFunction.locals[4].id;
    const auto temp = mirFunction.locals[5].id;
    ZC_EXPECT(mirFunction.locals[5].kind == mir::MirLocalKind::Temporary);
    ZC_REQUIRE(mirFunction.blocks.size() == 1);
    const auto& mirBlock = mirFunction.blocks[0];
    // z binding emits StorageLive(temp), Assign(temp = b*c), StorageLive(z),
    // Assign(z = a + temp); then w binding StorageLive(w) + Assign(w = z).
    ZC_REQUIRE(mirBlock.statements.size() == 6);
    ZC_EXPECT(mirBlock.statements[0].kind() == mir::MirStatementKind::StorageLive);
    ZC_EXPECT(mirBlock.statements[0].storageLocal() == temp);
    ZC_EXPECT(mirBlock.statements[1].kind() == mir::MirStatementKind::Assign);
    ZC_EXPECT(mirBlock.statements[2].kind() == mir::MirStatementKind::StorageLive);
    ZC_EXPECT(mirBlock.statements[2].storageLocal() == localZ);
    ZC_EXPECT(mirBlock.statements[3].kind() == mir::MirStatementKind::Assign);
    ZC_EXPECT(mirBlock.statements[4].kind() == mir::MirStatementKind::StorageLive);
    ZC_EXPECT(mirBlock.statements[4].storageLocal() == localW);
    ZC_EXPECT(mirBlock.statements[5].kind() == mir::MirStatementKind::Assign);
    // temp = b * c: Arithmetic Mul of parameters b and c into the temp.
    const auto& tempAssign = mirBlock.statements[1].assignmentValue();
    ZC_REQUIRE(tempAssign.value.kind() == mir::MirRvalueKind::Arithmetic);
    ZC_EXPECT(tempAssign.value.arithmeticValue().op == mir::MirArithmeticOperator::Mul);
    ZC_EXPECT(tempAssign.destination.local() == temp);
    ZC_EXPECT(tempAssign.value.arithmeticValue().left.place().local() == parameterB);
    ZC_EXPECT(tempAssign.value.arithmeticValue().right.place().local() == parameterC);
    // z = a + temp: Arithmetic Add of parameter a and the temp into z.
    const auto& zAssign = mirBlock.statements[3].assignmentValue();
    ZC_REQUIRE(zAssign.value.kind() == mir::MirRvalueKind::Arithmetic);
    ZC_EXPECT(zAssign.value.arithmeticValue().op == mir::MirArithmeticOperator::Add);
    ZC_EXPECT(zAssign.destination.local() == localZ);
    ZC_EXPECT(zAssign.value.arithmeticValue().left.place().local() == parameterA);
    ZC_EXPECT(zAssign.value.arithmeticValue().right.place().local() == temp);
    ZC_REQUIRE(mirBlock.terminator.kind() == mir::MirTerminatorKind::Return);
    ZC_IF_SOME(returnValue, mirBlock.terminator.returnValue().value) {
      ZC_EXPECT(returnValue.place().local() == localW);
    }
  }
}

ZC_TEST("HIR pipeline lowers a parameter-reference mutable local write") {
  HirPipelineFixture fixture("fun f(a: i32) -> i32 { mut x: i32 = 0; x = a; return x; }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 1);
  ZC_REQUIRE(module.blocks().size() == 1);
  ZC_REQUIRE(module.returns().size() == 1);
  ZC_REQUIRE(module.locals().size() == 1);
  ZC_REQUIRE(module.localWrites().size() == 1);
  // The initializer `0` is one scalar literal; the write value `a` is a
  // parameter reference (not a literal); the return of x is one local reference.
  ZC_REQUIRE(module.expressions().size() == 1);
  ZC_REQUIRE(module.parameterReferences().size() == 1);
  ZC_REQUIRE(module.localReferences().size() == 1);
  const auto& function = module.functions()[0];
  const auto& block = module.blocks()[0];
  const auto& local = module.locals()[0];
  const auto& write = module.localWrites()[0];
  const auto& initializer = module.expressions()[0];
  const auto& writeParameter = module.parameterReferences()[0];
  const auto& returnReference = module.localReferences()[0];
  // Fixed-id layout: F+0 function, F+1 block, F+2 local, F+3 initializer,
  // F+4 write, F+5 write value, F+6 return, F+7 return value.
  ZC_EXPECT(function.node.ordinal() == 1);
  ZC_EXPECT(block.node.ordinal() == 2);
  ZC_EXPECT(local.node.ordinal() == 3);
  ZC_EXPECT(initializer.node.ordinal() == 4);
  ZC_EXPECT(write.node.ordinal() == 5);
  ZC_EXPECT(writeParameter.node.ordinal() == 6);
  ZC_EXPECT(block.statements.size() == 3);
  ZC_EXPECT(block.statements[0] == local.node);
  ZC_EXPECT(block.statements[1] == write.node);
  ZC_EXPECT(local.initializer == initializer.node);
  // The write value node id is the parameter reference, not a literal.
  ZC_EXPECT(write.value == writeParameter.node);
  ZC_EXPECT(write.kind == HirLocalWriteKind::Overwrite);
  ZC_EXPECT(write.local == local.local);
  ZC_EXPECT(writeParameter.type == local.type);
  ZC_EXPECT(writeParameter.category == HirValueCategory::Place);
  ZC_EXPECT(returnReference.local == local.local);

  // Built MIR: the parameter is localId(1), the user local localId(2); the write
  // lowers to a place-use of the parameter local into the user local.
  const auto builtMir = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(builtMir.size() == 1);
  zc::Maybe<const mir::MirFunction&> lowered;
  for (const auto& mirFunction : builtMir[0].builtMir().functions()) {
    if (mirFunction.owner == function.definition) lowered = mirFunction;
  }
  ZC_REQUIRE(lowered != zc::none);
  ZC_IF_SOME(mirFunction, lowered) {
    ZC_REQUIRE(mirFunction.locals.size() == 2);
    ZC_EXPECT(mirFunction.locals[0].kind == mir::MirLocalKind::Parameter);
    ZC_EXPECT(mirFunction.locals[1].kind == mir::MirLocalKind::UserLocal);
    const auto parameter = mirFunction.locals[0].id;
    const auto userLocal = mirFunction.locals[1].id;
    ZC_REQUIRE(mirFunction.blocks.size() == 1);
    const auto& mirBlock = mirFunction.blocks[0];
    // StorageLive(x), Assign(x = 0, Initialize), Assign(x = param, Overwrite).
    ZC_REQUIRE(mirBlock.statements.size() == 3);
    ZC_EXPECT(mirBlock.statements[0].kind() == mir::MirStatementKind::StorageLive);
    ZC_EXPECT(mirBlock.statements[0].storageLocal() == userLocal);
    const auto& initAssign = mirBlock.statements[1].assignmentValue();
    ZC_EXPECT(initAssign.initialization == mir::MirInitializationKind::Initialize);
    ZC_EXPECT(initAssign.destination.local() == userLocal);
    ZC_EXPECT(initAssign.value.kind() == mir::MirRvalueKind::Use);
    ZC_EXPECT(initAssign.value.useValue().operand.kind() == mir::MirOperandKind::Constant);
    const auto& overwriteAssign = mirBlock.statements[2].assignmentValue();
    ZC_EXPECT(overwriteAssign.initialization == mir::MirInitializationKind::Overwrite);
    ZC_EXPECT(overwriteAssign.destination.local() == userLocal);
    ZC_EXPECT(overwriteAssign.value.kind() == mir::MirRvalueKind::Use);
    // The overwrite value is a place-use of the parameter local (no projections).
    ZC_EXPECT(overwriteAssign.value.useValue().operand.kind() != mir::MirOperandKind::Constant);
    ZC_EXPECT(overwriteAssign.value.useValue().operand.place().local() == parameter);
    ZC_EXPECT(overwriteAssign.value.useValue().operand.place().projections().size() == 0);
    ZC_REQUIRE(mirBlock.terminator.kind() == mir::MirTerminatorKind::Return);
    ZC_IF_SOME(returnValue, mirBlock.terminator.returnValue().value) {
      ZC_EXPECT(returnValue.place().local() == userLocal);
    }
  }
}

ZC_TEST("HIR pipeline lowers a scalar-literal mutable local write unchanged") {
  // Regression guard: an all-scalar-literal mut-local write keeps its exact
  // node-id layout and MIR shape after the reference-write value discriminator
  // was introduced. This body has no parameter and both write values are
  // literals, so no parameter reference is materialized.
  HirPipelineFixture fixture("fun g() -> i32 { mut x: i32 = 0; x = 1; return x; }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 1);
  ZC_REQUIRE(module.locals().size() == 1);
  ZC_REQUIRE(module.localWrites().size() == 1);
  // Two literals: the initializer `0` and the write value `1`. No parameter ref.
  ZC_REQUIRE(module.expressions().size() == 2);
  ZC_REQUIRE(module.parameterReferences().size() == 0);
  ZC_REQUIRE(module.localReferences().size() == 1);
  const auto& function = module.functions()[0];
  const auto& block = module.blocks()[0];
  const auto& local = module.locals()[0];
  const auto& write = module.localWrites()[0];
  ZC_EXPECT(function.node.ordinal() == 1);
  ZC_EXPECT(block.node.ordinal() == 2);
  ZC_EXPECT(local.node.ordinal() == 3);
  ZC_EXPECT(write.node.ordinal() == 5);
  ZC_EXPECT(write.value.ordinal() == 6);
  ZC_EXPECT(write.kind == HirLocalWriteKind::Overwrite);
  // The write value node is a scalar literal expression (unchanged behavior).
  bool writeValueIsLiteral = false;
  for (const auto& expression : module.expressions()) {
    if (expression.node == write.value) writeValueIsLiteral = true;
  }
  ZC_EXPECT(writeValueIsLiteral);

  const auto builtMir = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(builtMir.size() == 1);
  zc::Maybe<const mir::MirFunction&> lowered;
  for (const auto& mirFunction : builtMir[0].builtMir().functions()) {
    if (mirFunction.owner == function.definition) lowered = mirFunction;
  }
  ZC_REQUIRE(lowered != zc::none);
  ZC_IF_SOME(mirFunction, lowered) {
    // No parameter: the single user local is localId(1), exactly as before.
    ZC_REQUIRE(mirFunction.locals.size() == 1);
    ZC_EXPECT(mirFunction.locals[0].kind == mir::MirLocalKind::UserLocal);
    ZC_REQUIRE(mirFunction.blocks.size() == 1);
    const auto& mirBlock = mirFunction.blocks[0];
    ZC_REQUIRE(mirBlock.statements.size() == 3);
    const auto& overwriteAssign = mirBlock.statements[2].assignmentValue();
    ZC_EXPECT(overwriteAssign.initialization == mir::MirInitializationKind::Overwrite);
    // The literal write value stays a constant operand (unchanged behavior).
    ZC_EXPECT(overwriteAssign.value.useValue().operand.kind() == mir::MirOperandKind::Constant);
  }
}

ZC_TEST("HIR pipeline lowers a binary mutable local write") {
  HirPipelineFixture fixture(
      "fun f(a: i32, b: i32) -> i32 { mut x: i32 = 0; x = a + b; return x; }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 1);
  ZC_REQUIRE(module.blocks().size() == 1);
  ZC_REQUIRE(module.returns().size() == 1);
  ZC_REQUIRE(module.locals().size() == 1);
  ZC_REQUIRE(module.localWrites().size() == 1);
  // The initializer `0` is one scalar literal; the write value `a + b` is a
  // primitive binary (its two operands are parameter references); the return of
  // x is one local reference.
  ZC_REQUIRE(module.expressions().size() == 1);
  ZC_REQUIRE(module.primitiveBinaryOperations().size() == 1);
  ZC_REQUIRE(module.parameterReferences().size() == 2);
  ZC_REQUIRE(module.localReferences().size() == 1);
  const auto& function = module.functions()[0];
  const auto& block = module.blocks()[0];
  const auto& local = module.locals()[0];
  const auto& write = module.localWrites()[0];
  const auto& initializer = module.expressions()[0];
  const auto& writeBinary = module.primitiveBinaryOperations()[0];
  // Fixed-id layout (F=1): function 1, block 2, local 3, initializer 4, write 5,
  // write value (the binary node) 6, return 7, return value 8; the binary's two
  // operands are trailing ids 9 (left) and 10 (right).
  ZC_EXPECT(function.node.ordinal() == 1);
  ZC_EXPECT(block.node.ordinal() == 2);
  ZC_EXPECT(local.node.ordinal() == 3);
  ZC_EXPECT(initializer.node.ordinal() == 4);
  ZC_EXPECT(write.node.ordinal() == 5);
  ZC_EXPECT(writeBinary.node.ordinal() == 6);
  ZC_EXPECT(writeBinary.left.ordinal() == 9);
  ZC_EXPECT(writeBinary.right.ordinal() == 10);
  ZC_EXPECT(block.statements.size() == 3);
  ZC_EXPECT(block.statements[0] == local.node);
  ZC_EXPECT(block.statements[1] == write.node);
  ZC_EXPECT(local.initializer == initializer.node);
  // The write value node id is the primitive binary, not a literal or a bare
  // parameter reference.
  ZC_EXPECT(write.value == writeBinary.node);
  ZC_EXPECT(write.kind == HirLocalWriteKind::Overwrite);
  ZC_EXPECT(write.local == local.local);
  ZC_EXPECT(writeBinary.type == local.type);
  ZC_EXPECT(writeBinary.operandType == local.type);
  ZC_EXPECT(writeBinary.category == HirValueCategory::Value);
  ZC_EXPECT(writeBinary.operation == checker::PrimitiveOperation::Add);
  // Both operands are parameter references at the trailing node ids.
  bool sawLeft = false;
  bool sawRight = false;
  for (const auto& reference : module.parameterReferences()) {
    if (reference.node == writeBinary.left) sawLeft = reference.type == local.type;
    if (reference.node == writeBinary.right) sawRight = reference.type == local.type;
  }
  ZC_EXPECT(sawLeft);
  ZC_EXPECT(sawRight);

  // Built MIR: parameters are localId(1) and localId(2), the user local
  // localId(3); the write lowers to an Arithmetic Add of the two parameter
  // place-uses into the user local.
  const auto builtMir = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(builtMir.size() == 1);
  zc::Maybe<const mir::MirFunction&> lowered;
  for (const auto& mirFunction : builtMir[0].builtMir().functions()) {
    if (mirFunction.owner == function.definition) lowered = mirFunction;
  }
  ZC_REQUIRE(lowered != zc::none);
  ZC_IF_SOME(mirFunction, lowered) {
    ZC_REQUIRE(mirFunction.locals.size() == 3);
    ZC_EXPECT(mirFunction.locals[0].kind == mir::MirLocalKind::Parameter);
    ZC_EXPECT(mirFunction.locals[1].kind == mir::MirLocalKind::Parameter);
    ZC_EXPECT(mirFunction.locals[2].kind == mir::MirLocalKind::UserLocal);
    const auto parameterA = mirFunction.locals[0].id;
    const auto parameterB = mirFunction.locals[1].id;
    const auto userLocal = mirFunction.locals[2].id;
    ZC_REQUIRE(mirFunction.blocks.size() == 1);
    const auto& mirBlock = mirFunction.blocks[0];
    // StorageLive(x), Assign(x = 0, Initialize), Assign(x = a + b, Overwrite).
    ZC_REQUIRE(mirBlock.statements.size() == 3);
    ZC_EXPECT(mirBlock.statements[0].kind() == mir::MirStatementKind::StorageLive);
    ZC_EXPECT(mirBlock.statements[0].storageLocal() == userLocal);
    const auto& initAssign = mirBlock.statements[1].assignmentValue();
    ZC_EXPECT(initAssign.initialization == mir::MirInitializationKind::Initialize);
    ZC_EXPECT(initAssign.destination.local() == userLocal);
    ZC_EXPECT(initAssign.value.kind() == mir::MirRvalueKind::Use);
    ZC_EXPECT(initAssign.value.useValue().operand.kind() == mir::MirOperandKind::Constant);
    const auto& overwriteAssign = mirBlock.statements[2].assignmentValue();
    ZC_EXPECT(overwriteAssign.initialization == mir::MirInitializationKind::Overwrite);
    ZC_EXPECT(overwriteAssign.destination.local() == userLocal);
    // The overwrite value is an Arithmetic Add of the two parameter place-uses.
    ZC_REQUIRE(overwriteAssign.value.kind() == mir::MirRvalueKind::Arithmetic);
    ZC_EXPECT(overwriteAssign.value.arithmeticValue().op == mir::MirArithmeticOperator::Add);
    ZC_EXPECT(overwriteAssign.value.arithmeticValue().left.place().local() == parameterA);
    ZC_EXPECT(overwriteAssign.value.arithmeticValue().right.place().local() == parameterB);
    ZC_REQUIRE(mirBlock.terminator.kind() == mir::MirTerminatorKind::Return);
    ZC_IF_SOME(returnValue, mirBlock.terminator.returnValue().value) {
      ZC_EXPECT(returnValue.place().local() == userLocal);
    }
  }
}

ZC_TEST("HIR pipeline lowers a binary mutable local write with a literal operand") {
  // A binary write with one literal operand `x = a + 1`: the left operand is a
  // parameter reference and the right operand is a scalar literal, so the write
  // materializes one parameter reference and one extra literal expression.
  HirPipelineFixture fixture("fun f(a: i32) -> i32 { mut x: i32 = 0; x = a + 1; return x; }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 1);
  ZC_REQUIRE(module.locals().size() == 1);
  ZC_REQUIRE(module.localWrites().size() == 1);
  ZC_REQUIRE(module.primitiveBinaryOperations().size() == 1);
  // Two literals: the initializer `0` and the operand `1`; one parameter
  // reference for the operand `a`.
  ZC_REQUIRE(module.expressions().size() == 2);
  ZC_REQUIRE(module.parameterReferences().size() == 1);
  const auto& function = module.functions()[0];
  const auto& writeBinary = module.primitiveBinaryOperations()[0];
  ZC_EXPECT(writeBinary.operation == checker::PrimitiveOperation::Add);
  bool leftIsParameter = false;
  for (const auto& reference : module.parameterReferences()) {
    if (reference.node == writeBinary.left) leftIsParameter = true;
  }
  bool rightIsLiteral = false;
  for (const auto& expression : module.expressions()) {
    if (expression.node == writeBinary.right) rightIsLiteral = true;
  }
  ZC_EXPECT(leftIsParameter);
  ZC_EXPECT(rightIsLiteral);

  const auto builtMir = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(builtMir.size() == 1);
  zc::Maybe<const mir::MirFunction&> lowered;
  for (const auto& mirFunction : builtMir[0].builtMir().functions()) {
    if (mirFunction.owner == function.definition) lowered = mirFunction;
  }
  ZC_REQUIRE(lowered != zc::none);
  ZC_IF_SOME(mirFunction, lowered) {
    ZC_REQUIRE(mirFunction.locals.size() == 2);
    const auto parameterA = mirFunction.locals[0].id;
    const auto& mirBlock = mirFunction.blocks[0];
    ZC_REQUIRE(mirBlock.statements.size() == 3);
    const auto& overwriteAssign = mirBlock.statements[2].assignmentValue();
    ZC_REQUIRE(overwriteAssign.value.kind() == mir::MirRvalueKind::Arithmetic);
    // Left is a copy place-use of parameter a; right is a scalar-literal constant.
    ZC_EXPECT(overwriteAssign.value.arithmeticValue().left.place().local() == parameterA);
    ZC_EXPECT(overwriteAssign.value.arithmeticValue().right.kind() ==
              mir::MirOperandKind::Constant);
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

  const auto ownershipInputs = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(ownershipInputs.size() == 1);
  const auto& inputs = ownershipInputs[0].facts();
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

ZC_TEST("HIR pipeline lowers an unsafe block wrapping a parameter reborrow") {
  HirPipelineFixture fixture("fun entry() -> i32 { return unsafe { 1 }; }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 1);
  ZC_REQUIRE(module.unsafeBlocks().size() == 1);
}

ZC_TEST("HIR pipeline lowers an unsafe block wrapping a direct parameter reborrow") {
  HirPipelineFixture fixture("fun entry(p: &i32) -> &i32 { return unsafe { &*p }; }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 1);
  ZC_REQUIRE(module.returns().size() == 1);
  ZC_REQUIRE(module.parameterReborrows().size() == 1);
  ZC_REQUIRE(module.unsafeBlocks().size() == 1);
  const auto& function = module.functions()[0];
  const auto& reborrow = module.parameterReborrows()[0];
  const auto& unsafeBlock = module.unsafeBlocks()[0];
  ZC_EXPECT(function.unsafeBlock != zc::none);
  ZC_EXPECT(unsafeBlock.node == ZC_ASSERT_NONNULL(function.unsafeBlock));
  ZC_EXPECT(unsafeBlock.body == reborrow.node);
  ZC_EXPECT(unsafeBlock.type == function.resultType);
  ZC_EXPECT(reborrow.type == function.resultType);
  ZC_EXPECT(reborrow.sourceAlias == zc::none);
}

ZC_TEST("HIR pipeline lowers an unsafe block wrapping a local-alias reborrow") {
  HirPipelineFixture fixture("fun entry(p: &i32) -> &i32 { let y = p; return unsafe { &*y }; }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 1);
  ZC_REQUIRE(module.returns().size() == 1);
  ZC_REQUIRE(module.parameterReborrows().size() == 1);
  ZC_REQUIRE(module.unsafeBlocks().size() == 1);
  const auto& function = module.functions()[0];
  const auto& reborrow = module.parameterReborrows()[0];
  const auto& unsafeBlock = module.unsafeBlocks()[0];
  ZC_EXPECT(function.unsafeBlock != zc::none);
  ZC_EXPECT(unsafeBlock.node == ZC_ASSERT_NONNULL(function.unsafeBlock));
  ZC_EXPECT(unsafeBlock.body == reborrow.node);
  ZC_EXPECT(unsafeBlock.type == function.resultType);
  ZC_EXPECT(reborrow.type == function.resultType);
  ZC_EXPECT(reborrow.sourceAlias != zc::none);
}

ZC_TEST("HIR pipeline lowers an admitted while loop") {
  HirPipelineFixture fixture("fun spin(cond: bool) -> i32 { while (cond) { } return 0; }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 1);
  ZC_REQUIRE(module.returns().size() == 1);
  ZC_REQUIRE(module.loops().size() == 1);
  ZC_REQUIRE(module.parameterReferences().size() == 1);
  ZC_REQUIRE(module.expressions().size() == 1);
  const auto& function = module.functions()[0];
  const auto& block = module.blocks()[0];
  const auto& returnStatement = module.returns()[0];
  const auto& loop = module.loops()[0];
  const auto& condition = module.parameterReferences()[0];
  const auto& returnValue = module.expressions()[0];
  // The body block holds the loop statement followed by the scalar return.
  ZC_REQUIRE(block.statements.size() == 2);
  ZC_EXPECT(block.statements[0] == loop.node);
  ZC_EXPECT(block.statements[1] == returnStatement.node);
  ZC_EXPECT(loop.condition == condition.node);
  ZC_EXPECT(loop.type == condition.type);
  ZC_EXPECT(loop.category == HirValueCategory::Place);
  ZC_EXPECT(condition.category == HirValueCategory::Place);
  ZC_EXPECT(condition.parameter == function.parameters[0].key);
  ZC_EXPECT(returnStatement.value == returnValue.node);
  ZC_EXPECT(returnValue.type == function.resultType);
  ZC_EXPECT(returnValue.value.tag() == checker::signature::CanonicalConstValueTag::Integer);
  auto dump = module.dump();
  ZC_REQUIRE(dump != zc::none);
  ZC_IF_SOME(text, dump) {
    ZC_EXPECT(text.contains("loop h"_zc));
    ZC_EXPECT(!text.contains("NodeId"_zc));
  }

  // The admitted loop lowers to a reducible four-block Built MIR CFG that the
  // ownership pipeline accepts end-to-end (validLoopReturnFunction).
  const auto builtMir = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(builtMir.size() == 1);
  zc::Maybe<const mir::MirFunction&> spin;
  for (const auto& mirFunction : builtMir[0].builtMir().functions()) {
    if (mirFunction.owner == function.definition) spin = mirFunction;
  }
  ZC_REQUIRE(spin != zc::none);
  ZC_IF_SOME(mirFunction, spin) {
    ZC_REQUIRE(mirFunction.blocks.size() == 4);
    // bb1 entry: StorageLive(result) ; Goto(bb2)
    ZC_REQUIRE(mirFunction.blocks[0].statements.size() == 1);
    ZC_EXPECT(mirFunction.blocks[0].statements[0].kind() == mir::MirStatementKind::StorageLive);
    ZC_EXPECT(mirFunction.blocks[0].terminator.kind() == mir::MirTerminatorKind::Goto);
    ZC_EXPECT(mirFunction.blocks[0].terminator.gotoValue().target == mirFunction.blocks[1].id);
    // bb2 header: SwitchInt(cond, [true -> bb3], default = bb4)
    ZC_EXPECT(mirFunction.blocks[1].statements.size() == 0);
    ZC_REQUIRE(mirFunction.blocks[1].terminator.kind() == mir::MirTerminatorKind::SwitchInt);
    const auto& switchInt = mirFunction.blocks[1].terminator.switchIntValue();
    ZC_REQUIRE(switchInt.arms.size() == 1);
    ZC_EXPECT(switchInt.arms[0].target == mirFunction.blocks[2].id);
    ZC_EXPECT(switchInt.defaultTarget == mirFunction.blocks[3].id);
    ZC_EXPECT(switchInt.discriminant.kind() == mir::MirOperandKind::Copy);
    // bb3 body: reducible back-edge Goto(bb2)
    ZC_EXPECT(mirFunction.blocks[2].statements.size() == 0);
    ZC_REQUIRE(mirFunction.blocks[2].terminator.kind() == mir::MirTerminatorKind::Goto);
    ZC_EXPECT(mirFunction.blocks[2].terminator.gotoValue().target == mirFunction.blocks[1].id);
    // bb4 exit: Assign(result = 0, Initialize) ; Return
    ZC_REQUIRE(mirFunction.blocks[3].statements.size() == 1);
    ZC_EXPECT(mirFunction.blocks[3].statements[0].kind() == mir::MirStatementKind::Assign);
    ZC_EXPECT(mirFunction.blocks[3].terminator.kind() == mir::MirTerminatorKind::Return);
  }
}

ZC_TEST("HIR pipeline lowers a while loop whose body writes a mutable local") {
  HirPipelineFixture fixture(
      "fun f(a: i32, cond: bool) -> i32 { mut x: i32 = 0; while (cond) { x = a; } return x; }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 1);
  ZC_REQUIRE(module.blocks().size() == 1);
  ZC_REQUIRE(module.returns().size() == 1);
  ZC_REQUIRE(module.loops().size() == 1);
  ZC_REQUIRE(module.locals().size() == 1);
  ZC_REQUIRE(module.localWrites().size() == 1);
  // The initializer `0` is one scalar literal; the loop condition `cond` and the
  // write value `a` are two parameter references; the return of x is one local
  // reference.
  ZC_REQUIRE(module.expressions().size() == 1);
  ZC_REQUIRE(module.parameterReferences().size() == 2);
  ZC_REQUIRE(module.localReferences().size() == 1);
  const auto& function = module.functions()[0];
  const auto& block = module.blocks()[0];
  const auto& local = module.locals()[0];
  const auto& loop = module.loops()[0];
  const auto& write = module.localWrites()[0];
  const auto& returnStatement = module.returns()[0];
  // Fixed-id layout (F=1): function 1, block 2, local 3, initializer 4, write 5,
  // write value 6, return 7, return value (local ref) 8, condition param-ref 9,
  // loop 10.
  ZC_EXPECT(function.node.ordinal() == 1);
  ZC_EXPECT(block.node.ordinal() == 2);
  ZC_EXPECT(local.node.ordinal() == 3);
  ZC_EXPECT(write.node.ordinal() == 5);
  ZC_EXPECT(returnStatement.node.ordinal() == 7);
  ZC_EXPECT(loop.node.ordinal() == 10);
  // The function body block is `[local, loop, return]`, and the loop carries the
  // single write as its body.
  ZC_REQUIRE(block.statements.size() == 3);
  ZC_EXPECT(block.statements[0] == local.node);
  ZC_EXPECT(block.statements[1] == loop.node);
  ZC_EXPECT(block.statements[2] == returnStatement.node);
  ZC_REQUIRE(loop.body.size() == 1);
  ZC_EXPECT(loop.body[0] == write.node);
  ZC_EXPECT(loop.category == HirValueCategory::Place);
  ZC_EXPECT(write.kind == HirLocalWriteKind::Overwrite);
  ZC_EXPECT(write.local == local.local);

  // The composite lowers to a reducible four-block Built MIR CFG whose body block
  // carries the overwrite before the back-edge Goto. The result local is the user
  // local x (localId params+1 = 3).
  const auto builtMir = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(builtMir.size() == 1);
  zc::Maybe<const mir::MirFunction&> f;
  for (const auto& mirFunction : builtMir[0].builtMir().functions()) {
    if (mirFunction.owner == function.definition) f = mirFunction;
  }
  ZC_REQUIRE(f != zc::none);
  ZC_IF_SOME(mirFunction, f) {
    ZC_REQUIRE(mirFunction.blocks.size() == 4);
    // bb1 entry: StorageLive(x) ; Assign(x = 0, Initialize) ; Goto(bb2)
    ZC_REQUIRE(mirFunction.blocks[0].statements.size() == 2);
    ZC_EXPECT(mirFunction.blocks[0].statements[0].kind() == mir::MirStatementKind::StorageLive);
    ZC_EXPECT(mirFunction.blocks[0].statements[1].kind() == mir::MirStatementKind::Assign);
    ZC_EXPECT(mirFunction.blocks[0].terminator.kind() == mir::MirTerminatorKind::Goto);
    ZC_EXPECT(mirFunction.blocks[0].terminator.gotoValue().target == mirFunction.blocks[1].id);
    // bb2 header: SwitchInt(cond, [true -> bb3], default = bb4)
    ZC_EXPECT(mirFunction.blocks[1].statements.size() == 0);
    ZC_REQUIRE(mirFunction.blocks[1].terminator.kind() == mir::MirTerminatorKind::SwitchInt);
    const auto& switchInt = mirFunction.blocks[1].terminator.switchIntValue();
    ZC_REQUIRE(switchInt.arms.size() == 1);
    ZC_EXPECT(switchInt.arms[0].target == mirFunction.blocks[2].id);
    ZC_EXPECT(switchInt.defaultTarget == mirFunction.blocks[3].id);
    // bb3 body: Assign(x = a, Overwrite) ; back-edge Goto(bb2)
    ZC_REQUIRE(mirFunction.blocks[2].statements.size() == 1);
    ZC_EXPECT(mirFunction.blocks[2].statements[0].kind() == mir::MirStatementKind::Assign);
    ZC_EXPECT(mirFunction.blocks[2].statements[0].assignmentValue().initialization ==
              mir::MirInitializationKind::Overwrite);
    ZC_REQUIRE(mirFunction.blocks[2].terminator.kind() == mir::MirTerminatorKind::Goto);
    ZC_EXPECT(mirFunction.blocks[2].terminator.gotoValue().target == mirFunction.blocks[1].id);
    // bb4 exit: Return(placeUse(x))
    ZC_EXPECT(mirFunction.blocks[3].statements.size() == 0);
    ZC_EXPECT(mirFunction.blocks[3].terminator.kind() == mir::MirTerminatorKind::Return);
  }
}

ZC_TEST("HIR pipeline lowers a two-arm scalar-literal conditional") {
  HirPipelineFixture fixture(
      "fun pick(c: bool) -> i32 { if (c) { return 1; } else { return 2; } }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 1);
  ZC_REQUIRE(module.returns().size() == 1);
  ZC_REQUIRE(module.conditionals().size() == 1);
  // The condition is the sole parameter reference; both arms are scalar literals.
  ZC_REQUIRE(module.parameterReferences().size() == 1);
  ZC_REQUIRE(module.expressions().size() == 2);
  const auto& function = module.functions()[0];
  const auto& conditional = module.conditionals()[0];
  const auto& condition = module.parameterReferences()[0];
  ZC_EXPECT(conditional.condition == condition.node);
  ZC_EXPECT(condition.parameter == function.parameters[0].key);
  ZC_EXPECT(conditional.type == function.resultType);

  // The conditional lowers to a four-block CFG: SwitchInt on the c parameter,
  // one constant-initializing branch per arm, and a single join Return.
  const auto builtMir = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(builtMir.size() == 1);
  zc::Maybe<const mir::MirFunction&> pick;
  for (const auto& mirFunction : builtMir[0].builtMir().functions()) {
    if (mirFunction.owner == function.definition) pick = mirFunction;
  }
  ZC_REQUIRE(pick != zc::none);
  ZC_IF_SOME(mirFunction, pick) {
    ZC_REQUIRE(mirFunction.blocks.size() == 4);
    ZC_REQUIRE(mirFunction.blocks[0].terminator.kind() == mir::MirTerminatorKind::SwitchInt);
    const auto& switchInt = mirFunction.blocks[0].terminator.switchIntValue();
    ZC_EXPECT(switchInt.discriminant.kind() == mir::MirOperandKind::Copy);
    ZC_EXPECT(switchInt.discriminant.place().local() == mirFunction.locals[0].id);
    // bb2 / bb3: Assign(result = constant, Initialize) ; Goto(bb4)
    ZC_REQUIRE(mirFunction.blocks[1].statements.size() == 1);
    ZC_EXPECT(mirFunction.blocks[1].statements[0].kind() == mir::MirStatementKind::Assign);
    ZC_EXPECT(
        mirFunction.blocks[1].statements[0].assignmentValue().value.useValue().operand.kind() ==
        mir::MirOperandKind::Constant);
    ZC_EXPECT(mirFunction.blocks[1].terminator.kind() == mir::MirTerminatorKind::Goto);
    ZC_REQUIRE(mirFunction.blocks[2].statements.size() == 1);
    ZC_EXPECT(
        mirFunction.blocks[2].statements[0].assignmentValue().value.useValue().operand.kind() ==
        mir::MirOperandKind::Constant);
    ZC_EXPECT(mirFunction.blocks[2].terminator.kind() == mir::MirTerminatorKind::Goto);
    // bb4 join: Return(placeUse(result))
    ZC_EXPECT(mirFunction.blocks[3].statements.size() == 0);
    ZC_EXPECT(mirFunction.blocks[3].terminator.kind() == mir::MirTerminatorKind::Return);
  }
}

ZC_TEST("HIR pipeline lowers a two-arm parameter conditional") {
  HirPipelineFixture fixture(
      "fun choose(c: bool, a: i32, b: i32) -> i32 { if (c) { return a; } else { return b; } }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 1);
  ZC_REQUIRE(module.returns().size() == 1);
  ZC_REQUIRE(module.conditionals().size() == 1);
  // Both arms are parameter references, plus the condition: three references and
  // zero scalar-literal expressions for this function.
  ZC_REQUIRE(module.parameterReferences().size() == 3);
  ZC_REQUIRE(module.expressions().size() == 0);
  const auto& function = module.functions()[0];
  const auto& conditional = module.conditionals()[0];
  ZC_EXPECT(conditional.type == function.resultType);

  // The conditional lowers to a four-block CFG: SwitchInt on the c parameter,
  // one place-use-initializing branch per parameter arm, and a single join Return.
  const auto builtMir = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(builtMir.size() == 1);
  zc::Maybe<const mir::MirFunction&> choose;
  for (const auto& mirFunction : builtMir[0].builtMir().functions()) {
    if (mirFunction.owner == function.definition) choose = mirFunction;
  }
  ZC_REQUIRE(choose != zc::none);
  ZC_IF_SOME(mirFunction, choose) {
    // Three parameter locals (c, a, b) plus the function result.
    ZC_REQUIRE(mirFunction.locals.size() == 4);
    ZC_REQUIRE(mirFunction.blocks.size() == 4);
    // bb1 header: SwitchInt on the c parameter local (index 0).
    ZC_REQUIRE(mirFunction.blocks[0].terminator.kind() == mir::MirTerminatorKind::SwitchInt);
    const auto& switchInt = mirFunction.blocks[0].terminator.switchIntValue();
    ZC_EXPECT(switchInt.discriminant.kind() == mir::MirOperandKind::Copy);
    ZC_EXPECT(switchInt.discriminant.place().local() == mirFunction.locals[0].id);
    // bb2: Assign(result = copy/move of the a parameter local) ; Goto(bb4)
    ZC_REQUIRE(mirFunction.blocks[1].statements.size() == 1);
    const auto& thenAssign = mirFunction.blocks[1].statements[0].assignmentValue();
    ZC_EXPECT(thenAssign.value.useValue().operand.kind() != mir::MirOperandKind::Constant);
    ZC_EXPECT(thenAssign.value.useValue().operand.place().local() == mirFunction.locals[1].id);
    ZC_EXPECT(mirFunction.blocks[1].terminator.kind() == mir::MirTerminatorKind::Goto);
    // bb3: Assign(result = copy/move of the b parameter local) ; Goto(bb4)
    ZC_REQUIRE(mirFunction.blocks[2].statements.size() == 1);
    const auto& elseAssign = mirFunction.blocks[2].statements[0].assignmentValue();
    ZC_EXPECT(elseAssign.value.useValue().operand.kind() != mir::MirOperandKind::Constant);
    ZC_EXPECT(elseAssign.value.useValue().operand.place().local() == mirFunction.locals[2].id);
    ZC_EXPECT(mirFunction.blocks[2].terminator.kind() == mir::MirTerminatorKind::Goto);
    // bb4 join: Return(placeUse(result))
    ZC_EXPECT(mirFunction.blocks[3].statements.size() == 0);
    ZC_EXPECT(mirFunction.blocks[3].terminator.kind() == mir::MirTerminatorKind::Return);
  }
}

ZC_TEST("HIR pipeline lowers an equality-comparison conditional condition") {
  HirPipelineFixture fixture(
      "fun eq(a: i32, b: i32) -> i32 { if (a == b) { return 1; } else { return 2; } }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 1);
  ZC_REQUIRE(module.returns().size() == 1);
  ZC_REQUIRE(module.conditionals().size() == 1);
  // The `a == b` condition materializes one equality comparison plus its two
  // operand parameter references; both arms are scalar literals.
  ZC_REQUIRE(module.primitiveBinaryOperations().size() == 1);
  ZC_REQUIRE(module.parameterReferences().size() == 2);
  ZC_REQUIRE(module.expressions().size() == 2);
  const auto& function = module.functions()[0];
  const auto& conditional = module.conditionals()[0];
  const auto& equality = module.primitiveBinaryOperations()[0];
  const auto& left = module.parameterReferences()[0];
  const auto& right = module.parameterReferences()[1];
  ZC_EXPECT(conditional.condition == equality.node);
  ZC_EXPECT(equality.left == left.node);
  ZC_EXPECT(equality.right == right.node);
  ZC_EXPECT(left.parameter == function.parameters[0].key);
  ZC_EXPECT(right.parameter == function.parameters[1].key);
  ZC_EXPECT(equality.operandType == function.parameters[0].type);
  ZC_EXPECT(conditional.type == function.resultType);

  // The condition lowers to a four-block CFG whose entry block computes the
  // comparison into a bool temporary that feeds the SwitchInt discriminant.
  const auto builtMir = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(builtMir.size() == 1);
  zc::Maybe<const mir::MirFunction&> eq;
  for (const auto& mirFunction : builtMir[0].builtMir().functions()) {
    if (mirFunction.owner == function.definition) eq = mirFunction;
  }
  ZC_REQUIRE(eq != zc::none);
  ZC_IF_SOME(mirFunction, eq) {
    // Two parameter locals (a, b), the function result, and the bool temporary.
    ZC_REQUIRE(mirFunction.locals.size() == 4);
    ZC_REQUIRE(mirFunction.blocks.size() == 4);
    // bb1 entry: StorageLive(result), StorageLive(temp), Assign(temp = a == b).
    ZC_REQUIRE(mirFunction.blocks[0].statements.size() == 3);
    ZC_EXPECT(mirFunction.blocks[0].statements[0].kind() == mir::MirStatementKind::StorageLive);
    ZC_EXPECT(mirFunction.blocks[0].statements[1].kind() == mir::MirStatementKind::StorageLive);
    ZC_EXPECT(mirFunction.blocks[0].statements[2].kind() == mir::MirStatementKind::Assign);
    const auto& compareAssign = mirFunction.blocks[0].statements[2].assignmentValue();
    ZC_REQUIRE(compareAssign.value.kind() == mir::MirRvalueKind::Comparison);
    const auto& comparison = compareAssign.value.comparisonValue();
    ZC_EXPECT(comparison.op == mir::MirComparisonOperator::Eq);
    ZC_EXPECT(comparison.left.place().local() == mirFunction.locals[0].id);
    ZC_EXPECT(comparison.right.place().local() == mirFunction.locals[1].id);
    ZC_REQUIRE(mirFunction.blocks[0].terminator.kind() == mir::MirTerminatorKind::SwitchInt);
    const auto& switchInt = mirFunction.blocks[0].terminator.switchIntValue();
    ZC_EXPECT(switchInt.discriminant.kind() == mir::MirOperandKind::Copy);
    // The discriminant reads the bool temporary (the fourth local), not a parameter.
    ZC_EXPECT(switchInt.discriminant.place().local() == mirFunction.locals[3].id);
    // bb2 / bb3: constant-initializing branches; bb4: single join Return.
    ZC_EXPECT(mirFunction.blocks[1].terminator.kind() == mir::MirTerminatorKind::Goto);
    ZC_EXPECT(mirFunction.blocks[2].terminator.kind() == mir::MirTerminatorKind::Goto);
    ZC_EXPECT(mirFunction.blocks[3].statements.size() == 0);
    ZC_EXPECT(mirFunction.blocks[3].terminator.kind() == mir::MirTerminatorKind::Return);
  }
}

ZC_TEST("HIR pipeline lowers a less-than relational conditional condition") {
  HirPipelineFixture fixture(
      "fun lt(a: i32, b: i32) -> i32 { if (a < b) { return 1; } else { return 2; } }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 1);
  ZC_REQUIRE(module.conditionals().size() == 1);
  ZC_REQUIRE(module.primitiveBinaryOperations().size() == 1);
  const auto& function = module.functions()[0];
  const auto& equality = module.primitiveBinaryOperations()[0];
  // The HIR comparison node carries the relational operator selected by the
  // checked PrimitiveCallable.
  ZC_EXPECT(equality.operation == checker::PrimitiveOperation::Lt);

  const auto builtMir = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(builtMir.size() == 1);
  zc::Maybe<const mir::MirFunction&> lt;
  for (const auto& mirFunction : builtMir[0].builtMir().functions()) {
    if (mirFunction.owner == function.definition) lt = mirFunction;
  }
  ZC_REQUIRE(lt != zc::none);
  ZC_IF_SOME(mirFunction, lt) {
    const auto& compareAssign = mirFunction.blocks[0].statements[2].assignmentValue();
    ZC_REQUIRE(compareAssign.value.kind() == mir::MirRvalueKind::Comparison);
    // The MIR Comparison rvalue op is the operator mapped from the HIR Lt.
    ZC_EXPECT(compareAssign.value.comparisonValue().op == mir::MirComparisonOperator::Lt);
  }
}

ZC_TEST("Built MIR comparison rvalue byte oracle is stable and mutation sensitive") {
  HirPipelineFixture fixture(
      "fun eq(a: i32, b: i32) -> i32 { if (a == b) { return 1; } else { return 2; } }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 1);
  const auto& function = module.functions()[0];
  const auto builtMir = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(builtMir.size() == 1);
  zc::Maybe<const mir::VerifiedBuiltMir&> verified = builtMir[0].builtMir();

  // Locate the one canonical function record that carries the Comparison rvalue.
  // The record is produced by the live canonical encoder; no bytes are fabricated.
  zc::Maybe<zc::Array<uint8_t>> comparisonRecord;
  ZC_IF_SOME(mir, verified) {
    ZC_REQUIRE(mir.functions().size() == mir.canonicalFunctionRecords().size());
    for (size_t index = 0; index < mir.functions().size(); ++index) {
      if (mir.functions()[index].owner != function.definition) continue;
      comparisonRecord = zc::heapArray(mir.canonicalFunctionRecords()[index].asPtr());
    }
  }
  ZC_REQUIRE(comparisonRecord != zc::none);

  // Frame the live record and derive its digest from the live encoder. The
  // Comparison rvalue encodes tag 0x03, operator 0x01 (Eq), then two Copy
  // operands (0x01) and the bool result type; find that exact byte window.
  auto framedDigest = [](zc::ArrayPtr<const uint8_t> record) -> zc::String {
    uint8_t moduleKey[32];
    for (auto& value : moduleKey) value = uint8_t{0};
    ZC_IF_SOME(fingerprint, identity::Sha256Digest::fromBytes(zc::arrayPtr(moduleKey))) {
      const uint8_t moduleId[] = {0xa1};
      zc::Vector<zc::Array<uint8_t>> records;
      records.add(zc::heapArray(record));
      auto encoded =
          mir::MirRevisionCodec::encodeBuiltFramed(fingerprint, zc::arrayPtr(moduleId), fingerprint,
                                                   fingerprint, fingerprint, records.asPtr());
      ZC_REQUIRE(encoded != zc::none);
      auto digest = identity::sha256(ZC_REQUIRE_NONNULL(encoded).asPtr());
      ZC_REQUIRE(digest != zc::none);
      return zc::encodeHex(ZC_REQUIRE_NONNULL(digest).bytes());
    }
    ZC_FAIL_REQUIRE("invalid digest fixture");
  };

  zc::Array<uint8_t> record = zc::mv(ZC_REQUIRE_NONNULL(comparisonRecord));
  zc::Maybe<size_t> comparisonOffset;
  for (size_t i = 0; i + 2 < record.size(); ++i) {
    if (record[i] == 0x03 && record[i + 1] == 0x01 && record[i + 2] == 0x01) {
      ZC_REQUIRE(comparisonOffset == zc::none);
      comparisonOffset = i;
    }
  }
  ZC_REQUIRE(comparisonOffset != zc::none);
  const size_t comparisonKind = ZC_REQUIRE_NONNULL(comparisonOffset);
  const size_t operatorByte = comparisonKind + 1;
  const size_t leftOperandKind = comparisonKind + 2;
  ZC_REQUIRE(record[comparisonKind] == 0x03);
  ZC_REQUIRE(record[operatorByte] == 0x01);
  ZC_REQUIRE(record[leftOperandKind] == 0x01);

  const auto baseline = framedDigest(record.asPtr());

  // Mutating the comparison operator changes the derived digest.
  {
    auto mutated = zc::heapArray(record.asPtr());
    mutated[operatorByte] = 0x02;
    ZC_EXPECT(framedDigest(mutated.asPtr()) != baseline);
  }
  // Mutating the left operand kind (Copy -> Move) changes the derived digest.
  {
    auto mutated = zc::heapArray(record.asPtr());
    mutated[leftOperandKind] = 0x02;
    ZC_EXPECT(framedDigest(mutated.asPtr()) != baseline);
  }
  // Re-framing the unmutated record reproduces the same digest exactly.
  ZC_EXPECT(framedDigest(record.asPtr()) == baseline);
}

ZC_TEST(
    "Built MIR less-than comparison rvalue emits the Lt operator byte through the live encoder") {
  HirPipelineFixture fixture(
      "fun lt(a: i32, b: i32) -> i32 { if (a < b) { return 1; } else { return 2; } }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 1);
  const auto& function = module.functions()[0];
  const auto builtMir = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(builtMir.size() == 1);
  zc::Maybe<const mir::VerifiedBuiltMir&> verified = builtMir[0].builtMir();

  // Pull the live canonical function record for the `a < b` function; no bytes
  // are fabricated.
  zc::Maybe<zc::Array<uint8_t>> comparisonRecord;
  ZC_IF_SOME(mir, verified) {
    ZC_REQUIRE(mir.functions().size() == mir.canonicalFunctionRecords().size());
    for (size_t index = 0; index < mir.functions().size(); ++index) {
      if (mir.functions()[index].owner != function.definition) continue;
      comparisonRecord = zc::heapArray(mir.canonicalFunctionRecords()[index].asPtr());
    }
  }
  ZC_REQUIRE(comparisonRecord != zc::none);

  auto framedDigest = [](zc::ArrayPtr<const uint8_t> record) -> zc::String {
    uint8_t moduleKey[32];
    for (auto& value : moduleKey) value = uint8_t{0};
    ZC_IF_SOME(fingerprint, identity::Sha256Digest::fromBytes(zc::arrayPtr(moduleKey))) {
      const uint8_t moduleId[] = {0xa1};
      zc::Vector<zc::Array<uint8_t>> records;
      records.add(zc::heapArray(record));
      auto encoded =
          mir::MirRevisionCodec::encodeBuiltFramed(fingerprint, zc::arrayPtr(moduleId), fingerprint,
                                                   fingerprint, fingerprint, records.asPtr());
      ZC_REQUIRE(encoded != zc::none);
      auto digest = identity::sha256(ZC_REQUIRE_NONNULL(encoded).asPtr());
      ZC_REQUIRE(digest != zc::none);
      return zc::encodeHex(ZC_REQUIRE_NONNULL(digest).bytes());
    }
    ZC_FAIL_REQUIRE("invalid digest fixture");
  };

  // The Comparison rvalue encodes tag 0x03, operator 0x03 (Lt), then two Copy
  // operands (0x01); find that exact byte window in the live record.
  zc::Array<uint8_t> record = zc::mv(ZC_REQUIRE_NONNULL(comparisonRecord));
  zc::Maybe<size_t> comparisonOffset;
  for (size_t i = 0; i + 2 < record.size(); ++i) {
    if (record[i] == 0x03 && record[i + 1] == 0x03 && record[i + 2] == 0x01) {
      ZC_REQUIRE(comparisonOffset == zc::none);
      comparisonOffset = i;
    }
  }
  ZC_REQUIRE(comparisonOffset != zc::none);
  const size_t comparisonKind = ZC_REQUIRE_NONNULL(comparisonOffset);
  const size_t operatorByte = comparisonKind + 1;
  // The operator byte is the new Lt tag emitted by the live encoder.
  ZC_REQUIRE(record[operatorByte] == 0x03);

  const auto baseline = framedDigest(record.asPtr());
  // Mutating the Lt operator byte to any other operator changes the digest, and
  // re-framing the unmutated record reproduces it exactly.
  {
    auto mutated = zc::heapArray(record.asPtr());
    mutated[operatorByte] = 0x01;
    ZC_EXPECT(framedDigest(mutated.asPtr()) != baseline);
  }
  ZC_EXPECT(framedDigest(record.asPtr()) == baseline);
}

ZC_TEST("HIR pipeline lowers a parameter-and-literal relational conditional condition") {
  HirPipelineFixture fixture(
      "fun lt(a: i32) -> i32 { if (a < 5) { return 1; } else { return 2; } }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 1);
  ZC_REQUIRE(module.conditionals().size() == 1);
  ZC_REQUIRE(module.primitiveBinaryOperations().size() == 1);
  // The `a < 5` condition materializes the equality comparison, one operand
  // parameter reference (a), and the literal operand `5` into expressions along
  // with the two scalar-literal arms.
  ZC_REQUIRE(module.parameterReferences().size() == 1);
  ZC_REQUIRE(module.expressions().size() == 3);
  const auto& function = module.functions()[0];
  const auto& conditional = module.conditionals()[0];
  const auto& equality = module.primitiveBinaryOperations()[0];
  const auto& operandRef = module.parameterReferences()[0];
  ZC_EXPECT(conditional.condition == equality.node);
  ZC_EXPECT(equality.operation == checker::PrimitiveOperation::Lt);
  // The left operand is the parameter reference; the right operand id points at
  // a scalar-literal expression, proving the operand id is node-kind-agnostic.
  ZC_EXPECT(equality.left == operandRef.node);
  ZC_EXPECT(operandRef.parameter == function.parameters[0].key);
  bool rightIsLiteralExpression = false;
  for (const auto& expression : module.expressions()) {
    if (expression.node == equality.right) rightIsLiteralExpression = true;
  }
  ZC_EXPECT(rightIsLiteralExpression);
  ZC_EXPECT(equality.operandType == function.parameters[0].type);

  // The condition lowers to a four-block CFG whose entry block computes the
  // comparison of a parameter copy and a constant into a bool temporary.
  const auto builtMir = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(builtMir.size() == 1);
  zc::Maybe<const mir::MirFunction&> lt;
  for (const auto& mirFunction : builtMir[0].builtMir().functions()) {
    if (mirFunction.owner == function.definition) lt = mirFunction;
  }
  ZC_REQUIRE(lt != zc::none);
  ZC_IF_SOME(mirFunction, lt) {
    // One parameter local (a), the function result, and the bool temporary.
    ZC_REQUIRE(mirFunction.locals.size() == 3);
    ZC_REQUIRE(mirFunction.blocks.size() == 4);
    ZC_REQUIRE(mirFunction.blocks[0].statements.size() == 3);
    const auto& compareAssign = mirFunction.blocks[0].statements[2].assignmentValue();
    ZC_REQUIRE(compareAssign.value.kind() == mir::MirRvalueKind::Comparison);
    const auto& comparison = compareAssign.value.comparisonValue();
    ZC_EXPECT(comparison.op == mir::MirComparisonOperator::Lt);
    // The comparison feeds one Copy operand (the parameter) and one Constant
    // operand (the literal).
    ZC_EXPECT(comparison.left.kind() == mir::MirOperandKind::Copy);
    ZC_EXPECT(comparison.left.place().local() == mirFunction.locals[0].id);
    ZC_EXPECT(comparison.right.kind() == mir::MirOperandKind::Constant);
    ZC_REQUIRE(mirFunction.blocks[0].terminator.kind() == mir::MirTerminatorKind::SwitchInt);
    ZC_EXPECT(mirFunction.blocks[3].terminator.kind() == mir::MirTerminatorKind::Return);
  }
}

ZC_TEST("HIR pipeline lowers a return-position relational comparison") {
  HirPipelineFixture fixture("fun lt(a: i32, b: i32) -> bool { return a < b; }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 1);
  ZC_REQUIRE(module.returns().size() == 1);
  // The comparison result is returned directly: no conditional is produced, only
  // the comparison node plus its two operand parameter references.
  ZC_REQUIRE(module.conditionals().size() == 0);
  ZC_REQUIRE(module.primitiveBinaryOperations().size() == 1);
  ZC_REQUIRE(module.parameterReferences().size() == 2);
  ZC_REQUIRE(module.expressions().size() == 0);
  const auto& function = module.functions()[0];
  const auto& equality = module.primitiveBinaryOperations()[0];
  const auto& returnStatement = module.returns()[0];
  const auto& left = module.parameterReferences()[0];
  const auto& right = module.parameterReferences()[1];
  // The return statement feeds the comparison node directly (no conditional).
  ZC_EXPECT(returnStatement.value == equality.node);
  ZC_EXPECT(equality.left == left.node);
  ZC_EXPECT(equality.right == right.node);
  ZC_EXPECT(equality.operation == checker::PrimitiveOperation::Lt);
  ZC_EXPECT(left.parameter == function.parameters[0].key);
  ZC_EXPECT(right.parameter == function.parameters[1].key);
  ZC_EXPECT(equality.operandType == function.parameters[0].type);
  ZC_EXPECT(equality.type == function.resultType);

  // The body lowers to a single block: StorageLive(result), Assign(result =
  // a < b), then Return(placeUse(result)). No branching.
  const auto builtMir = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(builtMir.size() == 1);
  zc::Maybe<const mir::MirFunction&> lt;
  for (const auto& mirFunction : builtMir[0].builtMir().functions()) {
    if (mirFunction.owner == function.definition) lt = mirFunction;
  }
  ZC_REQUIRE(lt != zc::none);
  ZC_IF_SOME(mirFunction, lt) {
    // Two parameter locals (a, b) plus the bool function result. No temporary is
    // needed because the comparison writes the result local directly.
    ZC_REQUIRE(mirFunction.locals.size() == 3);
    ZC_REQUIRE(mirFunction.blocks.size() == 1);
    ZC_REQUIRE(mirFunction.blocks[0].statements.size() == 2);
    ZC_EXPECT(mirFunction.blocks[0].statements[0].kind() == mir::MirStatementKind::StorageLive);
    ZC_EXPECT(mirFunction.blocks[0].statements[1].kind() == mir::MirStatementKind::Assign);
    const auto& compareAssign = mirFunction.blocks[0].statements[1].assignmentValue();
    ZC_REQUIRE(compareAssign.value.kind() == mir::MirRvalueKind::Comparison);
    const auto& comparison = compareAssign.value.comparisonValue();
    ZC_EXPECT(comparison.op == mir::MirComparisonOperator::Lt);
    ZC_EXPECT(comparison.left.place().local() == mirFunction.locals[0].id);
    ZC_EXPECT(comparison.right.place().local() == mirFunction.locals[1].id);
    // The assignment initializes the function-result local (the third local).
    ZC_EXPECT(compareAssign.destination.local() == mirFunction.locals[2].id);
    // The single block returns the result local directly.
    ZC_REQUIRE(mirFunction.blocks[0].terminator.kind() == mir::MirTerminatorKind::Return);
    ZC_IF_SOME(returnValue, mirFunction.blocks[0].terminator.returnValue().value) {
      ZC_EXPECT(returnValue.place().local() == mirFunction.locals[2].id);
    }
  }
}

ZC_TEST("HIR pipeline lowers all six relational operators in return position") {
  struct Case final {
    zc::StringPtr source;
    checker::PrimitiveOperation operation;
    mir::MirComparisonOperator mirOperator;
  };
  const Case cases[] = {{"fun f(a: i32, b: i32) -> bool { return a == b; }"_zc,
                         checker::PrimitiveOperation::Eq, mir::MirComparisonOperator::Eq},
                        {"fun f(a: i32, b: i32) -> bool { return a != b; }"_zc,
                         checker::PrimitiveOperation::Ne, mir::MirComparisonOperator::Ne},
                        {"fun f(a: i32, b: i32) -> bool { return a < b; }"_zc,
                         checker::PrimitiveOperation::Lt, mir::MirComparisonOperator::Lt},
                        {"fun f(a: i32, b: i32) -> bool { return a <= b; }"_zc,
                         checker::PrimitiveOperation::Le, mir::MirComparisonOperator::Le},
                        {"fun f(a: i32, b: i32) -> bool { return a > b; }"_zc,
                         checker::PrimitiveOperation::Gt, mir::MirComparisonOperator::Gt},
                        {"fun f(a: i32, b: i32) -> bool { return a >= b; }"_zc,
                         checker::PrimitiveOperation::Ge, mir::MirComparisonOperator::Ge}};
  for (const auto& testCase : cases) {
    HirPipelineFixture fixture(testCase.source);
    const auto& module = fixture.hirModule();
    ZC_REQUIRE(module.primitiveBinaryOperations().size() == 1);
    ZC_REQUIRE(module.conditionals().size() == 0);
    ZC_EXPECT(module.primitiveBinaryOperations()[0].operation == testCase.operation);
    const auto& function = module.functions()[0];
    const auto builtMir = fixture.compilerSession().getOwnershipCheckedMirModules();
    ZC_REQUIRE(builtMir.size() == 1);
    zc::Maybe<const mir::MirFunction&> lowered;
    for (const auto& mirFunction : builtMir[0].builtMir().functions()) {
      if (mirFunction.owner == function.definition) lowered = mirFunction;
    }
    ZC_REQUIRE(lowered != zc::none);
    ZC_IF_SOME(mirFunction, lowered) {
      const auto& compareAssign = mirFunction.blocks[0].statements[1].assignmentValue();
      ZC_REQUIRE(compareAssign.value.kind() == mir::MirRvalueKind::Comparison);
      ZC_EXPECT(compareAssign.value.comparisonValue().op == testCase.mirOperator);
    }
  }
}

ZC_TEST("HIR pipeline lowers a return-position arithmetic operation") {
  HirPipelineFixture fixture("fun add(a: i32, b: i32) -> i32 { return a + b; }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 1);
  ZC_REQUIRE(module.returns().size() == 1);
  // The arithmetic result is returned directly: one primitive-binary node plus
  // its two operand parameter references; no conditional.
  ZC_REQUIRE(module.conditionals().size() == 0);
  ZC_REQUIRE(module.primitiveBinaryOperations().size() == 1);
  ZC_REQUIRE(module.parameterReferences().size() == 2);
  ZC_REQUIRE(module.expressions().size() == 0);
  const auto& function = module.functions()[0];
  const auto& arithmetic = module.primitiveBinaryOperations()[0];
  const auto& returnStatement = module.returns()[0];
  ZC_EXPECT(returnStatement.value == arithmetic.node);
  ZC_EXPECT(arithmetic.operation == checker::PrimitiveOperation::Add);
  // The result type is the operand type, not bool.
  ZC_EXPECT(arithmetic.operandType == function.parameters[0].type);
  ZC_EXPECT(arithmetic.type == function.resultType);
  ZC_EXPECT(arithmetic.type == arithmetic.operandType);

  // The body lowers to a single block: StorageLive(result), Assign(result =
  // a + b), then Return(placeUse(result)). The assignment carries an Arithmetic
  // rvalue whose result type is the operand type.
  const auto builtMir = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(builtMir.size() == 1);
  zc::Maybe<const mir::MirFunction&> add;
  for (const auto& mirFunction : builtMir[0].builtMir().functions()) {
    if (mirFunction.owner == function.definition) add = mirFunction;
  }
  ZC_REQUIRE(add != zc::none);
  ZC_IF_SOME(mirFunction, add) {
    ZC_REQUIRE(mirFunction.locals.size() == 3);
    ZC_REQUIRE(mirFunction.blocks.size() == 1);
    ZC_REQUIRE(mirFunction.blocks[0].statements.size() == 2);
    ZC_EXPECT(mirFunction.blocks[0].statements[0].kind() == mir::MirStatementKind::StorageLive);
    ZC_EXPECT(mirFunction.blocks[0].statements[1].kind() == mir::MirStatementKind::Assign);
    const auto& addAssign = mirFunction.blocks[0].statements[1].assignmentValue();
    ZC_REQUIRE(addAssign.value.kind() == mir::MirRvalueKind::Arithmetic);
    const auto& arithmeticValue = addAssign.value.arithmeticValue();
    ZC_EXPECT(arithmeticValue.op == mir::MirArithmeticOperator::Add);
    ZC_EXPECT(arithmeticValue.left.place().local() == mirFunction.locals[0].id);
    ZC_EXPECT(arithmeticValue.right.place().local() == mirFunction.locals[1].id);
    // The result type equals the operand type (i32), never bool.
    ZC_EXPECT(arithmeticValue.resultType == function.resultType);
    ZC_EXPECT(addAssign.destination.local() == mirFunction.locals[2].id);
    ZC_REQUIRE(mirFunction.blocks[0].terminator.kind() == mir::MirTerminatorKind::Return);
    ZC_IF_SOME(returnValue, mirFunction.blocks[0].terminator.returnValue().value) {
      ZC_EXPECT(returnValue.place().local() == mirFunction.locals[2].id);
    }
  }
}

ZC_TEST("HIR pipeline lowers all twelve arithmetic and bitwise operators in return position") {
  struct Case final {
    zc::StringPtr source;
    checker::PrimitiveOperation operation;
    mir::MirArithmeticOperator mirOperator;
  };
  const Case cases[] = {{"fun f(a: i32, b: i32) -> i32 { return a + b; }"_zc,
                         checker::PrimitiveOperation::Add, mir::MirArithmeticOperator::Add},
                        {"fun f(a: i32, b: i32) -> i32 { return a - b; }"_zc,
                         checker::PrimitiveOperation::Sub, mir::MirArithmeticOperator::Sub},
                        {"fun f(a: i32, b: i32) -> i32 { return a * b; }"_zc,
                         checker::PrimitiveOperation::Mul, mir::MirArithmeticOperator::Mul},
                        {"fun f(a: i32, b: i32) -> i32 { return a / b; }"_zc,
                         checker::PrimitiveOperation::Div, mir::MirArithmeticOperator::Div},
                        {"fun f(a: i32, b: i32) -> i32 { return a % b; }"_zc,
                         checker::PrimitiveOperation::Rem, mir::MirArithmeticOperator::Rem},
                        {"fun f(a: i32, b: i32) -> i32 { return a ** b; }"_zc,
                         checker::PrimitiveOperation::Pow, mir::MirArithmeticOperator::Pow},
                        {"fun f(a: i32, b: i32) -> i32 { return a << b; }"_zc,
                         checker::PrimitiveOperation::Shl, mir::MirArithmeticOperator::Shl},
                        {"fun f(a: i32, b: i32) -> i32 { return a >> b; }"_zc,
                         checker::PrimitiveOperation::Shr, mir::MirArithmeticOperator::Shr},
                        {"fun f(a: i32, b: i32) -> i32 { return a >>> b; }"_zc,
                         checker::PrimitiveOperation::UShr, mir::MirArithmeticOperator::UShr},
                        {"fun f(a: i32, b: i32) -> i32 { return a & b; }"_zc,
                         checker::PrimitiveOperation::BitAnd, mir::MirArithmeticOperator::BitAnd},
                        {"fun f(a: i32, b: i32) -> i32 { return a | b; }"_zc,
                         checker::PrimitiveOperation::BitOr, mir::MirArithmeticOperator::BitOr},
                        {"fun f(a: i32, b: i32) -> i32 { return a ^ b; }"_zc,
                         checker::PrimitiveOperation::BitXor, mir::MirArithmeticOperator::BitXor}};
  for (const auto& testCase : cases) {
    HirPipelineFixture fixture(testCase.source);
    const auto& module = fixture.hirModule();
    ZC_REQUIRE(module.primitiveBinaryOperations().size() == 1);
    ZC_REQUIRE(module.conditionals().size() == 0);
    ZC_EXPECT(module.primitiveBinaryOperations()[0].operation == testCase.operation);
    const auto& function = module.functions()[0];
    const auto builtMir = fixture.compilerSession().getOwnershipCheckedMirModules();
    ZC_REQUIRE(builtMir.size() == 1);
    zc::Maybe<const mir::MirFunction&> lowered;
    for (const auto& mirFunction : builtMir[0].builtMir().functions()) {
      if (mirFunction.owner == function.definition) lowered = mirFunction;
    }
    ZC_REQUIRE(lowered != zc::none);
    ZC_IF_SOME(mirFunction, lowered) {
      const auto& addAssign = mirFunction.blocks[0].statements[1].assignmentValue();
      ZC_REQUIRE(addAssign.value.kind() == mir::MirRvalueKind::Arithmetic);
      ZC_EXPECT(addAssign.value.arithmeticValue().op == testCase.mirOperator);
    }
  }
}

ZC_TEST("Built MIR arithmetic rvalue byte oracle is stable and mutation sensitive") {
  HirPipelineFixture fixture("fun add(a: i32, b: i32) -> i32 { return a + b; }"_zc);
  const auto& module = fixture.hirModule();
  ZC_REQUIRE(module.functions().size() == 1);
  const auto& function = module.functions()[0];
  const auto builtMir = fixture.compilerSession().getOwnershipCheckedMirModules();
  ZC_REQUIRE(builtMir.size() == 1);
  zc::Maybe<const mir::VerifiedBuiltMir&> verified = builtMir[0].builtMir();

  // Pull the live canonical function record for the `a + b` function; no bytes
  // are fabricated. The Arithmetic rvalue encodes tag 0x04, operator 0x01 (Add),
  // then two Copy operands (0x01) and the operand result type.
  zc::Maybe<zc::Array<uint8_t>> arithmeticRecord;
  ZC_IF_SOME(mir, verified) {
    ZC_REQUIRE(mir.functions().size() == mir.canonicalFunctionRecords().size());
    for (size_t index = 0; index < mir.functions().size(); ++index) {
      if (mir.functions()[index].owner != function.definition) continue;
      arithmeticRecord = zc::heapArray(mir.canonicalFunctionRecords()[index].asPtr());
    }
  }
  ZC_REQUIRE(arithmeticRecord != zc::none);

  auto framedDigest = [](zc::ArrayPtr<const uint8_t> record) -> zc::String {
    uint8_t moduleKey[32];
    for (auto& value : moduleKey) value = uint8_t{0};
    ZC_IF_SOME(fingerprint, identity::Sha256Digest::fromBytes(zc::arrayPtr(moduleKey))) {
      const uint8_t moduleId[] = {0xa1};
      zc::Vector<zc::Array<uint8_t>> records;
      records.add(zc::heapArray(record));
      auto encoded =
          mir::MirRevisionCodec::encodeBuiltFramed(fingerprint, zc::arrayPtr(moduleId), fingerprint,
                                                   fingerprint, fingerprint, records.asPtr());
      ZC_REQUIRE(encoded != zc::none);
      auto digest = identity::sha256(ZC_REQUIRE_NONNULL(encoded).asPtr());
      ZC_REQUIRE(digest != zc::none);
      return zc::encodeHex(ZC_REQUIRE_NONNULL(digest).bytes());
    }
    ZC_FAIL_REQUIRE("invalid digest fixture");
  };

  zc::Array<uint8_t> record = zc::mv(ZC_REQUIRE_NONNULL(arithmeticRecord));
  zc::Maybe<size_t> arithmeticOffset;
  for (size_t i = 0; i + 2 < record.size(); ++i) {
    if (record[i] == 0x04 && record[i + 1] == 0x01 && record[i + 2] == 0x01) {
      ZC_REQUIRE(arithmeticOffset == zc::none);
      arithmeticOffset = i;
    }
  }
  ZC_REQUIRE(arithmeticOffset != zc::none);
  const size_t arithmeticKind = ZC_REQUIRE_NONNULL(arithmeticOffset);
  const size_t operatorByte = arithmeticKind + 1;
  const size_t leftOperandKind = arithmeticKind + 2;
  ZC_REQUIRE(record[arithmeticKind] == 0x04);
  // The operator byte is the Add tag emitted by the live encoder.
  ZC_REQUIRE(record[operatorByte] == 0x01);
  ZC_REQUIRE(record[leftOperandKind] == 0x01);

  const auto baseline = framedDigest(record.asPtr());
  // Mutating the arithmetic operator byte to any other operator changes the
  // digest, and re-framing the unmutated record reproduces it exactly.
  {
    auto mutated = zc::heapArray(record.asPtr());
    mutated[operatorByte] = 0x02;
    ZC_EXPECT(framedDigest(mutated.asPtr()) != baseline);
  }
  // Mutating the rvalue kind byte (Arithmetic -> Comparison) changes the digest.
  {
    auto mutated = zc::heapArray(record.asPtr());
    mutated[arithmeticKind] = 0x03;
    ZC_EXPECT(framedDigest(mutated.asPtr()) != baseline);
  }
  ZC_EXPECT(framedDigest(record.asPtr()) == baseline);
}

}  // namespace
}  // namespace zomlang::compiler::hir
