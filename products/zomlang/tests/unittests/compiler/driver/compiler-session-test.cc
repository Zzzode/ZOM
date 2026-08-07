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

#include "zomlang/compiler/driver/compiler-session.h"

#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zc/core/time.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/basic/compiler-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/driver/package/manifest-parser.h"
#include "zomlang/compiler/driver/package/source-record.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/tests/unittests/compiler/driver/core-library-test-fixture.h"

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
};

class SessionDiagnosticConsumer final : public diagnostics::DiagnosticConsumer {
public:
  explicit SessionDiagnosticConsumer(SessionDiagnostics& capture) noexcept : capture(capture) {}

  void handleDiagnostic(const source::SourceManager&,
                        const diagnostics::Diagnostic& diagnostic) override {
    capture.ids.add(diagnostic.getId());
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

ZC_TEST("CompilerSessionTest.CompilerOptionsAccess") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  compilerOpts.emission.syntaxOnly = true;
  auto session = makeSession(langOpts, compilerOpts);

  auto& opts = session->getCompilerOptions();
  ZC_EXPECT(opts.emission.syntaxOnly);
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
