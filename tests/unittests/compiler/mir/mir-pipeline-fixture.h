// Copyright (c) 2026 Zode.Z. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/ztest/test.h"
#include "compiler/driver/package/manifest-parser.h"
#include "compiler/driver/package/source-record.h"
#include "compiler/driver/session/compiler-session.h"
#include "compiler/ir/target/target-registry.h"
#include "tests/unittests/compiler/driver/core/core-library-test-fixture.h"
#include "tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::mir {
namespace test_detail {

namespace package = driver::package;

template <typename Scalar>
Scalar mirFixtureScalar(zc::StringPtr text) {
  auto result = Scalar::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid Built MIR fixture scalar");
}

inline identity::SortedFeatureSet mirEmptyFeatures() {
  zc::Vector<identity::FeatureName> values;
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid Built MIR fixture feature set");
}

inline identity::CanonicalPackageSource mirPackageSource() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  return identity::CanonicalPackageSource::localPath(
      identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments)));
}

inline identity::PackageBaseKey mirPackageBase() {
  return identity::PackageBaseKey::from(mirPackageSource(),
                                        mirFixtureScalar<identity::PackageName>("app"_zc),
                                        mirFixtureScalar<identity::ResolvedVersion>("1.0.0"_zc));
}

inline identity::PackageKey mirPackageKey() {
  return identity::PackageKey::from(
      mirPackageSource(), mirFixtureScalar<identity::PackageName>("app"_zc),
      mirFixtureScalar<identity::ResolvedVersion>("1.0.0"_zc), mirEmptyFeatures());
}

inline identity::CanonicalRelativePath mirMainPath() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(mirFixtureScalar<identity::CanonicalPathSegment>("src"_zc));
  segments.add(mirFixtureScalar<identity::CanonicalPathSegment>("main.zom"_zc));
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

inline ir::TargetRegistrySnapshot mirTargetRegistry() {
  zc::Vector<ir::CanonicalTargetFeature> targetFeatures;
  auto targetSpec = ir::CanonicalTargetSpec::from(
      "x86_64-zom-none"_zc, "e-p:64:64"_zc, "generic"_zc, zc::mv(targetFeatures), "zom"_zc,
      ir::BackendPanicStrategy::Unwind, ir::ObjectFormat::Elf);
  ZC_REQUIRE(targetSpec != zc::none);
  zc::Vector<identity::TargetFeatureName> semanticFeatures;
  zc::Vector<ir::CanonicalTargetSpec> specifications;
  ZC_IF_SOME(value, targetSpec) { specifications.add(zc::mv(value)); }
  auto profile = ir::RegisteredTargetProfileRecord::from(
      []() {
        auto result = package::RegisteredTargetProfileName::from("host"_zc);
        ZC_REQUIRE(result != zc::none);
        ZC_IF_SOME(value, result) { return zc::mv(value); }
        ZC_FAIL_REQUIRE("invalid Built MIR fixture target profile name");
      }(),
      []() {
        zc::Vector<identity::TargetFeatureName> features;
        auto sorted = identity::SortedTargetFeatureSet::from(zc::mv(features));
        ZC_REQUIRE(sorted != zc::none);
        ZC_IF_SOME(values, sorted) {
          auto result = identity::CanonicalTargetSpecificationKey::from(
              mirFixtureScalar<identity::TargetComponentName>("x86_64"_zc),
              mirFixtureScalar<identity::TargetComponentName>("zom"_zc),
              mirFixtureScalar<identity::TargetComponentName>("none"_zc),
              mirFixtureScalar<identity::TargetComponentName>("unknown"_zc),
              mirFixtureScalar<identity::TargetComponentName>("zom"_zc), 64,
              identity::Endianness::Little, zc::mv(values));
          ZC_IF_SOME(value, result) { return zc::mv(value); }
        }
        ZC_FAIL_REQUIRE("invalid Built MIR fixture target projection");
      }(),
      zc::mv(semanticFeatures), zc::mv(specifications));
  ZC_REQUIRE(profile != zc::none);
  zc::Vector<ir::RegisteredTargetProfileRecord> profiles;
  ZC_IF_SOME(value, profile) { profiles.add(zc::mv(value)); }
  auto registryName = package::RegisteredTargetProfileName::from("host"_zc);
  ZC_REQUIRE(registryName != zc::none);
  auto registry =
      ir::TargetRegistrySnapshot::from(zc::mv(ZC_REQUIRE_NONNULL(registryName)), zc::mv(profiles));
  ZC_IF_SOME(value, registry) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid Built MIR fixture target registry");
}

inline package::RegisteredTargetSelection mirTargetSelection(
    const ir::TargetRegistrySnapshot& registry) {
  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  ZC_IF_SOME(targets, service) {
    auto result = targets.select(zc::none, package::PackagePanicStrategy::Unwind);
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("invalid Built MIR fixture target selection");
}

inline ir::VerifiedTargetSelection mirVerifiedTargetSelection(
    const ir::TargetRegistrySnapshot& registry) {
  auto result = registry.verify(mirTargetSelection(registry));
  ZC_REQUIRE(result.is<ir::VerifiedTargetSelection>());
  return zc::mv(result.get<ir::VerifiedTargetSelection>());
}

inline package::VerifiedPackageCompilationRequest mirCompilationRequest(
    const ir::TargetRegistrySnapshot& registry) {
  zc::Vector<package::VerifiedCompilationRoot> roots;
  roots.add(package::VerifiedCompilationRoot::from(
      mirPackageKey(), identity::CrateTargetKind::Binary,
      mirFixtureScalar<identity::TargetName>("app"_zc), 2026, false, mirMainPath()));
  auto result = package::VerifiedPackageCompilationRequest::from(
      zc::mv(roots), mirTargetSelection(registry), mirTargetSelection(registry),
      package::SelectedLanguageOptions{}, package::PackageLockMode::PreferLocked);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid Built MIR fixture compilation request");
}

class MirMemoryFreshDirectory final : public package::FreshSourceDirectory {
public:
  MirMemoryFreshDirectory() : rootValue(zc::newInMemoryDirectory(zc::nullClock())) {}
  ~MirMemoryFreshDirectory() noexcept override = default;
  const zc::Directory& root() const override { return *rootValue; }
  zc::Maybe<package::MaterializationIssue> finish() override { return zc::none; }

private:
  zc::Own<zc::Directory> rootValue;
};

class MirMemoryFreshDirectoryFactory final : public package::FreshSourceDirectoryFactory {
public:
  package::FreshSourceDirectoryResult create() override {
    zc::Own<package::FreshSourceDirectory> result = zc::heap<MirMemoryFreshDirectory>();
    return zc::mv(result);
  }
};

inline package::DigestVerifiedSourceSnapshot mirSourceSnapshot(zc::StringPtr sourceText) {
  auto sourceDirectory = zc::newInMemoryDirectory(zc::nullClock());
  sourceDirectory
      ->openFile(zc::Path({"src"_zc, "main.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll(sourceText);
  MirMemoryFreshDirectoryFactory factory;
  package::SourceDirectoryMaterializer materializer;
  auto result = materializer.materialize(*sourceDirectory, factory);
  ZC_REQUIRE(result.is<package::DigestVerifiedSourceSnapshot>());
  return zc::mv(result.get<package::DigestVerifiedSourceSnapshot>());
}

inline package::ResolutionOutput mirResolution(zc::MemoryResource& resource,
                                               zc::StringPtr sourceText) {
  package::ManifestParser parser;
  zc::Vector<identity::CanonicalRelativePath> files;
  auto inventory = package::PackageSourceInventory::from(zc::mv(files));
  ZC_REQUIRE(inventory != zc::none);
  package::NormalizedManifest normalized = [&]() {
    ZC_IF_SOME(sourceInventory, inventory) {
      zc::Vector<identity::CanonicalPathSegment> documentSegments;
      documentSegments.add(mirFixtureScalar<identity::CanonicalPathSegment>("Zom.toml"_zc));
      auto parsed = parser.parseWorkspaceManifest(
          identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(documentSegments)),
          "[package]\nname = \"app\"\nversion = \"1.0.0\"\nedition = \"2026\"\n"_zc,
          sourceInventory);
      ZC_REQUIRE(parsed.is<package::NormalizedManifest>());
      return zc::mv(parsed.get<package::NormalizedManifest>());
    }
    ZC_UNREACHABLE
  }();
  auto record = package::LocalPackageRecord::from(mirPackageBase(), zc::mv(normalized),
                                                  mirSourceSnapshot(sourceText));
  ZC_REQUIRE(record != zc::none);
  zc::Vector<package::ResolverRelease> releases;
  ZC_IF_SOME(value, record) { releases.add(package::ResolverRelease::fromLocal(value)); }
  zc::Vector<package::ResolverRoot> roots;
  roots.add(package::ResolverRoot::from(mirPackageBase(), mirEmptyFeatures(), false, false));
  auto result = package::PackageResolver::resolve(resource, roots, releases);
  ZC_REQUIRE(result.is<package::ResolutionOutput>());
  return zc::mv(result.get<package::ResolutionOutput>());
}

inline zc::Vector<package::ResolvedPackageSourceSnapshot> mirResolvedSnapshots(
    zc::StringPtr sourceText) {
  zc::Vector<package::ResolvedPackageSourceSnapshot> snapshots;
  snapshots.add(package::ResolvedPackageSourceSnapshot::from(mirPackageBase(),
                                                             mirSourceSnapshot(sourceText)));
  return snapshots;
}

}  // namespace test_detail

/// \brief In-memory package session that runs parseSources, bindSources, and
/// checkSources for one source text, with the core library installed.
///
/// Shared by the Built MIR and RFC 0056 monomorphization package tests so the
/// complete checkSources pipeline, including phase 0x0a, is exercised against a
/// real CompilerSession rather than forged artifacts.
class MirPipelineFixture final {
public:
  explicit MirPipelineFixture(zc::StringPtr sourceText)
      : session(contextFactory, languageOptions, compilerOptions) {
    auto registry = test_detail::mirTargetRegistry();
    auto input = driver::VerifiedPackageSessionInput::from(
        test_detail::mirCompilationRequest(registry),
        test_detail::mirVerifiedTargetSelection(registry),
        test_detail::mirVerifiedTargetSelection(registry),
        test_detail::mirResolution(session.getPackageResolutionMemoryResource(), sourceText),
        test_detail::mirResolvedSnapshots(sourceText));
    ZC_REQUIRE(input != zc::none);
    ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }
    driver::core_library_test::installCoreSources(session);
    const auto roots = session.getFinalizedCompilationRoots();
    ZC_REQUIRE(roots.size() == 1);
    ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
    ZC_REQUIRE(session.parseSources());
    ZC_REQUIRE(session.bindSources());
    ZC_REQUIRE(session.checkSources());
    ZC_REQUIRE(!session.hasDiagnosticErrors());
    ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  }

  ZC_DISALLOW_COPY(MirPipelineFixture);

  const driver::CompilerSession& sessionRef() const { return session; }
  const VerifiedBuiltMir& builtMir() const {
    return session.getOwnershipCheckedMirModules()[0].builtMir();
  }

private:
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  driver::CompilerSession session;
};

}  // namespace zomlang::compiler::mir
