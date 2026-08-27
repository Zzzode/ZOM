// Copyright (c) 2025 Zode.Z. All rights reserved
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
// See the License for the specific language governing permissions and limitations under
// the License.

#include "compiler/driver/session/compiler-session.h"

#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zc/core/time.h"
#include "zc/ztest/test.h"
#include "compiler/basic/compiler-opts.h"
#include "compiler/diagnostics/consumer/diagnostic-consumer.h"
#include "compiler/diagnostics/core/diagnostic-engine.h"
#include "compiler/driver/core/marker-authority.h"
#include "compiler/driver/package/manifest-parser.h"
#include "compiler/driver/package/source-record.h"
#include "compiler/source/manager.h"
#include "tests/unittests/compiler/driver/core/core-library-test-fixture.h"

namespace zomlang {
namespace compiler {
namespace driver {

namespace {

class UnexpectedBuildScriptPlanExecutor final : public package::BuildScriptPlanExecutor {
public:
  package::BuildScriptExecutionResult execute(
      const package::BuildScriptPlanNode&, const VerifiedPreparatoryCrateGraph&,
      zc::ArrayPtr<const package::VerifiedBuildScriptResult>) override {
    return package::BuildScriptIssue::ExecutionFailed;
  }
};

zc::Own<CompilerSession> makeSession(const basic::LangOptions& langOpts,
                                     const basic::CompilerOptions& compilerOpts) {
  identity::SemanticContextFactory contextFactory;
  return zc::heap<CompilerSession>(contextFactory, langOpts, compilerOpts);
}

struct SessionDiagnostics final {
  zc::Vector<diagnostics::DiagID> ids;
  zc::Vector<diagnostics::DiagID> childIds;
};

class SessionDiagnosticConsumer final : public diagnostics::DiagnosticConsumer {
public:
  explicit SessionDiagnosticConsumer(SessionDiagnostics& capture) noexcept : capture(capture) {}

  void handleDiagnostic(const source::SourceManager&,
                        const diagnostics::Diagnostic& diagnostic) override {
    capture.ids.add(diagnostic.getId());
    for (const auto& child : diagnostic.getChildDiagnostics()) {
      capture.childIds.add(child->getId());
    }
  }

private:
  SessionDiagnostics& capture;
};

template <typename Scalar>
Scalar sessionScalar(zc::StringPtr text) {
  auto result = Scalar::fromCanonical(text);
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::SortedFeatureSet sessionFeatures() {
  zc::Vector<identity::FeatureName> values;
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::CanonicalPackageSource sessionPackageSource() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  return identity::CanonicalPackageSource::localPath(
      identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments)));
}

identity::PackageBaseKey sessionPackageBase() {
  return identity::PackageBaseKey::from(sessionPackageSource(),
                                        sessionScalar<identity::PackageName>("app"_zc),
                                        sessionScalar<identity::ResolvedVersion>("1.0.0"_zc));
}

identity::PackageKey sessionPackageKey() {
  return identity::PackageKey::from(
      sessionPackageSource(), sessionScalar<identity::PackageName>("app"_zc),
      sessionScalar<identity::ResolvedVersion>("1.0.0"_zc), sessionFeatures());
}

identity::CanonicalTargetSpecificationKey sessionTargetProjection() {
  zc::Vector<identity::TargetFeatureName> features;
  auto sorted = identity::SortedTargetFeatureSet::from(zc::mv(features));
  ZC_REQUIRE(sorted != zc::none);
  auto result = identity::CanonicalTargetSpecificationKey::from(
      sessionScalar<identity::TargetComponentName>("x86_64"_zc),
      sessionScalar<identity::TargetComponentName>("zom"_zc),
      sessionScalar<identity::TargetComponentName>("none"_zc),
      sessionScalar<identity::TargetComponentName>("unknown"_zc),
      sessionScalar<identity::TargetComponentName>("zom"_zc), 64, identity::Endianness::Little,
      zc::mv(ZC_REQUIRE_NONNULL(sorted)));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

package::RegisteredTargetProfileName sessionProfileName() {
  auto result = package::RegisteredTargetProfileName::from("host"_zc);
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

ir::TargetRegistrySnapshot sessionTargetRegistry() {
  zc::Vector<ir::CanonicalTargetFeature> targetFeatures;
  auto specification = ir::CanonicalTargetSpec::from(
      "x86_64-zom-none"_zc, "e-p:64:64"_zc, "generic"_zc, zc::mv(targetFeatures), "zom"_zc,
      ir::BackendPanicStrategy::Unwind, ir::ObjectFormat::Elf);
  ZC_REQUIRE(specification != zc::none);
  zc::Vector<identity::TargetFeatureName> semanticFeatures;
  zc::Vector<ir::CanonicalTargetSpec> specifications;
  specifications.add(zc::mv(ZC_REQUIRE_NONNULL(specification)));
  auto profile =
      ir::RegisteredTargetProfileRecord::from(sessionProfileName(), sessionTargetProjection(),
                                              zc::mv(semanticFeatures), zc::mv(specifications));
  ZC_REQUIRE(profile != zc::none);
  zc::Vector<ir::RegisteredTargetProfileRecord> profiles;
  profiles.add(zc::mv(ZC_REQUIRE_NONNULL(profile)));
  auto registry = ir::TargetRegistrySnapshot::from(sessionProfileName(), zc::mv(profiles));
  return zc::mv(ZC_REQUIRE_NONNULL(registry));
}

package::RegisteredTargetSelection sessionSelection(const ir::TargetRegistrySnapshot& registry) {
  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  auto selected =
      ZC_REQUIRE_NONNULL(service).select(zc::none, package::PackagePanicStrategy::Unwind);
  return zc::mv(ZC_REQUIRE_NONNULL(selected));
}

ir::VerifiedTargetSelection sessionVerifiedSelection(const ir::TargetRegistrySnapshot& registry) {
  auto verified = registry.verify(sessionSelection(registry));
  ZC_REQUIRE(verified.is<ir::VerifiedTargetSelection>());
  return zc::mv(verified.get<ir::VerifiedTargetSelection>());
}

identity::CanonicalRelativePath sessionRootPath() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(sessionScalar<identity::CanonicalPathSegment>("src"_zc));
  segments.add(sessionScalar<identity::CanonicalPathSegment>("main.zom"_zc));
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

package::VerifiedPackageCompilationRequest sessionRequest(
    const ir::TargetRegistrySnapshot& registry) {
  zc::Vector<package::VerifiedCompilationRoot> roots;
  roots.add(package::VerifiedCompilationRoot::from(
      sessionPackageKey(), identity::CrateTargetKind::Binary,
      sessionScalar<identity::TargetName>("app"_zc), 2026, false, sessionRootPath()));
  auto request = package::VerifiedPackageCompilationRequest::from(
      zc::mv(roots), sessionSelection(registry), sessionSelection(registry),
      package::SelectedLanguageOptions{}, package::PackageLockMode::PreferLocked);
  return zc::mv(ZC_REQUIRE_NONNULL(request));
}

class SessionFreshDirectory final : public package::FreshSourceDirectory {
public:
  SessionFreshDirectory() : rootValue(zc::newInMemoryDirectory(zc::nullClock())) {}
  ~SessionFreshDirectory() noexcept override = default;
  const zc::Directory& root() const override { return *rootValue; }
  zc::Maybe<package::MaterializationIssue> finish() override { return zc::none; }

private:
  zc::Own<zc::Directory> rootValue;
};

class SessionFreshDirectoryFactory final : public package::FreshSourceDirectoryFactory {
public:
  package::FreshSourceDirectoryResult create() override {
    zc::Own<package::FreshSourceDirectory> result = zc::heap<SessionFreshDirectory>();
    return zc::mv(result);
  }
};

package::DigestVerifiedSourceSnapshot sessionSnapshot(zc::StringPtr mainSource,
                                                      zc::StringPtr childSource = {}) {
  auto directory = zc::newInMemoryDirectory(zc::nullClock());
  directory
      ->openFile(zc::Path({"src"_zc, "main.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll(mainSource);
  if (childSource.size() != 0) {
    directory->openFile(zc::Path({"src"_zc, "child.zom"_zc}), zc::WriteMode::CREATE)
        ->writeAll(childSource);
  }
  SessionFreshDirectoryFactory factory;
  package::SourceDirectoryMaterializer materializer;
  auto result = materializer.materialize(*directory, factory);
  ZC_REQUIRE(result.is<package::DigestVerifiedSourceSnapshot>());
  return zc::mv(result.get<package::DigestVerifiedSourceSnapshot>());
}

package::ResolutionOutput sessionResolution(zc::MemoryResource& resource) {
  auto verifiedSource = sessionSnapshot("let main = 0;"_zc);
  package::ManifestParser parser;
  zc::Vector<identity::CanonicalRelativePath> files;
  auto inventory = package::PackageSourceInventory::from(zc::mv(files));
  ZC_REQUIRE(inventory != zc::none);
  zc::Vector<identity::CanonicalPathSegment> manifestSegments;
  manifestSegments.add(sessionScalar<identity::CanonicalPathSegment>("Zom.toml"_zc));
  auto manifest = parser.parseWorkspaceManifest(
      identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(manifestSegments)),
      "[package]\nname = \"app\"\nversion = \"1.0.0\"\nedition = \"2026\"\n"_zc,
      ZC_REQUIRE_NONNULL(inventory));
  ZC_REQUIRE(manifest.is<package::NormalizedManifest>());
  auto record = package::LocalPackageRecord::from(
      sessionPackageBase(), zc::mv(manifest.get<package::NormalizedManifest>()), verifiedSource);
  ZC_REQUIRE(record != zc::none);
  zc::Vector<package::ResolverRelease> releases;
  releases.add(package::ResolverRelease::fromLocal(ZC_REQUIRE_NONNULL(record)));
  zc::Vector<package::ResolverRoot> roots;
  roots.add(package::ResolverRoot::from(sessionPackageBase(), sessionFeatures(), false, false));
  auto resolution = package::PackageResolver::resolve(resource, roots, releases);
  ZC_REQUIRE(resolution.is<package::ResolutionOutput>());
  return zc::mv(resolution.get<package::ResolutionOutput>());
}

class RetainedPackageSession final {
public:
  RetainedPackageSession()
      : languageOptions(zc::heap<basic::LangOptions>()),
        compilerOptions(zc::heap<basic::CompilerOptions>()),
        sessionValue(createSession(*languageOptions, *compilerOptions)) {}
  RetainedPackageSession(RetainedPackageSession&&) noexcept = default;
  RetainedPackageSession& operator=(RetainedPackageSession&&) noexcept = default;
  ZC_DISALLOW_COPY(RetainedPackageSession);

  zc::Own<CompilerSession>& operator->() { return sessionValue; }

private:
  static zc::Own<CompilerSession> createSession(const basic::LangOptions& languageOptions,
                                                const basic::CompilerOptions& compilerOptions) {
    identity::SemanticContextFactory contextFactory;
    return zc::heap<CompilerSession>(contextFactory, languageOptions, compilerOptions);
  }

  zc::Own<basic::LangOptions> languageOptions;
  zc::Own<basic::CompilerOptions> compilerOptions;
  zc::Own<CompilerSession> sessionValue;
};

RetainedPackageSession packageSession(zc::StringPtr mainSource, zc::StringPtr childSource = {},
                                      bool installCore = true) {
  RetainedPackageSession session;
  auto registry = sessionTargetRegistry();
  zc::Vector<package::ResolvedPackageSourceSnapshot> snapshots;
  snapshots.add(package::ResolvedPackageSourceSnapshot::from(
      sessionPackageBase(), sessionSnapshot(mainSource, childSource)));
  auto input = VerifiedPackageSessionInput::from(
      sessionRequest(registry), sessionVerifiedSelection(registry),
      sessionVerifiedSelection(registry),
      sessionResolution(session->getPackageResolutionMemoryResource()), zc::mv(snapshots));
  ZC_REQUIRE(input != zc::none);
  ZC_REQUIRE(session->installVerifiedPackageInput(zc::mv(ZC_REQUIRE_NONNULL(input))));
  if (installCore) {
    auto distribution = core_library_test::admittedCoreDistribution();
    ZC_REQUIRE(session->installVerifiedCoreDistribution(distribution));
  }
  const auto roots = session->getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session->addVerifiedPackageRoot(roots[0]) != zc::none);
  return session;
}

size_t diagnosticCount(const SessionDiagnostics& diagnostics, diagnostics::DiagID id) {
  size_t count = 0;
  for (const auto candidate : diagnostics.ids) {
    if (candidate == id) { ++count; }
  }
  return count;
}

size_t childDiagnosticCount(const SessionDiagnostics& diagnostics, diagnostics::DiagID id) {
  size_t count = 0;
  for (const auto candidate : diagnostics.childIds) {
    if (candidate == id) { ++count; }
  }
  return count;
}

}  // namespace

ZC_TEST("CompilerSessionTest.BasicInitialization") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto session = makeSession(langOpts, compilerOpts);
  ZC_EXPECT(session.get() != nullptr);
}

ZC_TEST("CompilerSessionTest.RejectsBuildPlanBeforePackageHandoff") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto session = makeSession(langOpts, compilerOpts);
  UnexpectedBuildScriptPlanExecutor executor;
  ZC_EXPECT(session->executeBuildScripts(executor) ==
            package::BuildScriptIssue::BuildResultIntegrityViolation);
  ZC_EXPECT(session->getBuildScriptPlan() == zc::none);
  ZC_EXPECT(session->getBuildScriptResults() == zc::none);
}

ZC_TEST("CompilerSessionTest.OwnsDistinctSemanticContextsAndTypeStores") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  identity::SemanticContextFactory contextFactory;
  auto first = zc::heap<CompilerSession>(contextFactory, langOpts, compilerOpts);
  auto second = zc::heap<CompilerSession>(contextFactory, langOpts, compilerOpts);
  ZC_EXPECT(first->getSemanticContextBrand().isValid());
  ZC_EXPECT(second->getSemanticContextBrand().isValid());
  ZC_EXPECT(first->getSemanticContextBrand() != second->getSemanticContextBrand());
  auto firstTypeStore = first->getSemanticTypeStore();
  auto secondTypeStore = second->getSemanticTypeStore();
  ZC_EXPECT(firstTypeStore != zc::none);
  ZC_EXPECT(secondTypeStore != zc::none);
  ZC_IF_SOME(firstStore, firstTypeStore) {
    ZC_IF_SOME(secondStore, secondTypeStore) {
      ZC_EXPECT(&firstStore != &secondStore);
      ZC_EXPECT(firstStore.context() == first->getSemanticContextBrand());
      ZC_EXPECT(secondStore.context() == second->getSemanticContextBrand());
    }
  }
}

ZC_TEST("CompilerSessionTest.BrandExhaustionUsesRegisteredDiagnostic") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  identity::SemanticContextFactory contextFactory(identity::SemanticContextIssueBudget{0, 1});
  auto session = zc::heap<CompilerSession>(contextFactory, langOpts, compilerOpts);
  ZC_EXPECT(!session->getSemanticContextBrand().isValid());
  ZC_EXPECT(session->getSemanticTypeStore() == zc::none);
  ZC_EXPECT(session->getDiagnosticEngine().hasErrors());
  ZC_EXPECT(!session->parseSources());
}

ZC_TEST("CompilerSessionTest.GetDiagnosticEngine") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto session = makeSession(langOpts, compilerOpts);

  const auto& diagnosticEngine = session->getDiagnosticEngine();
  ZC_EXPECT(&diagnosticEngine != nullptr);
}

ZC_TEST("CompilerSessionTest.HasVerifiedParsedSyntaxInitiallyFalse") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto session = makeSession(langOpts, compilerOpts);

  ZC_EXPECT(!session->hasVerifiedParsedSyntax());
  ZC_EXPECT(session->materializeParsedModules() == zc::none);
}

ZC_TEST("CompilerSessionTest.GetCheckerInvariantFailuresEmpty") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto session = makeSession(langOpts, compilerOpts);

  ZC_EXPECT(session->getCheckerInvariantFailures().size() == 0);
}

ZC_TEST("CompilerSessionTest.GetSourceManager") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto session = makeSession(langOpts, compilerOpts);

  const auto& sourceManager = session->getSourceManager();
  ZC_EXPECT(&sourceManager != nullptr);
}

ZC_TEST("CompilerSessionTest.ParseSourcesEmpty") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto session = makeSession(langOpts, compilerOpts);

  bool result = session->parseSources();
  ZC_EXPECT(result);
}

ZC_TEST("CompilerSessionTest.PublishesOrdinaryPackageModuleGraph") {
  auto session = packageSession("let main = 0;\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_REQUIRE(session->parseSources());
  ZC_EXPECT(captured.ids.empty());
  auto graphLease = session->materializeModuleGraph();
  ZC_REQUIRE(graphLease != zc::none);
  const auto& graph = ZC_REQUIRE_NONNULL(graphLease).capability();
  ZC_EXPECT(graph.modules().size() == 4);
  ZC_EXPECT(graph.requestEdges().size() == 2);
}

ZC_TEST("CompilerSessionTest.PublishesCanonicalParseRejectionAtomically") {
  auto session = packageSession("let main = ;\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_EXPECT(!session->parseSources());
  ZC_EXPECT(!session->hasVerifiedParsedSyntax());
  ZC_EXPECT(captured.ids.size() == 1);
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::ExpressionExpected) == 1);
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::ModuleGraphInvariant) == 0);
}

ZC_TEST("CompilerSessionTest.PublishesRetainedMissingLookupDiagnosticDuringBinding") {
  auto session = packageSession("missing_value;\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_REQUIRE(session->parseSources());
  ZC_EXPECT(!session->bindSources());
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::UndefinedIdentifier) == 1);
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::CheckerMissingRequiredFact) == 0);
}

ZC_TEST("CompilerSessionTest.RejectsPackageParsingWithoutCoreDistribution") {
  auto session = packageSession("let main = 0;\n"_zc, {}, false);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_EXPECT(!session->parseSources());
  ZC_EXPECT(!session->hasVerifiedParsedSyntax());
  ZC_EXPECT(session->materializeModuleGraph() == zc::none);
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::ModuleGraphInvariant) == 1);
}

ZC_TEST("CompilerSessionTest.PublishesSourceBackedCoreModulesInCompleteSemanticGraph") {
  auto session = packageSession("let main = 0;\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_REQUIRE(session->parseSources());
  ZC_EXPECT(captured.ids.empty());
  auto parsedModules = session->materializeParsedModules();
  ZC_REQUIRE(parsedModules != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(parsedModules).size() == 4);
  size_t userModules = 0;
  size_t toolchainModules = 0;
  for (const auto& parsed : ZC_ASSERT_NONNULL(parsedModules)) {
    switch (parsed.parsedModule().source().crate().unit().kind()) {
      case identity::CompilationUnitKind::UserPackage:
        ++userModules;
        break;
      case identity::CompilationUnitKind::Toolchain:
        ++toolchainModules;
        break;
    }
  }
  ZC_EXPECT(userModules == 1);
  ZC_EXPECT(toolchainModules == 3);

  auto graphLease = session->materializeModuleGraph();
  ZC_REQUIRE(graphLease != zc::none);
  const auto& graph = ZC_REQUIRE_NONNULL(graphLease).capability();
  ZC_EXPECT(graph.modules().size() == 4);
  ZC_EXPECT(graph.requestEdges().size() == 2);
  auto authority = session->materializeCheckerIdentityAuthority();
  ZC_REQUIRE(authority != zc::none);
  const auto& materialized = ZC_REQUIRE_NONNULL(authority).graphLease().capability();
  ZC_EXPECT(materialized.units().size() == 2);
  ZC_EXPECT(materialized.crates().size() == 2);
  ZC_EXPECT(materialized.sources().size() == 4);
  ZC_EXPECT(materialized.modules().size() == 4);

  zc::Maybe<identity::CrateKey> coreCrate;
  zc::Maybe<identity::CrateKey> userCrate;
  for (const auto& module : graph.modules()) {
    if (module.key().crate().unit().kind() == identity::CompilationUnitKind::UserPackage) {
      if (userCrate == zc::none) userCrate = module.key().crate().clone();
      continue;
    }
    if (coreCrate != zc::none) {
      ZC_EXPECT(ZC_REQUIRE_NONNULL(coreCrate).encode().asPtr() ==
                module.key().crate().encode().asPtr());
    } else {
      coreCrate = module.key().crate().clone();
    }
  }
  ZC_REQUIRE(userCrate != zc::none);
  ZC_EXPECT(session->materializeCoreLibrary(ZC_REQUIRE_NONNULL(userCrate)) == zc::none);
  ZC_REQUIRE(coreCrate != zc::none);
  auto coreLibrary = session->materializeCoreLibrary(ZC_REQUIRE_NONNULL(coreCrate));
  ZC_REQUIRE(coreLibrary != zc::none);
  const auto& library = ZC_REQUIRE_NONNULL(coreLibrary);
  ZC_EXPECT(library.modules().size() == 3);
  const auto& authorityLease = library.authorityLease();
  ZC_EXPECT(authorityLease.revision() == library.revision());
  ZC_EXPECT(authorityLease.arenaRevision() == library.revision());
  ZC_EXPECT(authorityLease.stableWitness().size() != 0);
  ZC_EXPECT(authorityLease.retainedDependencyCount() >= 2);
  auto retainedAuthority = authorityLease.retain();
  ZC_EXPECT(retainedAuthority.revision() == authorityLease.revision());
  ZC_EXPECT(retainedAuthority.stableWitness() == authorityLease.stableWitness());
  ZC_EXPECT(retainedAuthority.capability().encodeCanonical() ==
            authorityLease.capability().encodeCanonical());
  ZC_EXPECT(authorityLease.capability().copy() != authorityLease.capability().linear());
  auto markerConfiguration = core::checkerConfig(authorityLease.capability().policies());
  ZC_REQUIRE(markerConfiguration != zc::none);
  ZC_REQUIRE(ZC_REQUIRE_NONNULL(markerConfiguration).entries().size() == 1);
  const auto& copyPolicy = ZC_REQUIRE_NONNULL(markerConfiguration).entries()[0];
  ZC_EXPECT(copyPolicy.referenceRequirements.size() == 1);
  ZC_EXPECT(copyPolicy.referenceRequirements[0].requiredMarker == zc::none);
  ZC_EXPECT(copyPolicy.rawPointerMutabilities.size() == 2);
  ZC_EXPECT(copyPolicy.rawPointerMutabilities[0] == type::semantic::Mutability::Const);
  ZC_EXPECT(copyPolicy.rawPointerMutabilities[1] == type::semantic::Mutability::Mutable);
  bool foundRoot = false;
  bool foundMarker = false;
  bool foundPrelude = false;
  for (const auto& module : library.modules()) {
    const auto& interfaceLease = module.interfaceLease();
    ZC_EXPECT(interfaceLease.revision() == library.revision());
    ZC_EXPECT(interfaceLease.arenaRevision() == library.revision());
    ZC_EXPECT(interfaceLease.stableWitness().size() != 0);
    ZC_EXPECT(interfaceLease.retainedDependencyCount() >= 2);
    auto retainedInterface = interfaceLease.retain();
    ZC_EXPECT(retainedInterface.revision() == interfaceLease.revision());
    ZC_EXPECT(retainedInterface.stableWitness() == interfaceLease.stableWitness());
    ZC_EXPECT(retainedInterface.capability().encodeCanonical() ==
              interfaceLease.capability().encodeCanonical());
    const auto& record = interfaceLease.capability().record();
    if (module.module().encode().asPtr() == library.prelude().encode().asPtr()) {
      ZC_EXPECT(record.lookupDefinitions().size() == 2);
      ZC_EXPECT(record.supportDefinitions().size() == 0);
      ZC_EXPECT(record.definedRoles().size() == 0);
      ZC_EXPECT(record.signatureRoots().size() == 2);
      for (const auto& root : record.signatureRoots()) {
        ZC_EXPECT(root.sourceModule.encode().asPtr() != record.module().encode().asPtr());
        ZC_EXPECT(root.bindingSurfaceRevision != record.bindingSurfaceRevision());
      }
      foundPrelude = true;
      continue;
    }
    if (record.definedRoles().size() == 2) {
      ZC_EXPECT(record.lookupDefinitions().size() == 2);
      ZC_EXPECT(record.supportDefinitions().size() == 0);
      ZC_EXPECT(record.signatureRoots().size() == 2);
      ZC_EXPECT(record.definedRoles()[0].role == source::core::CoreSemanticRole::Copy);
      ZC_EXPECT(record.definedRoles()[1].role == source::core::CoreSemanticRole::Linear);
      ZC_EXPECT(record.definedRoles()[0].definition != record.definedRoles()[1].definition);
      for (size_t index = 0; index < record.lookupDefinitions().size(); ++index) {
        ZC_EXPECT(record.lookupDefinitions()[index].definition() ==
                  record.definedRoles()[index].definition);
      }
      foundMarker = true;
      continue;
    }
    ZC_EXPECT(record.lookupDefinitions().size() == 0);
    ZC_EXPECT(record.supportDefinitions().size() == 0);
    ZC_EXPECT(record.signatureRoots().size() == 0);
    ZC_EXPECT(record.definedRoles().size() == 0);
    foundRoot = true;
  }
  ZC_EXPECT(foundRoot);
  ZC_EXPECT(foundMarker);
  ZC_EXPECT(foundPrelude);
}

ZC_TEST("CompilerSessionTest.CheckerPreflightMaterializesSourceBackedCoreBootstrapInterfaces") {
  auto session = packageSession("let main = 0;\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_EXPECT(session->checkSources());
  ZC_EXPECT(captured.ids.empty());
}

ZC_TEST("CompilerSessionTest.CheckerPreflightPublishesAnnotatedConstantFacts") {
  auto session = packageSession("const value: i32 = 1;\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_EXPECT(session->checkSources());
  ZC_EXPECT(captured.ids.empty());
}

ZC_TEST("CompilerSessionTest.ErrorPropagateNonUnionUsesCheckerDiagnostic") {
  auto session = packageSession("fun entry(value: i32) -> i32 { return value?!; }\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_EXPECT(!session->checkSources());
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::ErrorPropagateNonUnion) == 1);
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::FunctionBodySemanticsUnavailable) == 0);
}

ZC_TEST("CompilerSessionTest.ErrorUnwrapNonUnionUsesCheckerDiagnostic") {
  auto session = packageSession("fun entry(value: i32) -> i32 { return value!!; }\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_EXPECT(!session->checkSources());
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::ErrorUnwrapNonUnion) == 1);
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::FunctionBodySemanticsUnavailable) == 0);
}

ZC_TEST("CompilerSessionTest.ErrorPropagateOrdinaryUnionUsesCheckerDiagnostic") {
  auto session = packageSession("fun entry(value: i32 | bool) -> i32 { return value?!; }\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_EXPECT(!session->checkSources());
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::ErrorPropagateNonUnion) == 1);
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::CheckerMissingRequiredFact) == 0);
}

ZC_TEST("CompilerSessionTest.ArrayIndexReturnUsesFunctionBodyUnavailableDiagnostic") {
  auto session = packageSession("fun entry(values: i32[]) -> i32 { return values[0]; }\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_EXPECT(!session->checkSources());
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::FunctionBodySemanticsUnavailable) == 1);
}

ZC_TEST("CompilerSessionTest.PublishesVerifiedOwnershipInputsForInitializedParameterReturn") {
  auto session = packageSession("fun identity(value: i32) -> i32 { return value; }\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_EXPECT(session->checkSources());
  ZC_EXPECT(captured.ids.empty());
  ZC_EXPECT(session->getOwnershipCheckedMirModules().size() == 1);
}

ZC_TEST("CompilerSessionTest.PublishesGenericFunctionSignature") {
  auto session = packageSession("fun identity<T>(value: i32) -> i32 { return value; }\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_EXPECT(session->checkSources());
  ZC_EXPECT(captured.ids.empty());
  const auto facts = session->getVerifiedSignatureFacts();
  ZC_REQUIRE(facts.size() == 1);
  ZC_EXPECT(facts[0].signatures().size() == 1);
  const auto& payload = facts[0].signatures()[0].payload.variant();
  ZC_REQUIRE(payload.is<checker::signature::CallableSignature>());
  ZC_EXPECT(payload.get<checker::signature::CallableSignature>().genericParameters.size() == 1);
  ZC_EXPECT(session->getOwnershipCheckedMirModules().size() == 1);
}

ZC_TEST("CompilerSessionTest.PublishesGenericDirectBorrowSignature") {
  auto session = packageSession("fun borrow<T>(value: &T) -> &T { return value; }\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_EXPECT(session->checkSources());
  ZC_EXPECT(captured.ids.empty());
  const auto facts = session->getVerifiedSignatureFacts();
  ZC_REQUIRE(facts.size() == 1);
  ZC_REQUIRE(facts[0].signatures().size() == 1);
  const auto& payload = facts[0].signatures()[0].payload.variant();
  ZC_REQUIRE(payload.is<checker::signature::CallableSignature>());
  ZC_EXPECT(payload.get<checker::signature::CallableSignature>().genericParameters.size() == 1);
  ZC_EXPECT(session->getOwnershipCheckedMirModules().size() == 1);
}

ZC_TEST("CompilerSessionTest.PublishesGenericMutableBorrowSignature") {
  auto session = packageSession("fun borrow<T>(value: &mut T) -> &mut T { return value; }\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_EXPECT(session->checkSources());
  ZC_EXPECT(captured.ids.empty());
  const auto facts = session->getVerifiedSignatureFacts();
  ZC_REQUIRE(facts.size() == 1);
  ZC_REQUIRE(facts[0].signatures().size() == 1);
  const auto& payload = facts[0].signatures()[0].payload.variant();
  ZC_REQUIRE(payload.is<checker::signature::CallableSignature>());
  const auto& callable = payload.get<checker::signature::CallableSignature>();
  ZC_REQUIRE(callable.parameters.size() == 1);
  ZC_EXPECT(callable.parameters[0].mode == checker::signature::ParameterMode::MutableReference);
  ZC_EXPECT(session->getOwnershipCheckedMirModules().size() == 1);
}

ZC_TEST("CompilerSessionTest.PublishesSharedParameterReborrow") {
  auto session = packageSession("fun reborrow(value: &i32) -> &i32 { return &*value; }\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_REQUIRE(session->checkSources());
  ZC_EXPECT(captured.ids.empty());
  const auto& hir = session->getVerifiedHirModules()[0];
  ZC_REQUIRE(hir.parameterReborrows().size() == 1);
  const auto& mir = session->getOwnershipCheckedMirModules()[0].builtMir();
  ZC_REQUIRE(mir.functions().size() == 1);
  const auto& function = mir.functions()[0];
  ZC_REQUIRE(function.blocks.size() == 1);
  ZC_REQUIRE(function.blocks[0].statements.size() == 2);
  ZC_REQUIRE(function.blocks[0].statements[1].kind() == mir::MirStatementKind::BorrowCreation);
  const auto& borrow = function.blocks[0].statements[1].borrowCreationValue();
  ZC_EXPECT(borrow.kind == mir::MirBorrowKind::Shared);
  ZC_REQUIRE(borrow.source.projections().size() == 1);
  ZC_EXPECT(borrow.source.projections()[0].kind() == mir::MirProjectionKind::Dereference);

  const auto checkedMir = session->getOwnershipCheckedMirModules();
  ZC_REQUIRE(checkedMir.size() == 1);
  const auto& inputs = checkedMir[0].facts();
  const auto& movePaths = inputs.movePaths();
  const auto& initialization = inputs.initialization();
  const auto& loans = inputs.loans();
  const auto& references = inputs.references();
  const auto& regions = inputs.regions();
  const auto& states = inputs.states();
  ZC_EXPECT(inputs.builtRevision().digest() == mir.revision().digest());
  ZC_EXPECT(inputs.overlayRevision().digest() == checkedMir[0].eventOverlay().revision().digest());
  ZC_EXPECT(inputs.borrowEvidenceRevision().digest() == mir.borrowEvidenceRevision().digest());
  ZC_REQUIRE(loans.loans().size() == 1);
  const auto& loan = loans.loans()[0];
  ZC_EXPECT(loan.owner == function.owner);
  ZC_EXPECT(loan.issue.location.point.kind() == ownership::MirPointKind::BeforeStatement);
  ZC_EXPECT(loan.issue.location.point.beforeStatementValue().ordinal == 1);
  ZC_EXPECT(loan.issue.operandOrdinal == 1);
  ZC_EXPECT(loan.commit.location.point.kind() == ownership::MirPointKind::BeforeStatement);
  ZC_EXPECT(loan.commit.location.point.beforeStatementValue().ordinal == 1);
  ZC_EXPECT(loan.commit.operandOrdinal == 2);
  ZC_EXPECT(loan.kind == mir::MirBorrowKind::Shared);
  ZC_EXPECT(loan.activeFrom.kind() == ownership::facts::OwnershipPointKind::AfterEvent);
  ZC_EXPECT(loan.activeFrom.afterEventValue().event.operandOrdinal == 1);
  ZC_EXPECT(loan.source.place.local() == borrow.source.local());
  ZC_EXPECT(loan.destination.place.local() == borrow.destination.local());
  ZC_REQUIRE(references.definitions().size() == 1);
  const auto& reference = references.definitions()[0];
  ZC_EXPECT(reference.owner == function.owner);
  ZC_EXPECT(reference.introduction.operandOrdinal == 2);
  ZC_EXPECT(reference.loan.operandOrdinal == 1);
  ZC_EXPECT(reference.origin.entry.location.point.kind() == ownership::MirPointKind::Entry);
  ZC_EXPECT(reference.origin.entry.operandOrdinal == 0);
  ZC_EXPECT(reference.origin.activation.kind() == ownership::facts::OwnershipPointKind::AfterEvent);
  ZC_EXPECT(reference.origin.activation.afterEventValue().event.operandOrdinal == 1);
  ZC_EXPECT(
      reference.origin.detail.get<ownership::facts::ParameterReferenceOrigin>().rootParameter == 0);
  ZC_EXPECT(reference.origin.referent.place.local() == borrow.source.local());
  ZC_REQUIRE(reference.origin.referent.place.projections().size() == 1);
  ZC_EXPECT(reference.origin.referent.place.projections()[0].kind() ==
            mir::MirProjectionKind::Dereference);
  ZC_EXPECT(reference.returned.location.point.kind() == ownership::MirPointKind::BeforeTerminator);
  ZC_EXPECT(reference.returned.operandOrdinal == 0);
  ZC_EXPECT(reference.destination.place.local() == borrow.destination.local());
  ZC_EXPECT(reference.livePoints.afterCommit.kind() ==
            ownership::facts::OwnershipPointKind::AfterEvent);
  ZC_EXPECT(reference.livePoints.afterCommit.afterEventValue().event.operandOrdinal == 2);
  ZC_EXPECT(reference.livePoints.afterCommitCfg.kind() ==
            ownership::facts::OwnershipPointKind::Cfg);
  ZC_EXPECT(reference.livePoints.afterCommitCfg.cfgValue().point.kind() ==
            ownership::MirPointKind::AfterStatement);
  ZC_EXPECT(reference.livePoints.beforeReturnCfg.kind() ==
            ownership::facts::OwnershipPointKind::Cfg);
  ZC_EXPECT(reference.livePoints.beforeReturnCfg.cfgValue().point.kind() ==
            ownership::MirPointKind::BeforeTerminator);
  ZC_EXPECT(reference.livePoints.beforeReturn.kind() ==
            ownership::facts::OwnershipPointKind::BeforeEvent);
  ZC_EXPECT(reference.livePoints.beforeReturn.beforeEventValue().event.operandOrdinal == 0);
  ZC_EXPECT(reference.livePoints.afterReturn.kind() ==
            ownership::facts::OwnershipPointKind::AfterEvent);
  ZC_EXPECT(reference.livePoints.afterReturn.afterEventValue().event.operandOrdinal == 0);
  ZC_REQUIRE(regions.regions().size() == 1);
  const auto& region = regions.regions()[0];
  ZC_EXPECT(region.owner == function.owner);
  ZC_EXPECT(region.entry.location.point.kind() == ownership::MirPointKind::Entry);
  ZC_EXPECT(region.entry.operandOrdinal == 0);
  ZC_EXPECT(region.loan.operandOrdinal == 1);
  ZC_EXPECT(region.origin.get<ownership::facts::ParameterReferenceOrigin>().rootParameter == 0);
  ZC_REQUIRE(region.members.size() == 6);
  ZC_EXPECT(region.members[0].kind() == ownership::facts::OwnershipPointKind::AfterEvent);
  ZC_EXPECT(region.members[0].afterEventValue().event.operandOrdinal == 1);
  ZC_EXPECT(region.members[5].kind() == ownership::facts::OwnershipPointKind::AfterEvent);
  ZC_EXPECT(region.members[5].afterEventValue().event.operandOrdinal == 0);
  ZC_REQUIRE(states.states().size() == 5);
  const auto& referenceState = states.states()[0];
  ZC_EXPECT(referenceState.owner == function.owner);
  ZC_EXPECT(referenceState.loan.operandOrdinal == 1);
  ZC_EXPECT(referenceState.origin.get<ownership::facts::ParameterReferenceOrigin>().rootParameter ==
            0);
  ZC_EXPECT(referenceState.destination.place.local() == borrow.destination.local());
  ZC_EXPECT(referenceState.point.kind() == ownership::facts::OwnershipPointKind::AfterEvent);
  ZC_EXPECT(referenceState.point.afterEventValue().event.operandOrdinal == 2);
  ZC_EXPECT(states.states()[4].point.kind() == ownership::facts::OwnershipPointKind::AfterEvent);
  ZC_EXPECT(states.states()[4].point.afterEventValue().event.operandOrdinal == 0);

  ZC_REQUIRE(movePaths.functions().size() == 1);
  bool foundDerefPath = false;
  for (const auto& path : movePaths.functions()[0].facts) {
    if (path.key.place.local() != borrow.source.local() ||
        path.key.place.projections().size() != 1 ||
        path.key.place.projections()[0].kind() != mir::MirProjectionKind::Dereference) {
      continue;
    }
    ZC_REQUIRE(path.parent != zc::none);
    ZC_IF_SOME(parent, path.parent) {
      ZC_EXPECT(parent.place.local() == borrow.source.local());
      ZC_EXPECT(parent.place.projections().size() == 0);
    }
    foundDerefPath = true;
  }
  ZC_EXPECT(foundDerefPath);

  ZC_REQUIRE(initialization.functions().size() == 1);
  bool foundInitializedBorrowTemporary = false;
  for (const auto& fact : initialization.functions()[0].facts) {
    if (fact.point.kind() != ownership::MirPointKind::AfterStatement ||
        fact.point.afterStatementValue().ordinal != 1 ||
        fact.key.place.local() != borrow.destination.local() ||
        fact.key.place.projections().size() != 0) {
      continue;
    }
    ZC_EXPECT(fact.state == ownership::facts::InitializationState::initialized());
    foundInitializedBorrowTemporary = true;
  }
  ZC_EXPECT(foundInitializedBorrowTemporary);
}

ZC_TEST("CompilerSessionTest.PublishesMutableParameterReborrow") {
  auto session =
      packageSession("fun reborrow(value: &mut i32) -> &mut i32 { return &mut *value; }\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_EXPECT(session->checkSources());
  ZC_EXPECT(captured.ids.empty());
  ZC_REQUIRE(session->getVerifiedHirModules().size() == 1);
  const auto& reborrows = session->getVerifiedHirModules()[0].parameterReborrows();
  ZC_REQUIRE(reborrows.size() == 1);
  ZC_EXPECT(reborrows[0].mutability == type::semantic::Mutability::Mutable);
  ZC_REQUIRE(session->getOwnershipCheckedMirModules().size() == 1);
  const auto& functions = session->getOwnershipCheckedMirModules()[0].builtMir().functions();
  ZC_REQUIRE(functions.size() == 1);
  const auto& function = functions[0];
  ZC_REQUIRE(function.blocks.size() == 1);
  ZC_REQUIRE(function.blocks[0].statements.size() == 2);
  ZC_REQUIRE(function.blocks[0].statements[1].kind() == mir::MirStatementKind::BorrowCreation);
  const auto& borrow = function.blocks[0].statements[1].borrowCreationValue();
  ZC_EXPECT(borrow.kind == mir::MirBorrowKind::Mutable);
  ZC_REQUIRE(borrow.source.projections().size() == 1);
  ZC_EXPECT(borrow.source.projections()[0].kind() == mir::MirProjectionKind::Dereference);

  const auto checkedMir = session->getOwnershipCheckedMirModules();
  ZC_REQUIRE(checkedMir.size() == 1);
  const auto& inputs = checkedMir[0].facts();
  const auto& movePaths = inputs.movePaths();
  const auto& initialization = inputs.initialization();
  const auto& loans = inputs.loans();
  const auto& references = inputs.references();
  const auto& regions = inputs.regions();
  const auto& states = inputs.states();
  ZC_REQUIRE(checkedMir[0].eventOverlay().functions().size() == 1);
  ZC_EXPECT(checkedMir[0].eventOverlay().functions()[0].owner == function.owner);
  ZC_EXPECT(loans.builtRevision().digest() ==
            session->getOwnershipCheckedMirModules()[0].builtMir().revision().digest());
  ZC_EXPECT(loans.overlayRevision().digest() == checkedMir[0].eventOverlay().revision().digest());
  ZC_EXPECT(
      loans.borrowEvidenceRevision().digest() ==
      session->getOwnershipCheckedMirModules()[0].builtMir().borrowEvidenceRevision().digest());
  ZC_REQUIRE(loans.loans().size() == 1);
  const auto& loan = loans.loans()[0];
  ZC_EXPECT(loan.owner == function.owner);
  ZC_EXPECT(loan.issue.location.point.kind() == ownership::MirPointKind::BeforeStatement);
  ZC_EXPECT(loan.issue.location.point.beforeStatementValue().ordinal == 1);
  ZC_EXPECT(loan.issue.operandOrdinal == 1);
  ZC_EXPECT(loan.commit.location.point.kind() == ownership::MirPointKind::BeforeStatement);
  ZC_EXPECT(loan.commit.location.point.beforeStatementValue().ordinal == 1);
  ZC_EXPECT(loan.commit.operandOrdinal == 2);
  ZC_EXPECT(loan.kind == mir::MirBorrowKind::Mutable);
  ZC_EXPECT(loan.activeFrom.kind() == ownership::facts::OwnershipPointKind::AfterEvent);
  ZC_EXPECT(loan.activeFrom.afterEventValue().event.operandOrdinal == 1);
  ZC_EXPECT(loan.source.place.local() == borrow.source.local());
  ZC_EXPECT(loan.destination.place.local() == borrow.destination.local());
  ZC_REQUIRE(references.definitions().size() == 1);
  const auto& reference = references.definitions()[0];
  ZC_EXPECT(reference.owner == function.owner);
  ZC_EXPECT(reference.introduction.operandOrdinal == 2);
  ZC_EXPECT(reference.loan.operandOrdinal == 1);
  ZC_EXPECT(reference.origin.entry.location.point.kind() == ownership::MirPointKind::Entry);
  ZC_EXPECT(reference.origin.entry.operandOrdinal == 0);
  ZC_EXPECT(reference.origin.activation.kind() == ownership::facts::OwnershipPointKind::AfterEvent);
  ZC_EXPECT(reference.origin.activation.afterEventValue().event.operandOrdinal == 1);
  ZC_EXPECT(
      reference.origin.detail.get<ownership::facts::ParameterReferenceOrigin>().rootParameter == 0);
  ZC_EXPECT(reference.origin.referent.place.local() == borrow.source.local());
  ZC_REQUIRE(reference.origin.referent.place.projections().size() == 1);
  ZC_EXPECT(reference.origin.referent.place.projections()[0].kind() ==
            mir::MirProjectionKind::Dereference);
  ZC_EXPECT(reference.returned.location.point.kind() == ownership::MirPointKind::BeforeTerminator);
  ZC_EXPECT(reference.returned.operandOrdinal == 0);
  ZC_EXPECT(reference.destination.place.local() == borrow.destination.local());
  ZC_EXPECT(reference.livePoints.afterCommit.kind() ==
            ownership::facts::OwnershipPointKind::AfterEvent);
  ZC_EXPECT(reference.livePoints.afterCommit.afterEventValue().event.operandOrdinal == 2);
  ZC_EXPECT(reference.livePoints.afterCommitCfg.kind() ==
            ownership::facts::OwnershipPointKind::Cfg);
  ZC_EXPECT(reference.livePoints.afterCommitCfg.cfgValue().point.kind() ==
            ownership::MirPointKind::AfterStatement);
  ZC_EXPECT(reference.livePoints.beforeReturnCfg.kind() ==
            ownership::facts::OwnershipPointKind::Cfg);
  ZC_EXPECT(reference.livePoints.beforeReturnCfg.cfgValue().point.kind() ==
            ownership::MirPointKind::BeforeTerminator);
  ZC_EXPECT(reference.livePoints.beforeReturn.kind() ==
            ownership::facts::OwnershipPointKind::BeforeEvent);
  ZC_EXPECT(reference.livePoints.beforeReturn.beforeEventValue().event.operandOrdinal == 0);
  ZC_EXPECT(reference.livePoints.afterReturn.kind() ==
            ownership::facts::OwnershipPointKind::AfterEvent);
  ZC_EXPECT(reference.livePoints.afterReturn.afterEventValue().event.operandOrdinal == 0);
  ZC_REQUIRE(regions.regions().size() == 1);
  const auto& region = regions.regions()[0];
  ZC_EXPECT(region.owner == function.owner);
  ZC_EXPECT(region.entry.location.point.kind() == ownership::MirPointKind::Entry);
  ZC_EXPECT(region.entry.operandOrdinal == 0);
  ZC_EXPECT(region.loan.operandOrdinal == 1);
  ZC_EXPECT(region.origin.get<ownership::facts::ParameterReferenceOrigin>().rootParameter == 0);
  ZC_REQUIRE(region.members.size() == 6);
  ZC_EXPECT(region.members[0].kind() == ownership::facts::OwnershipPointKind::AfterEvent);
  ZC_EXPECT(region.members[0].afterEventValue().event.operandOrdinal == 1);
  ZC_EXPECT(region.members[5].kind() == ownership::facts::OwnershipPointKind::AfterEvent);
  ZC_EXPECT(region.members[5].afterEventValue().event.operandOrdinal == 0);
  ZC_REQUIRE(states.states().size() == 5);
  const auto& referenceState = states.states()[0];
  ZC_EXPECT(referenceState.owner == function.owner);
  ZC_EXPECT(referenceState.loan.operandOrdinal == 1);
  ZC_EXPECT(referenceState.origin.get<ownership::facts::ParameterReferenceOrigin>().rootParameter ==
            0);
  ZC_EXPECT(referenceState.destination.place.local() == borrow.destination.local());
  ZC_EXPECT(referenceState.point.kind() == ownership::facts::OwnershipPointKind::AfterEvent);
  ZC_EXPECT(referenceState.point.afterEventValue().event.operandOrdinal == 2);
  ZC_EXPECT(states.states()[4].point.kind() == ownership::facts::OwnershipPointKind::AfterEvent);
  ZC_EXPECT(states.states()[4].point.afterEventValue().event.operandOrdinal == 0);

  bool foundBorrowRead = false;
  bool foundBorrowIssue = false;
  bool foundBorrowCommit = false;
  for (const auto& slot : checkedMir[0].eventOverlay().functions()[0].slots) {
    if (slot.key.location.point.kind() != ownership::MirPointKind::BeforeStatement ||
        slot.key.location.point.beforeStatementValue().ordinal != 1) {
      continue;
    }
    if (slot.key.operandOrdinal == 0) {
      ZC_EXPECT(slot.stage == ownership::OwnershipEventStage::Source);
      ZC_REQUIRE(slot.roles.size() == 1);
      ZC_EXPECT(slot.roles[0] == ownership::OwnershipEventRole::OperandRead);
      foundBorrowRead = true;
    } else if (slot.key.operandOrdinal == 1) {
      ZC_EXPECT(slot.stage == ownership::OwnershipEventStage::Effect);
      ZC_REQUIRE(slot.roles.size() == 2);
      ZC_EXPECT(slot.roles[0] == ownership::OwnershipEventRole::Operation);
      ZC_EXPECT(slot.roles[1] == ownership::OwnershipEventRole::BorrowIssue);
      foundBorrowIssue = true;
    } else if (slot.key.operandOrdinal == 2) {
      ZC_EXPECT(slot.stage == ownership::OwnershipEventStage::Commit);
      ZC_REQUIRE(slot.roles.size() == 1);
      ZC_EXPECT(slot.roles[0] == ownership::OwnershipEventRole::DestinationWrite);
      foundBorrowCommit = true;
    }
  }
  ZC_EXPECT(foundBorrowRead);
  ZC_EXPECT(foundBorrowIssue);
  ZC_EXPECT(foundBorrowCommit);

  ZC_REQUIRE(movePaths.functions().size() == 1);
  bool foundDerefPath = false;
  for (const auto& path : movePaths.functions()[0].facts) {
    if (path.key.place.local() != borrow.source.local() ||
        path.key.place.projections().size() != 1 ||
        path.key.place.projections()[0].kind() != mir::MirProjectionKind::Dereference) {
      continue;
    }
    ZC_REQUIRE(path.parent != zc::none);
    ZC_IF_SOME(parent, path.parent) {
      ZC_EXPECT(parent.place.local() == borrow.source.local());
      ZC_EXPECT(parent.place.projections().size() == 0);
    }
    foundDerefPath = true;
  }
  ZC_EXPECT(foundDerefPath);

  ZC_REQUIRE(initialization.functions().size() == 1);
  bool foundInitializedBorrowTemporary = false;
  for (const auto& fact : initialization.functions()[0].facts) {
    if (fact.point.kind() != ownership::MirPointKind::AfterStatement ||
        fact.point.afterStatementValue().ordinal != 1 ||
        fact.key.place.local() != borrow.destination.local() ||
        fact.key.place.projections().size() != 0) {
      continue;
    }
    ZC_EXPECT(fact.state == ownership::facts::InitializationState::initialized());
    foundInitializedBorrowTemporary = true;
  }
  ZC_EXPECT(foundInitializedBorrowTemporary);
}

ZC_TEST("CompilerSessionTest.PublishesGenericMutableParameterReborrow") {
  auto session =
      packageSession("fun reborrow<T>(value: &mut T) -> &mut T { return &mut *value; }\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_EXPECT(session->checkSources());
  ZC_EXPECT(captured.ids.empty());
  ZC_REQUIRE(session->getOwnershipCheckedMirModules().size() == 1);
  const auto& functions = session->getOwnershipCheckedMirModules()[0].builtMir().functions();
  ZC_REQUIRE(functions.size() == 1);
  ZC_REQUIRE(functions[0].blocks.size() == 1);
  ZC_REQUIRE(functions[0].blocks[0].statements.size() == 2);
  const auto& borrow = functions[0].blocks[0].statements[1].borrowCreationValue();
  ZC_EXPECT(borrow.kind == mir::MirBorrowKind::Mutable);
  ZC_REQUIRE(session->getOwnershipCheckedMirModules().size() == 1);
  const auto& inputs = session->getOwnershipCheckedMirModules()[0].facts();
  ZC_REQUIRE(inputs.loans().loans().size() == 1);
  ZC_REQUIRE(inputs.references().definitions().size() == 1);
  const auto& reference = inputs.references().definitions()[0];
  ZC_EXPECT(reference.destination.place.local() == borrow.destination.local());
  ZC_EXPECT(reference.returned.location.point.kind() == ownership::MirPointKind::BeforeTerminator);
  ZC_EXPECT(reference.returned.operandOrdinal == 0);
  ZC_REQUIRE(inputs.regions().regions().size() == 1);
  ZC_REQUIRE(inputs.states().states().size() == 5);
}

ZC_TEST("CompilerSessionTest.RejectsGenericParameterReturnWithoutBorrowContract") {
  auto session = packageSession("fun identity<T>(value: T) -> T { return value; }\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_EXPECT(!session->checkSources());
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::BorrowOutputRegionUnexpressible) == 1);
  ZC_EXPECT(session->getOwnershipCheckedMirModules().size() == 0);
}

ZC_TEST("CompilerSessionTest.PublishesVerifiedOwnershipInputsForParameterInitializedLocalReturn") {
  auto session = packageSession(
      "fun identity(value: i32) -> i32 { let copy: i32 = value; return copy; }\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_EXPECT(session->checkSources());
  ZC_EXPECT(captured.ids.empty());
  ZC_EXPECT(session->getOwnershipCheckedMirModules().size() == 1);
}

ZC_TEST("CompilerSessionTest.PublishesOwnershipInputsForLocalAliasReborrow") {
  auto session = packageSession(
      "fun reborrow(value: &i32) -> &i32 { let local = value; return &*local; }\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_REQUIRE(session->checkSources());
  ZC_EXPECT(captured.ids.empty());

  const auto& hir = session->getVerifiedHirModules()[0];
  ZC_REQUIRE(hir.parameterReborrows().size() == 1);
  ZC_EXPECT(hir.parameterReborrows()[0].sourceAlias != zc::none);

  const auto& mir = session->getOwnershipCheckedMirModules()[0].builtMir();
  ZC_REQUIRE(mir.functions().size() == 1);
  const auto& function = mir.functions()[0];
  ZC_REQUIRE(function.locals.size() == 3);
  ZC_EXPECT(function.locals[0].kind == mir::MirLocalKind::Parameter);
  ZC_EXPECT(function.locals[1].kind == mir::MirLocalKind::UserLocal);
  ZC_EXPECT(function.locals[2].kind == mir::MirLocalKind::Temporary);
  ZC_REQUIRE(function.blocks.size() == 1);
  ZC_REQUIRE(function.blocks[0].statements.size() == 4);
  ZC_EXPECT(function.blocks[0].statements[1].kind() == mir::MirStatementKind::Assign);
  ZC_EXPECT(function.blocks[0].statements[3].kind() == mir::MirStatementKind::BorrowCreation);

  const auto checkedMir = session->getOwnershipCheckedMirModules();
  ZC_REQUIRE(checkedMir.size() == 1);
  ZC_EXPECT(checkedMir[0].facts().loans().loans().size() == 1);
  ZC_EXPECT(checkedMir[0].facts().references().definitions().size() == 1);
  ZC_EXPECT(checkedMir[0].facts().regions().regions().size() == 1);
  ZC_EXPECT(checkedMir[0].facts().states().states().size() == 5);
  const auto& loan = checkedMir[0].facts().loans().loans()[0];
  ZC_EXPECT(loan.kind == mir::MirBorrowKind::Shared);
  ZC_EXPECT(loan.source.place.local() == function.locals[1].id);
  ZC_EXPECT(loan.destination.place.local() == function.locals[2].id);
  const auto& reference = checkedMir[0].facts().references().definitions()[0];
  ZC_EXPECT(reference.loan == loan.issue);
  ZC_EXPECT(reference.introduction == loan.commit);
  ZC_EXPECT(
      reference.origin.detail.get<ownership::facts::ParameterReferenceOrigin>().rootParameter == 0);
  ZC_EXPECT(reference.origin.referent.place.local() == function.locals[1].id);
  ZC_EXPECT(reference.destination.place.local() == function.locals[2].id);
  const auto& region = checkedMir[0].facts().regions().regions()[0];
  ZC_EXPECT(region.loan == loan.issue);
  ZC_EXPECT(region.origin.get<ownership::facts::ParameterReferenceOrigin>().rootParameter == 0);
  const auto& state = checkedMir[0].facts().states().states()[0];
  ZC_EXPECT(state.loan == loan.issue);
  ZC_EXPECT(state.origin.get<ownership::facts::ParameterReferenceOrigin>().rootParameter == 0);
  ZC_EXPECT(state.destination.place.local() == function.locals[2].id);
}

ZC_TEST("CompilerSessionTest.PublishesOwnershipInputsForMutableLocalAliasReborrow") {
  auto session = packageSession(
      "fun reborrow(value: &mut i32) -> &mut i32 { let local = value; return &mut *local; }\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_REQUIRE(session->checkSources());
  ZC_EXPECT(captured.ids.empty());

  const auto& hir = session->getVerifiedHirModules()[0];
  ZC_REQUIRE(hir.parameterReborrows().size() == 1);
  ZC_EXPECT(hir.parameterReborrows()[0].sourceAlias != zc::none);
  ZC_EXPECT(hir.parameterReborrows()[0].mutability == type::semantic::Mutability::Mutable);

  const auto& mir = session->getOwnershipCheckedMirModules()[0].builtMir();
  ZC_REQUIRE(mir.functions().size() == 1);
  const auto& function = mir.functions()[0];
  ZC_REQUIRE(function.blocks.size() == 1);
  ZC_REQUIRE(function.blocks[0].statements.size() == 4);
  ZC_REQUIRE(function.blocks[0].statements[3].kind() == mir::MirStatementKind::BorrowCreation);
  ZC_EXPECT(function.blocks[0].statements[3].borrowCreationValue().kind ==
            mir::MirBorrowKind::Mutable);

  const auto checkedMir = session->getOwnershipCheckedMirModules();
  ZC_REQUIRE(checkedMir.size() == 1);
  ZC_EXPECT(checkedMir[0].facts().loans().loans().size() == 1);
  ZC_EXPECT(checkedMir[0].facts().references().definitions().size() == 1);
  ZC_EXPECT(checkedMir[0].facts().regions().regions().size() == 1);
  ZC_EXPECT(checkedMir[0].facts().states().states().size() == 5);
  const auto& loan = checkedMir[0].facts().loans().loans()[0];
  ZC_EXPECT(loan.kind == mir::MirBorrowKind::Mutable);
  ZC_EXPECT(loan.source.place.local() == function.locals[1].id);
  ZC_EXPECT(loan.destination.place.local() == function.locals[2].id);
  const auto& reference = checkedMir[0].facts().references().definitions()[0];
  ZC_EXPECT(reference.loan == loan.issue);
  ZC_EXPECT(reference.introduction == loan.commit);
  ZC_EXPECT(
      reference.origin.detail.get<ownership::facts::ParameterReferenceOrigin>().rootParameter == 0);
  ZC_EXPECT(reference.origin.referent.place.local() == function.locals[1].id);
  ZC_EXPECT(reference.destination.place.local() == function.locals[2].id);
  const auto& region = checkedMir[0].facts().regions().regions()[0];
  ZC_EXPECT(region.loan == loan.issue);
  ZC_EXPECT(region.origin.get<ownership::facts::ParameterReferenceOrigin>().rootParameter == 0);
  const auto& state = checkedMir[0].facts().states().states()[0];
  ZC_EXPECT(state.loan == loan.issue);
  ZC_EXPECT(state.origin.get<ownership::facts::ParameterReferenceOrigin>().rootParameter == 0);
  ZC_EXPECT(state.destination.place.local() == function.locals[2].id);
}

ZC_TEST("CompilerSessionTest.PublishesVerifiedOwnershipInputsForInitializedAggregateFieldReturn") {
  auto session = packageSession(
      "struct Cell { value: i32, }\n"
      "fun entry() -> i32 { let cell = Cell { value: 0 }; return cell.value; }\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_EXPECT(session->checkSources());
  ZC_EXPECT(captured.ids.empty());
  ZC_EXPECT(session->getOwnershipCheckedMirModules().size() == 1);
}

ZC_TEST("CompilerSessionTest.PublishesVerifiedOwnershipInputsForAggregateFieldOverwrite") {
  auto session = packageSession(
      "struct Cell { mut value: i32, }\n"
      "fun entry() -> i32 { mut cell = Cell { value: 0 }; cell.value = 1; return cell.value; }\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_EXPECT(session->checkSources());
  ZC_EXPECT(captured.ids.empty());
  ZC_EXPECT(session->getOwnershipCheckedMirModules().size() == 1);
  const auto& builtMir = session->getOwnershipCheckedMirModules()[0].builtMir();
  ZC_REQUIRE(builtMir.functions().size() == 1);
  const auto& function = builtMir.functions()[0];
  ZC_REQUIRE(function.blocks.size() == 1);
  const auto& block = function.blocks[0];
  ZC_REQUIRE(block.statements.size() == 3);
  ZC_REQUIRE(block.statements[2].kind() == mir::MirStatementKind::Assign);
  const auto& overwrite = block.statements[2].assignmentValue();
  ZC_EXPECT(overwrite.initialization == mir::MirInitializationKind::Overwrite);
  ZC_REQUIRE(overwrite.destination.projections().size() == 1);
  ZC_EXPECT(overwrite.destination.projections()[0].kind() == mir::MirProjectionKind::Field);
  ZC_REQUIRE(block.terminator.returnValue().value != zc::none);
  ZC_IF_SOME(value, block.terminator.returnValue().value) {
    ZC_REQUIRE(value.place().projections().size() == 1);
    ZC_EXPECT(value.place().projections()[0].kind() == mir::MirProjectionKind::Field);
    ZC_EXPECT(value.place().projections()[0].fieldValue().field ==
              overwrite.destination.projections()[0].fieldValue().field);
  }
}

ZC_TEST("CompilerSessionTest.RejectsUninitializedLocalUseWithoutPublishingOwnershipInputs") {
  auto session = packageSession("fun entry() -> i32 { let value: i32; return value; }\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_EXPECT(!session->checkSources());
  const auto failures = session->getIrFailureGroups();
  ZC_EXPECT(failures.size() == 0);
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::UninitializedPlaceUse) == 1);
  ZC_EXPECT(childDiagnosticCount(captured, diagnostics::DiagID::PlaceBecameUnavailableHere) == 1);
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::OwnershipProofInvariant) == 0);
  ZC_EXPECT(session->getOwnershipCheckedMirModules().size() == 0);
}

ZC_TEST("CompilerSessionTest.RejectsUseAfterMoveWithoutPublishingOwnershipInputs") {
  auto session = packageSession(
      "import core::marker::{Copy};\n"
      "struct Cell { value: i32, }\n"
      "impl !Copy for Cell;\n"
      "fun entry() -> Cell { let first = Cell { value: 0 }; let second = first; return first; }\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_EXPECT(!session->checkSources());
  ZC_EXPECT(session->getIrFailureGroups().size() == 0);
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::UseAfterMove) == 1);
  ZC_EXPECT(childDiagnosticCount(captured, diagnostics::DiagID::ValueMovedHere) == 1);
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::OwnershipProofInvariant) == 0);
  ZC_EXPECT(session->getOwnershipCheckedMirModules().size() == 0);
}

ZC_TEST("CompilerSessionTest.AcceptsCopyAfterLocalTransfer") {
  auto session = packageSession(
      "fun entry() -> i32 { let first = 0; let second = first; return first; }\n"_zc);

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_EXPECT(session->checkSources());
  ZC_EXPECT(!session->getDiagnosticEngine().hasErrors());
  ZC_EXPECT(session->getOwnershipCheckedMirModules().size() == 1);
}

ZC_TEST("CompilerSessionTest.AcceptsThreeSequentialScalarLocals") {
  auto session = packageSession(
      "fun entry(a: i32) -> i32 { let x: i32 = a; let y: i32 = x; let z: i32 = 5; return z; }\n"_zc);

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_EXPECT(session->checkSources());
  ZC_EXPECT(!session->getDiagnosticEngine().hasErrors());
  ZC_EXPECT(session->getOwnershipCheckedMirModules().size() == 1);
}

ZC_TEST("CompilerSessionTest.AcceptsFourSequentialLocalsReturningParameter") {
  auto session = packageSession(
      "fun entry(a: i32, b: i32) -> i32 { let w: i32 = a; let x: i32 = w; let y: i32 = b; "
      "let z: i32 = 7; return a; }\n"_zc);

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_EXPECT(session->checkSources());
  ZC_EXPECT(!session->getDiagnosticEngine().hasErrors());
  ZC_EXPECT(session->getOwnershipCheckedMirModules().size() == 1);
}

ZC_TEST("CompilerSessionTest.AcceptsBinaryInitializerInSequentialLocalBody") {
  // A primitive arithmetic binary operation is now admitted as a local
  // initializer (slice 2 of G2), including an operand that references an earlier
  // local (`x * b`). The full body lowers end-to-end.
  auto session = packageSession(
      "fun entry(a: i32, b: i32) -> i32 { let x: i32 = a + b; let y: i32 = x * b; return y; }\n"_zc);

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_EXPECT(session->checkSources());
  ZC_EXPECT(!session->getDiagnosticEngine().hasErrors());
  ZC_EXPECT(session->getOwnershipCheckedMirModules().size() == 1);
}

ZC_TEST("CompilerSessionTest.RejectsLogicalInitializerInSequentialLocalBody") {
  // A logical short-circuit `&&` is not a primitive binary operation. Its
  // operand structure is admitted at the surface, but the checker leaves the
  // BinaryExpr production unsupported and fails closed with a missing-required-
  // fact invariant. This preserves negative coverage after the arithmetic and
  // bitwise binary initializers became supported.
  auto session = packageSession(
      "fun entry(a: bool, b: bool) -> bool { let x: bool = a && b; let y: bool = x; return y; }\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_EXPECT(!session->checkSources());
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::CheckerMissingRequiredFact) == 1);
  ZC_EXPECT(session->getOwnershipCheckedMirModules().size() == 0);
}

ZC_TEST(
    "CompilerSessionTest.RejectsUninitializedAggregateFieldUseWithoutPublishingOwnershipInputs") {
  auto session = packageSession(
      "struct Cell { value: i32, }\n"
      "fun entry() -> i32 { let cell: Cell; return cell.value; }\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_EXPECT(!session->checkSources());
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::UninitializedPlaceUse) == 1);
  ZC_EXPECT(childDiagnosticCount(captured, diagnostics::DiagID::PlaceBecameUnavailableHere) == 1);
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::OwnershipProofInvariant) == 0);
  ZC_EXPECT(session->getOwnershipCheckedMirModules().size() == 0);
}

ZC_TEST(
    "CompilerSessionTest."
    "RejectsUninitializedAggregateSiblingFieldWithoutPublishingOwnershipInputs") {
  auto session = packageSession(
      "struct Pair { mut left: i32, mut right: bool, }\n"
      "fun entry() -> bool { mut pair: Pair; pair.left = 0; return pair.right; }\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_REQUIRE(session->parseSources());
  ZC_REQUIRE(session->bindSources());
  ZC_EXPECT(!session->checkSources());
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::UninitializedPlaceUse) == 1);
  ZC_EXPECT(childDiagnosticCount(captured, diagnostics::DiagID::PlaceBecameUnavailableHere) == 1);
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::OwnershipProofInvariant) == 0);
  ZC_EXPECT(session->getOwnershipCheckedMirModules().size() == 0);
}

ZC_TEST("CompilerSessionTest.RejectsReservedCoreRootWithoutPublishingModuleGraph") {
  auto session = packageSession("module core;\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_EXPECT(!session->parseSources());
  ZC_EXPECT(session->hasVerifiedParsedSyntax());
  ZC_EXPECT(session->materializeModuleGraph() == zc::none);
  ZC_EXPECT(captured.ids.size() == 1);
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::ToolchainModuleRootReserved) == 1);
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::ModuleDeclarationNameMismatch) == 0);
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::ModuleGraphInvariant) == 0);
}

ZC_TEST("CompilerSessionTest.SuppressesReservedRootCycleAndRetainsIndependentFailure") {
  auto session = packageSession(
      "module core;\n"
      "import app::child::{member};\n"_zc,
      "module child;\n"
      "export app::{member};\n"
      "import missing::{member};\n"_zc);
  SessionDiagnostics captured;
  session->getDiagnosticEngine().addConsumer(zc::heap<SessionDiagnosticConsumer>(captured));

  ZC_EXPECT(!session->parseSources());
  ZC_EXPECT(session->hasVerifiedParsedSyntax());
  ZC_EXPECT(session->materializeModuleGraph() == zc::none);
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::ToolchainModuleRootReserved) == 1);
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::ImportModuleNotFound) == 1);
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::CircularImport) == 0);
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::CircularReexport) == 0);
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::ModuleDeclarationNameMismatch) == 0);
  ZC_EXPECT(diagnosticCount(captured, diagnostics::DiagID::ModuleGraphInvariant) == 0);
}

}  // namespace driver
}  // namespace compiler
}  // namespace zomlang
