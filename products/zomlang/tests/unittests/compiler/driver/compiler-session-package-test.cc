// Copyright (c) 2026 Zode.Z. All rights reserved
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

#include <unistd.h>

#include "zc/core/time.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/driver/compiler-session.h"
#include "zomlang/compiler/driver/package/manifest-parser.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang::compiler::driver {
namespace {

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto result = Scalar::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid package-session scalar fixture");
}

identity::SortedFeatureSet emptyFeatures() {
  zc::Vector<identity::FeatureName> values;
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("empty package-session feature set was rejected");
}

zc::String writeTempSource(zc::StringPtr sourceText) {
  zc::String file = zc::str("/tmp/zom-package-session-test.XXXXXX.zom");
  const int descriptor = mkstemps(file.begin(), 4);
  ZC_REQUIRE(descriptor >= 0);
  size_t remaining = sourceText.size();
  const char* cursor = sourceText.cStr();
  while (remaining != 0) {
    const auto written = write(descriptor, cursor, remaining);
    ZC_REQUIRE(written > 0);
    cursor += written;
    remaining -= static_cast<size_t>(written);
  }
  close(descriptor);
  return file;
}

identity::CanonicalPackageSource source() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  return identity::CanonicalPackageSource::localPath(
      identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments)));
}

identity::PackageBaseKey packageBase(zc::StringPtr name) {
  return identity::PackageBaseKey::from(source(), scalar<identity::PackageName>(name),
                                        scalar<identity::ResolvedVersion>("1.0.0"_zc));
}

identity::PackageKey packageKey(zc::StringPtr name) {
  return identity::PackageKey::from(source(), scalar<identity::PackageName>(name),
                                    scalar<identity::ResolvedVersion>("1.0.0"_zc), emptyFeatures());
}

identity::CanonicalTargetSpecificationKey projection(zc::StringPtr abiProfile = "zom-v1"_zc) {
  zc::Vector<identity::TargetFeatureName> features;
  auto sorted = identity::SortedTargetFeatureSet::from(zc::mv(features));
  ZC_REQUIRE(sorted != zc::none);
  ZC_IF_SOME(values, sorted) {
    auto result = identity::CanonicalTargetSpecificationKey::from(
        scalar<identity::TargetComponentName>("x86_64"_zc),
        scalar<identity::TargetComponentName>("zom"_zc),
        scalar<identity::TargetComponentName>("none"_zc),
        scalar<identity::TargetComponentName>("unknown"_zc),
        scalar<identity::TargetComponentName>(abiProfile), 64, identity::Endianness::Little,
        zc::mv(values));
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("package-session projection was rejected");
}

package::RegisteredTargetProfileName profileName() {
  auto result = package::RegisteredTargetProfileName::from("host"_zc);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("package-session profile name was rejected");
}

irgen::TargetRegistrySnapshot targetRegistry(zc::StringPtr abiProfile = "zom-v1"_zc) {
  zc::Vector<irgen::CanonicalTargetFeature> targetFeatures;
  auto targetSpec = irgen::CanonicalTargetSpec::from(
      "x86_64-zom-none"_zc, "e-p:64:64"_zc, "generic"_zc, zc::mv(targetFeatures), abiProfile,
      irgen::BackendPanicStrategy::Unwind, irgen::ObjectFormat::Elf);
  ZC_REQUIRE(targetSpec != zc::none);
  zc::Vector<identity::TargetFeatureName> semanticFeatures;
  zc::Vector<irgen::CanonicalTargetSpec> specifications;
  ZC_IF_SOME(value, targetSpec) { specifications.add(zc::mv(value)); }
  auto profile = irgen::RegisteredTargetProfileRecord::from(
      profileName(), projection(abiProfile), zc::mv(semanticFeatures), zc::mv(specifications));
  ZC_REQUIRE(profile != zc::none);
  zc::Vector<irgen::RegisteredTargetProfileRecord> profiles;
  ZC_IF_SOME(value, profile) { profiles.add(zc::mv(value)); }
  auto registry = irgen::TargetRegistrySnapshot::from(profileName(), zc::mv(profiles));
  ZC_IF_SOME(value, registry) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("package-session target registry was rejected");
}

package::RegisteredTargetSelection selection(const irgen::TargetRegistrySnapshot& registry) {
  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  ZC_IF_SOME(targets, service) {
    auto selected = targets.select(zc::none, package::PackagePanicStrategy::Unwind);
    ZC_IF_SOME(value, selected) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("package-session target selection failed");
}

irgen::VerifiedTargetSelection verifiedSelection(const irgen::TargetRegistrySnapshot& registry) {
  auto verified = registry.verify(selection(registry));
  ZC_REQUIRE(verified.is<irgen::VerifiedTargetSelection>());
  return zc::mv(verified.get<irgen::VerifiedTargetSelection>());
}

identity::CanonicalRelativePath path(zc::StringPtr first, zc::StringPtr second) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(scalar<identity::CanonicalPathSegment>(first));
  segments.add(scalar<identity::CanonicalPathSegment>(second));
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

package::VerifiedPackageCompilationRequest requestForPackage(
    const irgen::TargetRegistrySnapshot& registry, zc::StringPtr packageName,
    bool requiresBuildScript = false) {
  zc::Vector<package::VerifiedCompilationRoot> roots;
  roots.add(package::VerifiedCompilationRoot::from(
      packageKey(packageName), identity::CrateTargetKind::Binary,
      scalar<identity::TargetName>(packageName), 2026, requiresBuildScript,
      path("src"_zc, "main.zom"_zc)));
  auto result = package::VerifiedPackageCompilationRequest::from(
      zc::mv(roots), selection(registry), selection(registry), package::SelectedLanguageOptions{},
      package::PackageLockMode::PreferLocked);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("package-session request was rejected");
}

package::VerifiedPackageCompilationRequest request(const irgen::TargetRegistrySnapshot& registry,
                                                   bool requiresBuildScript = false) {
  return requestForPackage(registry, "app"_zc, requiresBuildScript);
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

package::DigestVerifiedSourceSnapshot snapshot(zc::StringPtr output = zc::StringPtr()) {
  auto sourceDirectory = zc::newInMemoryDirectory(zc::nullClock());
  if (output.size() == 0) {
    sourceDirectory
        ->openFile(zc::Path({"src"_zc, "main.zom"_zc}),
                   zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
        ->writeAll("let main = 0;"_zc);
  } else {
    sourceDirectory
        ->openFile(zc::Path({"generated"_zc, "out.zom"_zc}),
                   zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
        ->writeAll(output);
  }
  MemoryFreshDirectoryFactory factory;
  package::SourceDirectoryMaterializer materializer;
  auto result = materializer.materialize(*sourceDirectory, factory);
  ZC_REQUIRE(result.is<package::DigestVerifiedSourceSnapshot>());
  return zc::mv(result.get<package::DigestVerifiedSourceSnapshot>());
}

package::PackageResolution resolution(zc::StringPtr packageName) {
  zc::Vector<package::ResolvedPackageSelection> packages;
  packages.add(package::ResolvedPackageSelection::from(
      packageBase(packageName), package::FeatureActivationDomain::Target, emptyFeatures()));
  zc::Vector<identity::PackageDependencyEdgeKey> edges;
  return package::PackageResolution::from(zc::mv(packages), zc::mv(edges));
}

zc::Vector<package::ResolvedPackageSourceSnapshot> resolvedSnapshots(zc::StringPtr packageName) {
  zc::Vector<package::ResolvedPackageSourceSnapshot> snapshots;
  snapshots.add(package::ResolvedPackageSourceSnapshot::from(packageBase(packageName), snapshot()));
  return snapshots;
}

VerifiedPackageSessionInput packageInput(const irgen::TargetRegistrySnapshot& registry,
                                         bool requiresBuildScript = false) {
  auto input = VerifiedPackageSessionInput::from(
      request(registry, requiresBuildScript), verifiedSelection(registry),
      verifiedSelection(registry), resolution("app"_zc), resolvedSnapshots("app"_zc));
  ZC_IF_SOME(value, input) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("valid atomic package-session input was rejected");
}

package::CanonicalBuildScriptManifest contract() {
  zc::Vector<identity::CanonicalRelativePath> files;
  files.add(path("tools"_zc, "build.zom"_zc));
  files.add(path("data"_zc, "input.txt"_zc));
  auto inventory = package::PackageSourceInventory::from(zc::mv(files));
  ZC_REQUIRE(inventory != zc::none);
  ZC_IF_SOME(sourceInventory, inventory) {
    zc::Vector<identity::CanonicalPathSegment> documentSegments;
    documentSegments.add(scalar<identity::CanonicalPathSegment>("Zom.toml"_zc));
    package::ManifestParser parser;
    auto parsed = parser.parseWorkspaceManifest(
        identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(documentSegments)),
        R"toml([package]
name = "app"
version = "1.0.0"
edition = "2026"

[build]
path = "tools/build.zom"
inputs = ["data/input.txt"]
outputs = ["generated/out.zom"]
)toml"_zc,
        sourceInventory);
    if (parsed.is<package::NormalizedManifest>()) {
      return package::CanonicalBuildScriptManifest::from(
          parsed.get<package::NormalizedManifest>().buildScript());
    }
  }
  ZC_FAIL_REQUIRE("package-session build contract was rejected");
}

identity::PreparatoryBuildScriptKey preparatory(zc::StringPtr name = "app"_zc) {
  zc::Vector<identity::PackageKey> dependencies;
  auto result = identity::PreparatoryBuildScriptKey::from(
      packageKey(name), scalar<identity::TargetName>("build"_zc), projection(),
      identity::SemanticCompilerOptionsKey::from(2026, true, false, true), zc::mv(dependencies));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("package-session preparatory key was rejected");
}

package::VerifiedBuildScriptPlan plan(zc::StringPtr packageName = "app"_zc) {
  zc::Vector<package::BuildScriptPlanNodeKey> predecessors;
  auto node = package::BuildScriptPlanNode::from(
      package::BuildScriptPlanNodeKey::from(preparatory(packageName)), contract(),
      zc::mv(predecessors));
  ZC_REQUIRE(node != zc::none);
  zc::Vector<package::BuildScriptPlanNode> nodes;
  ZC_IF_SOME(value, node) { nodes.add(zc::mv(value)); }
  auto result = package::VerifiedBuildScriptPlan::from(zc::mv(nodes));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("package-session build plan was rejected");
}

class RecordingPlanExecutor final : public package::BuildScriptPlanExecutor {
public:
  package::BuildScriptExecutionResult execute(
      const package::BuildScriptPlanNode& node,
      zc::ArrayPtr<const package::VerifiedBuildScriptResult> completedResults) override {
    observedCompletedCounts.add(completedResults.size());
    auto generatedSnapshot = snapshot("generated"_zc);
    zc::Vector<identity::BuildScriptDigestEntry> sourceDigests;
    zc::Vector<identity::BuildScriptEnvironmentEntry> declaredEnvironment;
    zc::Vector<identity::BuildScriptDigestEntry> generatedSources;
    generatedSources.add(identity::BuildScriptDigestEntry::from(
        generatedSnapshot.record().files()[0].path().clone(),
        generatedSnapshot.record().files()[0].contentDigest()));
    zc::Vector<identity::BuildScriptEnvironmentEntry> exportedEnvironment;
    auto output = identity::BuildScriptOutputRecord::from(
        node.key().preparatory().clone(), zc::mv(sourceDigests), zc::mv(declaredEnvironment),
        zc::mv(generatedSources), zc::mv(exportedEnvironment));
    ZC_REQUIRE(output != zc::none);
    zc::Vector<package::BuildScriptEnvironmentValue> responseEnvironment;
    ZC_IF_SOME(value, output) {
      return package::VerifiedBuildScriptResult::from(
          package::VerifiedBuildScriptRun::from(
              zc::mv(generatedSnapshot),
              package::BuildScriptResponse::success(zc::mv(responseEnvironment))),
          zc::mv(value));
    }
    ZC_UNREACHABLE;
  }

  zc::Vector<size_t> observedCompletedCounts;
};

zc::Own<CompilerSession> preparedSession(const basic::LangOptions& languageOptions,
                                         const basic::CompilerOptions& compilerOptions,
                                         bool requiresBuildScript = false) {
  identity::SemanticContextFactory contextFactory;
  auto session = zc::heap<CompilerSession>(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();
  ZC_REQUIRE(session->installVerifiedPackageInput(packageInput(registry, requiresBuildScript)));
  return session;
}

}  // namespace

ZC_TEST("Verified package input rejects a request root outside the resolved graph") {
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      resolution("other"_zc), resolvedSnapshots("other"_zc));
  ZC_EXPECT(input == zc::none);
}

ZC_TEST("Verified package input rejects target selections from another registry revision") {
  auto registry = targetRegistry();
  auto otherRegistry = targetRegistry("zom-v2"_zc);
  auto input = VerifiedPackageSessionInput::from(request(registry), verifiedSelection(registry),
                                                 verifiedSelection(otherRegistry),
                                                 resolution("app"_zc), resolvedSnapshots("app"_zc));
  ZC_EXPECT(input == zc::none);
}

ZC_TEST("Verified package input rejects snapshots outside the resolved graph") {
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(request(registry), verifiedSelection(registry),
                                                 verifiedSelection(registry), resolution("app"_zc),
                                                 resolvedSnapshots("other"_zc));
  ZC_EXPECT(input == zc::none);
}

ZC_TEST("CompilerSession installs one atomic package input exactly once") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();

  ZC_EXPECT(session.installVerifiedPackageInput(packageInput(registry)));
  ZC_EXPECT(!session.installVerifiedPackageInput(packageInput(registry)));
  ZC_REQUIRE(session.getIdentityRegistries() != zc::none);
  ZC_IF_SOME(registries, session.getIdentityRegistries()) {
    ZC_EXPECT(registries.packages().isFrozen());
    ZC_EXPECT(registries.packages().size() == 1);
  }
}

ZC_TEST("CompilerSession executes and freezes one exact build-plan result map") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  auto withoutBuildScript = preparedSession(languageOptions, compilerOptions);
  ZC_REQUIRE(withoutBuildScript->getFinalizedCompilationRoots().size() == 1);
  const auto withoutBuildOutputKey =
      withoutBuildScript->getFinalizedCompilationRoots()[0].crateKey().encode();
  auto session = preparedSession(languageOptions, compilerOptions, true);
  ZC_EXPECT(session->getFinalizedCompilationRoots().size() == 0);
  ZC_REQUIRE(session->getIdentityRegistries() != zc::none);
  ZC_IF_SOME(registries, session->getIdentityRegistries()) {
    ZC_EXPECT(registries.packages().isFrozen());
    ZC_EXPECT(registries.packages().size() == 1);
    ZC_EXPECT(registries.crates().size() == 0);
    auto admittedPackage = packageKey("app"_zc);
    ZC_EXPECT(registries.packages().find(admittedPackage) != zc::none);
  }
  RecordingPlanExecutor executor;
  ZC_EXPECT(session->executeBuildScriptPlan(plan(), executor) == zc::none);
  ZC_REQUIRE(session->getFinalizedCompilationRoots().size() == 1);
  ZC_EXPECT(session->getFinalizedCompilationRoots()[0].crateKey().encode().size() != 0);
  ZC_EXPECT(session->getFinalizedCompilationRoots()[0].crateKey().encode().asPtr() !=
            withoutBuildOutputKey.asPtr());
  ZC_REQUIRE(session->getBuildScriptPlan() != zc::none);
  ZC_REQUIRE(session->getBuildScriptResults() != zc::none);
  ZC_IF_SOME(results, session->getBuildScriptResults()) {
    ZC_EXPECT(results.results().size() == 1);
    ZC_EXPECT(results.planKeys().size() == 1);
  }
  ZC_REQUIRE(executor.observedCompletedCounts.size() == 1);
  ZC_EXPECT(executor.observedCompletedCounts[0] == 0);
  ZC_EXPECT(session->executeBuildScriptPlan(plan(), executor) ==
            package::BuildScriptIssue::BuildResultIntegrityViolation);
}

ZC_TEST("CompilerSession rejects identity freeze before required build outputs exist") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  auto session = preparedSession(languageOptions, compilerOptions, true);

  ZC_EXPECT(session->getFinalizedCompilationRoots().size() == 0);
  ZC_EXPECT(!session->parseSources());
  ZC_EXPECT(session->getDiagnosticEngine().hasErrors());
}

ZC_TEST("CompilerSession rejects a build plan outside the resolved package graph") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  auto session = preparedSession(languageOptions, compilerOptions);
  RecordingPlanExecutor executor;
  ZC_EXPECT(session->executeBuildScriptPlan(plan("other"_zc), executor) ==
            package::BuildScriptIssue::BuildResultIntegrityViolation);
  ZC_EXPECT(executor.observedCompletedCounts.size() == 0);
  ZC_EXPECT(session->getBuildScriptResults() == zc::none);
}

ZC_TEST("CompilerSession freezes canonical crate and source identities before parsing") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  auto session = preparedSession(languageOptions, compilerOptions);
  auto sourceFile =
      writeTempSource("interface Runnable { }\nclass App { }\nimpl Runnable for App { }\n"_zc);
  ZC_DEFER(unlink(sourceFile.cStr()));

  ZC_REQUIRE(session->getPackageCompilationRequest() != zc::none);
  auto finalizedRoots = session->getFinalizedCompilationRoots();
  ZC_REQUIRE(finalizedRoots.size() == 1);
  {
    ZC_EXPECT(session->addPackageSourceFile(sourceFile, "src/main.zom"_zc, finalizedRoots[0]) !=
              zc::none);
  }
  ZC_EXPECT(session->parseSources());

  ZC_REQUIRE(session->getIdentityRegistries() != zc::none);
  ZC_IF_SOME(registries, session->getIdentityRegistries()) {
    ZC_EXPECT(registries.crates().isFrozen());
    ZC_EXPECT(registries.crates().size() == 1);
    ZC_EXPECT(registries.sourceFiles().isFrozen());
    ZC_EXPECT(registries.sourceFiles().size() == 1);
    ZC_EXPECT(registries.sourceSnapshots().size() == 1);
    ZC_EXPECT(registries.modules().isFrozen());
    ZC_EXPECT(registries.modules().size() == 1);
    ZC_EXPECT(registries.definitions().isFrozen());
    ZC_EXPECT(registries.definitions().size() == 2);
    ZC_EXPECT(registries.impls().isFrozen());
    ZC_EXPECT(registries.impls().size() == 1);
  }
}

}  // namespace zomlang::compiler::driver
