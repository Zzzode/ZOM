// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/module-dependency-requests.h"

#include "parsed-module-query-test-fixture.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/source-diagnostic-draft-buffer.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/parser/parser.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang::compiler::binder {
namespace {

template <typename Scalar>
Scalar requireScalar(zc::StringPtr text) {
  auto value = Scalar::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid canonical request-derivation fixture");
}

identity::PackageKey packageKey() {
  zc::Vector<identity::CanonicalPathSegment> pathSegments;
  auto path = identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(pathSegments));
  zc::Vector<identity::FeatureName> features;
  auto featureSet = identity::SortedFeatureSet::from(zc::mv(features));
  auto version = identity::ResolvedVersion::fromCanonical("0.0.0"_zc);
  ZC_IF_SOME(featuresValue, featureSet) {
    ZC_IF_SOME(versionValue, version) {
      return identity::PackageKey::from(identity::CanonicalPackageSource::localPath(zc::mv(path)),
                                        requireScalar<identity::PackageName>("requests"_zc),
                                        zc::mv(versionValue), zc::mv(featuresValue));
    }
  }
  ZC_FAIL_REQUIRE("invalid package request-derivation fixture");
}

identity::CrateKey crateKey() {
  zc::Vector<identity::TargetFeatureName> features;
  auto featureSet = identity::SortedTargetFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(featuresValue, featureSet) {
    auto target = identity::CanonicalTargetSpecificationKey::from(
        requireScalar<identity::TargetComponentName>("aarch64"_zc),
        requireScalar<identity::TargetComponentName>("apple"_zc),
        requireScalar<identity::TargetComponentName>("darwin"_zc),
        requireScalar<identity::TargetComponentName>("none"_zc),
        requireScalar<identity::TargetComponentName>("zom"_zc), 64, identity::Endianness::Little,
        zc::mv(featuresValue));
    ZC_IF_SOME(targetValue, target) {
      zc::Maybe<identity::BuildScriptProducerKey> noOutput;
      auto config = identity::CompilationConfigKey::from(
          identity::CompilationDomain::Target, zc::mv(targetValue),
          identity::SemanticCompilerOptionsKey::from(2026, true, false, false), zc::mv(noOutput));
      ZC_IF_SOME(configValue, config) {
        auto crate = identity::CrateKey::from(
            identity::CompilationUnitIdentity::userPackage(packageKey()),
            identity::CrateTargetKind::Library, requireScalar<identity::TargetName>("requests"_zc),
            zc::mv(configValue));
        ZC_IF_SOME(value, crate) { return zc::mv(value); }
      }
    }
  }
  ZC_FAIL_REQUIRE("invalid crate request-derivation fixture");
}

identity::CrateKey coreCrateKey() {
  zc::Vector<identity::TargetFeatureName> features;
  auto featureSet = identity::SortedTargetFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(featuresValue, featureSet) {
    auto target = identity::CanonicalTargetSpecificationKey::from(
        requireScalar<identity::TargetComponentName>("aarch64"_zc),
        requireScalar<identity::TargetComponentName>("apple"_zc),
        requireScalar<identity::TargetComponentName>("darwin"_zc),
        requireScalar<identity::TargetComponentName>("none"_zc),
        requireScalar<identity::TargetComponentName>("zom"_zc), 64, identity::Endianness::Little,
        zc::mv(featuresValue));
    ZC_IF_SOME(targetValue, target) {
      zc::Maybe<identity::BuildScriptProducerKey> noOutput;
      auto config = identity::CompilationConfigKey::from(
          identity::CompilationDomain::Target, zc::mv(targetValue),
          identity::SemanticCompilerOptionsKey::from(2026, true, false, false), zc::mv(noOutput));
      ZC_IF_SOME(configValue, config) {
        auto crate = identity::CrateKey::from(
            identity::CompilationUnitIdentity::toolchain(identity::ToolchainUnitKey::core()),
            identity::CrateTargetKind::Library, requireScalar<identity::TargetName>("core"_zc),
            zc::mv(configValue));
        ZC_IF_SOME(value, crate) { return zc::mv(value); }
      }
    }
  }
  ZC_FAIL_REQUIRE("invalid core crate request-derivation fixture");
}

identity::SourceFileKey sourceKey() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(requireScalar<identity::CanonicalPathSegment>("main.zom"_zc));
  auto path = identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
  return identity::SourceFileKey::from(crateKey(),
                                       identity::SourceOriginKey::localFile(zc::mv(path)));
}

identity::ModuleKey moduleKey() {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(requireScalar<identity::ModulePathSegment>("root"_zc));
  auto module = identity::ModuleKey::from(crateKey(), zc::mv(path));
  ZC_IF_SOME(value, module) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid module request-derivation fixture");
}

identity::ModuleKey nestedModuleKey() {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(requireScalar<identity::ModulePathSegment>("inactive"_zc));
  path.add(requireScalar<identity::ModulePathSegment>("nested"_zc));
  auto module = identity::ModuleKey::from(crateKey(), zc::mv(path));
  ZC_IF_SOME(value, module) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid nested module request-derivation fixture");
}

identity::ModuleKey inactiveRootModuleKey() {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(requireScalar<identity::ModulePathSegment>("inactive"_zc));
  auto module = identity::ModuleKey::from(crateKey(), zc::mv(path));
  ZC_IF_SOME(value, module) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid inactive root module request-derivation fixture");
}

identity::SemanticContextBrand requireContext(identity::SemanticContextFactory& factory) {
  ZC_IF_SOME(context, factory.issue()) { return context; }
  ZC_FAIL_REQUIRE("semantic context request-derivation fixture exhausted");
}

struct ParsedSource final {
  explicit ParsedSource(zc::StringPtr text)
      : sources(zc::heap<source::SourceManager>()),
        buffer(sources->addMemBufferCopy(text.asBytes(), "main.zom")) {
    diagnostics::SourceDiagnosticDraftBuffer diagnosticFacts(*sources, buffer);
    parser::Parser parser(*sources, diagnosticFacts, options, strings, buffer);
    ZC_IF_SOME(parsed, parser.parse()) {
      tree = zc::mv(parsed);
    } else {
      ZC_FAIL_REQUIRE("request-derivation source did not parse");
    }
    ZC_REQUIRE(!diagnosticFacts.hasErrors());
    auto retainedTokens = parser.takeTokenSnapshot();
    ZC_REQUIRE(retainedTokens != zc::none);
    ZC_IF_SOME(value, retainedTokens) { tokens = zc::mv(value); }
  }

  identity::ImmutableSourceSnapshot snapshot() const {
    auto value = identity::ImmutableSourceSnapshot::from(
        sourceKey(), zc::heapArray(sources->getEntireTextForBuffer(buffer)));
    ZC_IF_SOME(snapshotValue, value) { return zc::mv(snapshotValue); }
    ZC_FAIL_REQUIRE("request-derivation snapshot failed");
  }

  zc::Own<source::SourceManager> sources;
  basic::LangOptions options;
  basic::StringPool strings;
  source::BufferId buffer;
  ast::Tree tree;
  zc::Maybe<parser::ParsedTokenSnapshot> tokens;
};

struct DerivationFixture final {
  explicit DerivationFixture(ParsedSource& sourceFixture, bool withDependencyAlias = false)
      : context(requireContext(factory)), registries(createRegistries()) {
    auto snapshot = sourceFixture.snapshot();
    ZC_REQUIRE(registries.collectCompilationUnit(identity::CompilationUnitIdentity::userPackage(
                   packageKey())) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeCompilationUnits() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectCrate(crateKey()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeCrates() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectSourceFile(snapshot.clone()) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeSourceFiles() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectModule(moduleKey()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeModules() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeStableIdentities() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeGenericParameters() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeCallableParameters() == identity::FrozenRegistryFailure::None);
    module = requireHandle(registries.modules().find(moduleKey()));

    ZC_REQUIRE(sourceFixture.tokens != zc::none);
    ZC_IF_SOME(tokensValue, sourceFixture.tokens) {
      parsedModule = test::requireVerifiedParsedSource(
          context, registries, snapshot, *sourceFixture.sources, sourceFixture.buffer,
          zc::mv(tokensValue), zc::mv(sourceFixture.tree));
    }

    zc::Vector<identity::CanonicalPathSegment> noRootSegments;
    zc::Vector<ModuleSearchRoot> searchRoots;
    searchRoots.add(ModuleSearchRoot::workspace(
        crateKey(), identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(noRootSegments))));
    zc::Vector<ModuleSourceSnapshotRevision> snapshots;
    snapshots.add(
        ModuleSourceSnapshotRevision(snapshot.source().clone(), snapshot.contentDigest()));
    zc::Vector<GeneratedModuleSourceRevision> generated;
    zc::Vector<ModuleDependencyAliasRoot> aliases;
    if (withDependencyAlias) {
      aliases.add(ModuleDependencyAliasRoot(
          crateKey(), requireScalar<identity::DependencyAlias>("dep"_zc), moduleKey()));
    }
    zc::Vector<identity::ModuleKey> ancestryPath;
    ancestryPath.add(moduleKey());
    zc::Vector<RequesterModuleAncestryCandidate> ancestry;
    ancestry.add(RequesterModuleAncestryCandidate(moduleKey(), zc::mv(ancestryPath)));
    zc::Vector<StructuralModuleCatalogEntry> catalog;
    catalog.add(StructuralModuleCatalogEntry(moduleKey(), module, sourceKey()));
    auto frozen = StructuralModuleResolver::freeze(
        context, registries,
        ModuleResolutionEnvironmentRecord(zc::mv(searchRoots), zc::mv(snapshots), zc::mv(generated),
                                          zc::mv(aliases), zc::mv(ancestry)),
        zc::mv(catalog));
    ZC_REQUIRE(frozen.is<StructuralModuleResolver>());
    resolver = zc::mv(frozen.get<StructuralModuleResolver>());
  }

  template <typename Handle>
  Handle requireHandle(zc::Maybe<Handle>&& value) {
    ZC_IF_SOME(handle, value) { return handle; }
    ZC_FAIL_REQUIRE("request-derivation handle lookup failed");
  }

  identity::SemanticIdentityRegistrySet createRegistries() {
    auto value = identity::SemanticIdentityRegistrySet::create(factory, context);
    ZC_IF_SOME(registrySet, value) { return zc::mv(registrySet); }
    ZC_FAIL_REQUIRE("request-derivation registry creation failed");
  }

  identity::SemanticContextFactory factory;
  identity::SemanticContextBrand context;
  identity::SemanticIdentityRegistrySet registries;
  identity::ModuleId module;
  zc::Maybe<VerifiedParsedModule> parsedModule;
  zc::Maybe<StructuralModuleResolver> resolver;
};

void expectPath(const ModuleDependencyRequest& request, zc::StringPtr first,
                zc::StringPtr second = nullptr) {
  ZC_REQUIRE(request.normalizedPath().size() == (second == nullptr ? 1 : 2));
  ZC_EXPECT(request.normalizedPath()[0].text() == first);
  if (second != nullptr) { ZC_EXPECT(request.normalizedPath()[1].text() == second); }
}

ZC_TEST("ModuleDependencyRequestDeriver.DerivesImportsAndReexportsInCanonicalOrder") {
  ParsedSource sourceFixture(
      "module root;\n"
      "import math::geometry as geo;\n"
      "export math::geometry::{Point};\n"_zc);
  DerivationFixture fixture(sourceFixture);
  ZC_REQUIRE(fixture.parsedModule != zc::none);
  ZC_REQUIRE(fixture.resolver != zc::none);
  ZC_IF_SOME(parsedModule, fixture.parsedModule) {
    ZC_IF_SOME(resolver, fixture.resolver) {
      auto result = ModuleDependencyRequestDeriver::derive(fixture.module, parsedModule, resolver);
      ZC_REQUIRE(result.is<zc::Vector<ModuleDependencyRequest>>());
      auto& requests = result.get<zc::Vector<ModuleDependencyRequest>>();
      ZC_REQUIRE(requests.size() == 2);

      ZC_EXPECT(requests[0].kind() == identity::ModuleDependencyKind::Import);
      ZC_EXPECT(requests[1].kind() == identity::ModuleDependencyKind::ForeignReexport);
      expectPath(requests[0], "math"_zc, "geometry"_zc);
      expectPath(requests[1], "math"_zc, "geometry"_zc);

      uint32_t previousOrdinal = 0;
      for (size_t index = 0; index < requests.size(); ++index) {
        const auto& request = requests[index];
        ZC_EXPECT(request.requester() == fixture.module);
        ZC_REQUIRE(request.syntaxSites().size() == 1);
        ZC_EXPECT(request.syntaxSite().span.byteStart() < request.syntaxSite().span.byteEnd());
        if (index != 0) { ZC_EXPECT(previousOrdinal < request.syntaxSite().schemaPreorderOrdinal); }
        previousOrdinal = request.syntaxSite().schemaPreorderOrdinal;
      }
      return;
    }
  }
  ZC_EXPECT(false);
}

ZC_TEST("ModuleDependencyRequestDeriver.DeduplicatesSemanticKeyAndRetainsEverySite") {
  ParsedSource sourceFixture(
      "module root;\n"
      "import alpha::value;\n"
      "import alpha::value;\n"_zc);
  DerivationFixture fixture(sourceFixture);
  ZC_REQUIRE(fixture.parsedModule != zc::none);
  ZC_REQUIRE(fixture.resolver != zc::none);
  ZC_IF_SOME(parsedModule, fixture.parsedModule) {
    ZC_IF_SOME(resolver, fixture.resolver) {
      auto result = ModuleDependencyRequestDeriver::derive(fixture.module, parsedModule, resolver);
      ZC_REQUIRE(result.is<zc::Vector<ModuleDependencyRequest>>());
      auto& requests = result.get<zc::Vector<ModuleDependencyRequest>>();
      ZC_REQUIRE(requests.size() == 1);
      const auto sites = requests[0].syntaxSites();
      ZC_REQUIRE(sites.size() == 2);
      ZC_EXPECT(sites[0].span.byteStart() < sites[1].span.byteStart());
      ZC_EXPECT(sites[0].schemaPreorderOrdinal < sites[1].schemaPreorderOrdinal);
      ZC_EXPECT(requests[0].key().policy().encode().asPtr() == resolver.policy().encode().asPtr());
      ZC_EXPECT(requests[0].key().dependencyAlias() == zc::none);
      return;
    }
  }
  ZC_EXPECT(false);
}

ZC_TEST("ModuleDependencyRequestDeriver.SiteEditsDoNotReplaceSemanticKey") {
  ParsedSource firstSource("module root;\nimport alpha::value;\n"_zc);
  ParsedSource secondSource("module root;\n\n\nimport alpha::value;\n"_zc);
  DerivationFixture firstFixture(firstSource);
  DerivationFixture secondFixture(secondSource);
  ZC_REQUIRE(firstFixture.parsedModule != zc::none);
  ZC_REQUIRE(firstFixture.resolver != zc::none);
  ZC_REQUIRE(secondFixture.parsedModule != zc::none);
  ZC_REQUIRE(secondFixture.resolver != zc::none);
  ZC_IF_SOME(firstParsed, firstFixture.parsedModule) {
    ZC_IF_SOME(firstResolver, firstFixture.resolver) {
      auto firstResult =
          ModuleDependencyRequestDeriver::derive(firstFixture.module, firstParsed, firstResolver);
      ZC_REQUIRE(firstResult.is<zc::Vector<ModuleDependencyRequest>>());
      auto& firstRequests = firstResult.get<zc::Vector<ModuleDependencyRequest>>();
      ZC_REQUIRE(firstRequests.size() == 1);
      const auto firstKey = firstRequests[0].key().encode();
      const auto firstSiteStart = firstRequests[0].syntaxSite().span.byteStart();

      ZC_IF_SOME(secondParsed, secondFixture.parsedModule) {
        ZC_IF_SOME(secondResolver, secondFixture.resolver) {
          auto secondResult = ModuleDependencyRequestDeriver::derive(secondFixture.module,
                                                                     secondParsed, secondResolver);
          ZC_REQUIRE(secondResult.is<zc::Vector<ModuleDependencyRequest>>());
          auto& secondRequests = secondResult.get<zc::Vector<ModuleDependencyRequest>>();
          ZC_REQUIRE(secondRequests.size() == 1);
          ZC_EXPECT(firstKey.asPtr() == secondRequests[0].key().encode().asPtr());
          ZC_EXPECT(firstSiteStart != secondRequests[0].syntaxSite().span.byteStart());
          return;
        }
      }
    }
  }
  ZC_EXPECT(false);
}

ZC_TEST("ModuleDependencyRequestDeriver.ExactAliasPresenceChangesSemanticKey") {
  ParsedSource withoutAliasSource("module root;\nimport dep::value;\n"_zc);
  ParsedSource withAliasSource("module root;\nimport dep::value;\n"_zc);
  DerivationFixture withoutAliasFixture(withoutAliasSource);
  DerivationFixture withAliasFixture(withAliasSource, true);
  ZC_REQUIRE(withoutAliasFixture.parsedModule != zc::none);
  ZC_REQUIRE(withoutAliasFixture.resolver != zc::none);
  ZC_REQUIRE(withAliasFixture.parsedModule != zc::none);
  ZC_REQUIRE(withAliasFixture.resolver != zc::none);
  ZC_IF_SOME(withoutAliasParsed, withoutAliasFixture.parsedModule) {
    ZC_IF_SOME(withoutAliasResolver, withoutAliasFixture.resolver) {
      auto withoutAliasResult = ModuleDependencyRequestDeriver::derive(
          withoutAliasFixture.module, withoutAliasParsed, withoutAliasResolver);
      ZC_REQUIRE(withoutAliasResult.is<zc::Vector<ModuleDependencyRequest>>());
      auto& withoutAliasRequests = withoutAliasResult.get<zc::Vector<ModuleDependencyRequest>>();
      ZC_REQUIRE(withoutAliasRequests.size() == 1);
      ZC_EXPECT(withoutAliasRequests[0].key().dependencyAlias() == zc::none);

      ZC_IF_SOME(withAliasParsed, withAliasFixture.parsedModule) {
        ZC_IF_SOME(withAliasResolver, withAliasFixture.resolver) {
          auto withAliasResult = ModuleDependencyRequestDeriver::derive(
              withAliasFixture.module, withAliasParsed, withAliasResolver);
          ZC_REQUIRE(withAliasResult.is<zc::Vector<ModuleDependencyRequest>>());
          auto& withAliasRequests = withAliasResult.get<zc::Vector<ModuleDependencyRequest>>();
          ZC_REQUIRE(withAliasRequests.size() == 1);
          ZC_REQUIRE(withAliasRequests[0].key().dependencyAlias() != zc::none);
          ZC_IF_SOME(alias, withAliasRequests[0].key().dependencyAlias()) {
            ZC_EXPECT(alias == "dep"_zc);
          }
          ZC_EXPECT(withoutAliasRequests[0].key().encode().asPtr() !=
                    withAliasRequests[0].key().encode().asPtr());
          ZC_EXPECT(withAliasRequests[0].key().policy().encode().asPtr() ==
                    withAliasResolver.policy().encode().asPtr());
          return;
        }
      }
    }
  }
  ZC_EXPECT(false);
}

ZC_TEST("ModuleDependencyRequestDeriver.DerivesModuleAlias") {
  ParsedSource sourceFixture("module qux = alpha::beta::target;\n"_zc);
  DerivationFixture fixture(sourceFixture);
  ZC_REQUIRE(fixture.parsedModule != zc::none);
  ZC_REQUIRE(fixture.resolver != zc::none);
  ZC_IF_SOME(parsedModule, fixture.parsedModule) {
    ZC_IF_SOME(resolver, fixture.resolver) {
      auto result = ModuleDependencyRequestDeriver::derive(fixture.module, parsedModule, resolver);
      ZC_REQUIRE(result.is<zc::Vector<ModuleDependencyRequest>>());
      auto& requests = result.get<zc::Vector<ModuleDependencyRequest>>();
      ZC_REQUIRE(requests.size() == 1);
      ZC_EXPECT(requests[0].kind() == identity::ModuleDependencyKind::ModuleAlias);
      ZC_REQUIRE(requests[0].normalizedPath().size() == 3);
      ZC_EXPECT(requests[0].normalizedPath()[0].text() == "alpha"_zc);
      ZC_EXPECT(requests[0].normalizedPath()[1].text() == "beta"_zc);
      ZC_EXPECT(requests[0].normalizedPath()[2].text() == "target"_zc);
      return;
    }
  }
  ZC_EXPECT(false);
}

StructuralModuleResolver::FreezeResult freezeEnvironment(
    DerivationFixture& fixture, ParsedSource& sourceFixture,
    zc::Vector<ModuleSearchRoot>&& searchRoots,
    zc::Vector<GeneratedModuleSourceRevision>&& generated,
    const identity::Sha256Digest& sourceRevision) {
  auto snapshot = sourceFixture.snapshot();
  zc::Vector<ModuleSourceSnapshotRevision> snapshots;
  snapshots.add(ModuleSourceSnapshotRevision(snapshot.source().clone(), sourceRevision));
  zc::Vector<ModuleDependencyAliasRoot> aliases;
  zc::Vector<identity::ModuleKey> ancestryPath;
  ancestryPath.add(moduleKey());
  zc::Vector<RequesterModuleAncestryCandidate> ancestry;
  ancestry.add(RequesterModuleAncestryCandidate(moduleKey(), zc::mv(ancestryPath)));
  zc::Vector<StructuralModuleCatalogEntry> catalog;
  catalog.add(StructuralModuleCatalogEntry(moduleKey(), fixture.module, sourceKey()));
  return StructuralModuleResolver::freeze(
      fixture.context, fixture.registries,
      ModuleResolutionEnvironmentRecord(zc::mv(searchRoots), zc::mv(snapshots), zc::mv(generated),
                                        zc::mv(aliases), zc::mv(ancestry)),
      zc::mv(catalog));
}

zc::Vector<ModuleSearchRoot> workspaceSearchRoots(uint32_t leadingParents = 0) {
  zc::Vector<identity::CanonicalPathSegment> noRootSegments;
  zc::Vector<ModuleSearchRoot> roots;
  roots.add(ModuleSearchRoot::workspace(crateKey(), identity::CanonicalWorkspaceRelativePath::from(
                                                        leadingParents, zc::mv(noRootSegments))));
  return roots;
}

identity::BuildScriptProducerKey generatedProducer(zc::StringPtr seed) {
  auto value = identity::sha256(seed.asBytes());
  ZC_IF_SOME(digest, value) { return identity::BuildScriptProducerKey::from(digest); }
  ZC_FAIL_REQUIRE("generated producer digest fixture failed");
}

ZC_TEST("ModuleSearchRoot admits only an exact unversioned toolchain core root") {
  auto digest = identity::sha256("core-distribution"_zc.asBytes());
  ZC_REQUIRE(digest != zc::none);
  auto root = ModuleSearchRoot::toolchainCore(coreCrateKey(), ZC_REQUIRE_NONNULL(digest));
  ZC_REQUIRE(root != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(root).kind() == ModuleSearchRootKind::ToolchainCore);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(root).crate().unit().kind() ==
            identity::CompilationUnitKind::Toolchain);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(root).toolchainCoreDistributionDigest() ==
            ZC_REQUIRE_NONNULL(digest));

  identity::CanonicalEncoder encoder;
  ZC_REQUIRE_NONNULL(root).encode(encoder);
  auto encoded = encoder.finish();
  ZC_EXPECT(encoded[0] == static_cast<uint8_t>(ModuleSearchRootKind::ToolchainCore));
  identity::CanonicalDecoder decoder(encoded.asPtr());
  auto decoded = ModuleSearchRoot::decodeCanonical(decoder);
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(decoder.finished());
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded).kind() == ModuleSearchRootKind::ToolchainCore);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded).crate().encode().asPtr() ==
            ZC_REQUIRE_NONNULL(root).crate().encode().asPtr());
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded).toolchainCoreDistributionDigest() ==
            ZC_REQUIRE_NONNULL(digest));

  ZC_EXPECT(ModuleSearchRoot::toolchainCore(crateKey(), ZC_REQUIRE_NONNULL(digest)) == zc::none);
  ZC_EXPECT(ModuleSearchRoot::toolchainCore(coreCrateKey(), identity::Sha256Digest()) == zc::none);
  auto unknownTag = zc::heapArray<uint8_t>(encoded.asPtr());
  unknownTag[0] = 0xff;
  identity::CanonicalDecoder unknownDecoder(unknownTag.asPtr());
  ZC_EXPECT(ModuleSearchRoot::decodeCanonical(unknownDecoder) == zc::none);
}

ZC_TEST("StructuralModuleResolver.RejectsUnverifiedDiscoveryEnvironmentInputs") {
  ParsedSource sourceFixture("module root;\n"_zc);
  DerivationFixture fixture(sourceFixture);
  const auto snapshot = sourceFixture.snapshot();

  {
    auto roots = workspaceSearchRoots();
    auto duplicate = workspaceSearchRoots();
    roots.add(zc::mv(duplicate[0]));
    zc::Vector<GeneratedModuleSourceRevision> generated;
    auto result = freezeEnvironment(fixture, sourceFixture, zc::mv(roots), zc::mv(generated),
                                    snapshot.contentDigest());
    ZC_REQUIRE(result.is<ModuleResolutionInvariantFact>());
    ZC_EXPECT(result.get<ModuleResolutionInvariantFact>().kind ==
              ModuleResolutionInvariantKind::InvalidEnvironment);
  }

  {
    auto roots = workspaceSearchRoots(1);
    zc::Vector<GeneratedModuleSourceRevision> generated;
    auto result = freezeEnvironment(fixture, sourceFixture, zc::mv(roots), zc::mv(generated),
                                    snapshot.contentDigest());
    ZC_REQUIRE(result.is<ModuleResolutionInvariantFact>());
    ZC_EXPECT(result.get<ModuleResolutionInvariantFact>().kind ==
              ModuleResolutionInvariantKind::InvalidEnvironment);
  }

  {
    auto roots = workspaceSearchRoots();
    zc::Vector<identity::CanonicalPathSegment> noGeneratedRootSegments;
    roots.add(ModuleSearchRoot::generated(
        crateKey(), generatedProducer("missing-revision"_zc),
        identity::CanonicalRelativePath::from(zc::mv(noGeneratedRootSegments))));
    zc::Vector<GeneratedModuleSourceRevision> generated;
    auto result = freezeEnvironment(fixture, sourceFixture, zc::mv(roots), zc::mv(generated),
                                    snapshot.contentDigest());
    ZC_REQUIRE(result.is<ModuleResolutionInvariantFact>());
    ZC_EXPECT(result.get<ModuleResolutionInvariantFact>().kind ==
              ModuleResolutionInvariantKind::InvalidEnvironment);
  }

  {
    auto roots = workspaceSearchRoots();
    auto producer = generatedProducer("unused-revision"_zc);
    zc::Vector<GeneratedModuleSourceRevision> generated;
    generated.add(GeneratedModuleSourceRevision(producer, producer.digest()));
    auto result = freezeEnvironment(fixture, sourceFixture, zc::mv(roots), zc::mv(generated),
                                    snapshot.contentDigest());
    ZC_REQUIRE(result.is<ModuleResolutionInvariantFact>());
    ZC_EXPECT(result.get<ModuleResolutionInvariantFact>().kind ==
              ModuleResolutionInvariantKind::InvalidEnvironment);
  }

  {
    auto roots = workspaceSearchRoots();
    zc::Vector<GeneratedModuleSourceRevision> generated;
    auto result = freezeEnvironment(fixture, sourceFixture, zc::mv(roots), zc::mv(generated),
                                    identity::Sha256Digest());
    ZC_REQUIRE(result.is<ModuleResolutionInvariantFact>());
    ZC_EXPECT(result.get<ModuleResolutionInvariantFact>().kind ==
              ModuleResolutionInvariantKind::InvalidEnvironment);
  }
}

ZC_TEST("StructuralModuleResolver.RejectsAncestryEndingAtInactiveRoot") {
  ParsedSource sourceFixture("module nested;\n"_zc);
  const auto snapshot = sourceFixture.snapshot();
  identity::SemanticContextFactory factory;
  const auto context = requireContext(factory);
  auto registriesResult = identity::SemanticIdentityRegistrySet::create(factory, context);
  ZC_REQUIRE(registriesResult != zc::none);
  ZC_IF_SOME(registries, registriesResult) {
    ZC_REQUIRE(registries.collectCompilationUnit(identity::CompilationUnitIdentity::userPackage(
                   packageKey())) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeCompilationUnits() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectCrate(crateKey()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeCrates() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectSourceFile(snapshot.clone()) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeSourceFiles() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectModule(nestedModuleKey()) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeModules() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeStableIdentities() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeGenericParameters() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeCallableParameters() == identity::FrozenRegistryFailure::None);

    auto moduleResult = registries.modules().find(nestedModuleKey());
    ZC_REQUIRE(moduleResult != zc::none);
    ZC_IF_SOME(module, moduleResult) {
      zc::Vector<ModuleSourceSnapshotRevision> snapshots;
      snapshots.add(
          ModuleSourceSnapshotRevision(snapshot.source().clone(), snapshot.contentDigest()));
      zc::Vector<GeneratedModuleSourceRevision> generated;
      zc::Vector<ModuleDependencyAliasRoot> aliases;
      zc::Vector<identity::ModuleKey> ancestryPath;
      ancestryPath.add(nestedModuleKey());
      ancestryPath.add(inactiveRootModuleKey());
      zc::Vector<RequesterModuleAncestryCandidate> ancestry;
      ancestry.add(RequesterModuleAncestryCandidate(nestedModuleKey(), zc::mv(ancestryPath)));
      zc::Vector<StructuralModuleCatalogEntry> catalog;
      catalog.add(StructuralModuleCatalogEntry(nestedModuleKey(), module, sourceKey()));

      auto result = StructuralModuleResolver::freeze(
          context, registries,
          ModuleResolutionEnvironmentRecord(workspaceSearchRoots(), zc::mv(snapshots),
                                            zc::mv(generated), zc::mv(aliases), zc::mv(ancestry)),
          zc::mv(catalog));
      ZC_REQUIRE(result.is<ModuleResolutionInvariantFact>());
      ZC_EXPECT(result.get<ModuleResolutionInvariantFact>().kind ==
                ModuleResolutionInvariantKind::InvalidEnvironment);
      return;
    }
  }
  ZC_EXPECT(false);
}

}  // namespace
}  // namespace zomlang::compiler::binder
