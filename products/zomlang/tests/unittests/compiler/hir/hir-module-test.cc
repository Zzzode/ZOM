// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/hir/hir-module.h"

#include "zc/core/time.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/driver/compiler-session.h"
#include "zomlang/compiler/driver/package/manifest-parser.h"
#include "zomlang/compiler/driver/package/source-record.h"

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
        scalar<identity::TargetComponentName>("zom-v1"_zc), 64, identity::Endianness::Little,
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
      "x86_64-zom-none"_zc, "e-p:64:64"_zc, "generic"_zc, zc::mv(targetFeatures), "zom-v1"_zc,
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
    const auto roots = session.getFinalizedCompilationRoots();
    ZC_REQUIRE(roots.size() == 1);
    ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
    ZC_REQUIRE(session.parseSources());
    ZC_REQUIRE(session.bindSources());
    ZC_REQUIRE(session.checkSources());
    ZC_REQUIRE(!session.getDiagnosticEngine().hasErrors());
    ZC_REQUIRE(session.getVerifiedBoundModules().size() == 1);
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

ZC_TEST("HIR pipeline publishes an exact empty module") {
  HirPipelineFixture fixture(""_zc);
  const auto& module = fixture.hirModule();
  const auto& bound = fixture.compilerSession().getVerifiedBoundModules()[0];
  const auto& interface = fixture.compilerSession().getVerifiedModuleInterfaces()[0];
  ZC_EXPECT(module.semanticContext() == bound.semanticContext());
  ZC_EXPECT(module.contextFingerprint().digest() == bound.semanticFingerprint().digest());
  ZC_EXPECT(module.package() == bound.package());
  ZC_EXPECT(module.crate() == bound.crate());
  ZC_EXPECT(module.module() == bound.module());
  ZC_EXPECT(module.sourceContentDigest() == bound.parsedModule().contentDigest());
  ZC_EXPECT(module.parsedModuleReceiptDigest() == bound.parsedModule().receipt().digest());
  ZC_EXPECT(module.ownInterface().revision == interface.revision().digest());
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
    const auto evidence = value.lookup(module.borrowEvidenceLease());
    ZC_REQUIRE(evidence.isResolved());
    ZC_EXPECT(evidence.evidence().revision().digest() == module.borrowEvidenceRevision().digest());
  }
  ZC_EXPECT(module.visibleImportedInterfaces().size() == 0);
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
      ZC_EXPECT(left.startsWith("zom.hir.v0\n"_zc));
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
  ZC_EXPECT(fixture.hirModule().visibleImportedInterfaces().size() == 0);
}

}  // namespace
}  // namespace zomlang::compiler::hir
