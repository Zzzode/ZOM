// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zc/core/encoding.h"
#include "zc/core/map.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/thread-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/binder/binding-input.h"
#include "zomlang/compiler/binder/module-dependency-requests.h"
#include "zomlang/compiler/diagnostics/diagnostic-fact-buffer.h"
#include "zomlang/compiler/driver/core-library-query-provider.h"
#include "zomlang/compiler/driver/module-graph-query.h"
#include "zomlang/compiler/driver/package/package-compilation-request.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/source-snapshot.h"
#include "zomlang/compiler/ir/target-registry.h"
#include "zomlang/compiler/parser/parser.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/tests/unittests/compiler/binder/parsed-module-query-test-fixture.h"
#include "zomlang/tests/unittests/compiler/driver/core-library-test-fixture.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::driver::module_graph_query {
namespace {

using tests::test_identity_detail::coreCrate;
using tests::test_identity_detail::crate;
using tests::test_identity_detail::digest;
using tests::test_identity_detail::package;
using tests::test_identity_detail::scalar;

identity::ModuleKey module(const identity::CrateKey& owner, zc::StringPtr first,
                           zc::StringPtr second = nullptr) {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(scalar<identity::ModulePathSegment>(first));
  if (second != nullptr) { path.add(scalar<identity::ModulePathSegment>(second)); }
  auto result = identity::ModuleKey::from(owner.clone(), zc::mv(path));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

bool sameCrate(const identity::CrateKey& left, const identity::CrateKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

bool sameModule(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

zc::Vector<identity::ModulePathSegment> cloneModulePath(
    zc::ArrayPtr<const identity::ModulePathSegment> path) {
  zc::Vector<identity::ModulePathSegment> result(path.size());
  for (const auto& segment : path) { result.add(segment.clone()); }
  return result;
}

zc::Vector<identity::ModulePathSegment> modulePath(zc::StringPtr first,
                                                   zc::StringPtr second = nullptr,
                                                   zc::StringPtr third = nullptr) {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(scalar<identity::ModulePathSegment>(first));
  if (second != nullptr) { path.add(scalar<identity::ModulePathSegment>(second)); }
  if (third != nullptr) { path.add(scalar<identity::ModulePathSegment>(third)); }
  return path;
}

identity::SourceFileKey localSource(const identity::CrateKey& owner, zc::StringPtr name) {
  zc::Vector<identity::CanonicalPathSegment> path;
  path.add(scalar<identity::CanonicalPathSegment>(name));
  return identity::SourceFileKey::from(
      owner.clone(), identity::SourceOriginKey::localFile(
                         identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(path))));
}

identity::SourceFileKey coreSource(const identity::CrateKey& owner, zc::StringPtr name) {
  zc::Vector<identity::CanonicalPathSegment> path;
  if (name != "core.zom"_zc) { path.add(scalar<identity::CanonicalPathSegment>("core"_zc)); }
  path.add(scalar<identity::CanonicalPathSegment>(name));
  return identity::SourceFileKey::from(
      owner.clone(),
      identity::SourceOriginKey::coreFile(identity::ToolchainUnitKey::core(),
                                          identity::CanonicalRelativePath::from(zc::mv(path))));
}

zc::Vector<identity::ModuleKey> ancestry(identity::ModuleKey&& requester,
                                         zc::Maybe<identity::ModuleKey>&& parent = zc::none) {
  zc::Vector<identity::ModuleKey> result;
  result.add(zc::mv(requester));
  ZC_IF_SOME(value, parent) { result.add(zc::mv(value)); }
  return result;
}

incremental_binding_query::CompilationRootSetQueryKey contextRoots(const identity::CrateKey& core) {
  zc::Vector<incremental_binding_query::CompilationRootKey> roots;
  auto user = incremental_binding_query::CompilationRootKey::userPackage(package());
  auto toolchain = incremental_binding_query::CompilationRootKey::toolchainCore(core);
  roots.add(zc::mv(ZC_REQUIRE_NONNULL(user)));
  roots.add(zc::mv(ZC_REQUIRE_NONNULL(toolchain)));
  auto result = incremental_binding_query::CompilationRootSetQueryKey::from(zc::mv(roots));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

package::RegisteredTargetProfileName profileName() {
  auto value = package::RegisteredTargetProfileName::from("module-graph-test"_zc);
  return zc::mv(ZC_REQUIRE_NONNULL(value));
}

ir::TargetRegistrySnapshot targetRegistry() {
  zc::Vector<ir::CanonicalTargetFeature> backendFeatures;
  auto specification = ir::CanonicalTargetSpec::from(
      "x-v-o-e"_zc, "e-p:64:64"_zc, "generic"_zc, zc::mv(backendFeatures), "a"_zc,
      ir::BackendPanicStrategy::Unwind, ir::ObjectFormat::Elf);
  ZC_REQUIRE(specification != zc::none);
  zc::Vector<identity::TargetFeatureName> semanticFeatures;
  zc::Vector<ir::CanonicalTargetSpec> specifications;
  specifications.add(zc::mv(ZC_REQUIRE_NONNULL(specification)));
  auto profile =
      ir::RegisteredTargetProfileRecord::from(profileName(), tests::test_identity_detail::target(),
                                              zc::mv(semanticFeatures), zc::mv(specifications));
  ZC_REQUIRE(profile != zc::none);
  zc::Vector<ir::RegisteredTargetProfileRecord> profiles;
  profiles.add(zc::mv(ZC_REQUIRE_NONNULL(profile)));
  auto registry = ir::TargetRegistrySnapshot::from(profileName(), zc::mv(profiles));
  return zc::mv(ZC_REQUIRE_NONNULL(registry));
}

package::RegisteredTargetSelection targetSelection(const ir::TargetRegistrySnapshot& registry) {
  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  auto selected =
      ZC_REQUIRE_NONNULL(service).select(zc::none, package::PackagePanicStrategy::Unwind);
  return zc::mv(ZC_REQUIRE_NONNULL(selected));
}

package::VerifiedPackageCompilationRequest packageRequest(
    const ir::TargetRegistrySnapshot& registry) {
  zc::Vector<identity::CanonicalPathSegment> path;
  path.add(scalar<identity::CanonicalPathSegment>("src"_zc));
  path.add(scalar<identity::CanonicalPathSegment>("test.zom"_zc));
  zc::Vector<package::VerifiedCompilationRoot> roots;
  roots.add(package::VerifiedCompilationRoot::from(
      package(), identity::CrateTargetKind::Library, scalar<identity::TargetName>("test"_zc), 2026,
      false, identity::CanonicalRelativePath::from(zc::mv(path))));
  auto request = package::VerifiedPackageCompilationRequest::from(
      zc::mv(roots), targetSelection(registry), targetSelection(registry),
      package::SelectedLanguageOptions{}, package::PackageLockMode::PreferLocked);
  return zc::mv(ZC_REQUIRE_NONNULL(request));
}

zc::Array<uint8_t> encodedTargetSelection() {
  identity::CanonicalEncoder encoder;
  encoder.encodeDigest(digest(0x71));
  encoder.encodeByteString("module-graph-test"_zc.asBytes());
  tests::test_identity_detail::target().encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(package::PackagePanicStrategy::Unwind));
  return encoder.finish();
}

identity::source_query::CanonicalCompilationOptions compilationOptions() {
  auto host = encodedTargetSelection();
  auto targetValue = encodedTargetSelection();
  auto options = identity::source_query::CanonicalCompilationOptions::fromCanonicalSelections(
      zc::mv(host), zc::mv(targetValue), true, false, true);
  return zc::mv(ZC_REQUIRE_NONNULL(options));
}

struct ParsedSource final {
  explicit ParsedSource(zc::ArrayPtr<const uint8_t> text)
      : sources(zc::heap<source::SourceManager>()),
        buffer(sources->addMemBufferCopy(text, "module.zom")) {
    diagnostics::DiagnosticFactBuffer facts(*sources, buffer);
    parser::Parser parser(*sources, facts, options, strings, buffer);
    auto parsed = parser.parse();
    ZC_REQUIRE(parsed != zc::none);
    tree = zc::mv(ZC_REQUIRE_NONNULL(parsed));
    ZC_REQUIRE(!facts.hasErrors());
    tokens = parser.takeTokenSnapshot();
    ZC_REQUIRE(tokens != zc::none);
  }

  identity::ImmutableSourceSnapshot snapshot(const identity::SourceFileKey& source) const {
    auto value = identity::ImmutableSourceSnapshot::from(
        source.clone(), zc::heapArray<uint8_t>(sources->getEntireTextForBuffer(buffer)));
    return zc::mv(ZC_REQUIRE_NONNULL(value));
  }

  binder::VerifiedParsedModule verify(identity::SemanticContextBrand context,
                                      const identity::SemanticIdentityRegistrySet& registries,
                                      const identity::SourceFileKey& source) {
    auto retained = zc::mv(ZC_REQUIRE_NONNULL(tokens));
    return binder::test::requireVerifiedParsedSource(
        context, registries, snapshot(source), *sources, buffer, zc::mv(retained), zc::mv(tree));
  }

  zc::Own<source::SourceManager> sources;
  basic::LangOptions options;
  basic::StringPool strings;
  source::BufferId buffer;
  ast::Tree tree;
  zc::Maybe<parser::ParsedTokenSnapshot> tokens;
};

zc::Maybe<VerifiedModuleGraphInputTransaction> preparedTransaction(
    const VerifiedModuleGraphInputLedger& prior, bool includeMarker, bool withCycle = false,
    bool omitCoreProjection = false) {
  auto userCrate = crate();
  auto core = coreCrate();
  auto userModule = module(userCrate, "test"_zc);
  auto coreRoot = module(core, "core"_zc);
  auto prelude = module(core, "core"_zc, "prelude"_zc);
  auto marker = module(core, "core"_zc, "marker"_zc);
  auto userFile = localSource(userCrate, "test.zom"_zc);
  auto coreFile = coreSource(core, "core.zom"_zc);
  auto preludeFile = coreSource(core, "prelude.zom"_zc);
  auto markerFile = coreSource(core, "marker.zom"_zc);
  auto distribution = core_library_test::admittedCoreDistribution();
  ZC_REQUIRE(distribution.snapshots().size() == 3);

  auto options = compilationOptions();
  zc::Vector<identity::CrateKey> consumers;
  consumers.add(userCrate.clone());
  auto preparedCore = core_library_query::VerifiedCoreDistributionInputTransaction::prepare(
      distribution, options, consumers.asPtr());
  ZC_REQUIRE(preparedCore != zc::none);
  auto coreInputs = zc::mv(ZC_REQUIRE_NONNULL(preparedCore));
  ZC_REQUIRE(coreInputs.projections().size() == 1);
  ZC_REQUIRE(coreInputs.projections()[0].crate().encode().asPtr() == core.encode().asPtr());

  ParsedSource userParsedSource("module test;\n"_zc.asBytes());
  ParsedSource coreParsedSource(distribution.snapshots()[0].bytes());
  ParsedSource markerParsedSource(distribution.snapshots()[1].bytes());
  ParsedSource preludeParsedSource(distribution.snapshots()[2].bytes());
  identity::SemanticContextFactory contextFactory;
  auto context = contextFactory.issue();
  ZC_REQUIRE(context != zc::none);
  auto registryResult =
      identity::SemanticIdentityRegistrySet::create(contextFactory, ZC_REQUIRE_NONNULL(context));
  ZC_REQUIRE(registryResult != zc::none);
  auto registries = zc::mv(ZC_REQUIRE_NONNULL(registryResult));
  ZC_REQUIRE(registries.collectCompilationUnit(identity::CompilationUnitIdentity::userPackage(
                 package())) == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectCompilationUnit(identity::CompilationUnitIdentity::toolchain(
                 identity::ToolchainUnitKey::core())) == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeCompilationUnits() == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectCrate(userCrate.clone()) == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectCrate(core.clone()) == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeCrates() == identity::FrozenRegistryFailure::None);
  auto userSnapshot = userParsedSource.snapshot(userFile);
  auto coreSnapshot = coreParsedSource.snapshot(coreFile);
  auto markerSnapshot = markerParsedSource.snapshot(markerFile);
  auto preludeSnapshot = preludeParsedSource.snapshot(preludeFile);
  ZC_REQUIRE(registries.collectSourceFile(userSnapshot.clone()) ==
             identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectSourceFile(coreSnapshot.clone()) ==
             identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectSourceFile(markerSnapshot.clone()) ==
             identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectSourceFile(preludeSnapshot.clone()) ==
             identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeSourceFiles() == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectModule(userModule.clone()) == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectModule(coreRoot.clone()) == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectModule(marker.clone()) == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectModule(prelude.clone()) == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeModules() == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeStableIdentities() == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeGenericParameters() == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeCallableParameters() == identity::FrozenRegistryFailure::None);

  zc::Vector<binder::VerifiedParsedModule> parsedModules(4);
  parsedModules.add(userParsedSource.verify(ZC_REQUIRE_NONNULL(context), registries, userFile));
  parsedModules.add(coreParsedSource.verify(ZC_REQUIRE_NONNULL(context), registries, coreFile));
  parsedModules.add(markerParsedSource.verify(ZC_REQUIRE_NONNULL(context), registries, markerFile));
  parsedModules.add(
      preludeParsedSource.verify(ZC_REQUIRE_NONNULL(context), registries, preludeFile));
  const identity::ModuleKey* moduleKeys[] = {&userModule, &coreRoot, &marker, &prelude};
  const identity::SourceFileKey* sourceKeys[] = {&userFile, &coreFile, &markerFile, &preludeFile};
  zc::Vector<binder::ParsedModuleGraphInput> parsedInputs(4);
  for (size_t index = 0; index < 4; ++index) {
    auto handle = registries.modules().find(*moduleKeys[index]);
    ZC_REQUIRE(handle != zc::none);
    parsedInputs.add(
        binder::ParsedModuleGraphInput{ZC_REQUIRE_NONNULL(handle), parsedModules[index]});
  }

  zc::Vector<binder::ModuleSearchRoot> environmentRoots;
  zc::Vector<identity::CanonicalPathSegment> workspacePath;
  environmentRoots.add(binder::ModuleSearchRoot::workspace(
      userCrate.clone(), identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(workspacePath))));
  for (const auto& root : coreInputs.projections()[0].searchRoots().roots()) {
    environmentRoots.add(root.clone());
  }
  zc::Vector<binder::ModuleSourceSnapshotRevision> sourceRevisions;
  for (const auto& snapshot : registries.sourceSnapshots()) {
    sourceRevisions.add(
        binder::ModuleSourceSnapshotRevision(snapshot.source().clone(), snapshot.contentDigest()));
  }
  zc::Vector<binder::GeneratedModuleSourceRevision> generatedRevisions;
  zc::Vector<binder::ModuleDependencyAliasRoot> resolverAliases;
  resolverAliases.add(binder::ModuleDependencyAliasRoot(
      userCrate.clone(), scalar<identity::DependencyAlias>("core"_zc), coreRoot.clone()));
  zc::Vector<binder::RequesterModuleAncestryCandidate> ancestryCandidates;
  const auto addAncestry = [&](const identity::ModuleKey& requester,
                               zc::Maybe<const identity::ModuleKey&> parent) {
    auto chain = ancestry(requester.clone());
    ZC_IF_SOME(value, parent) { chain.add(value.clone()); }
    ancestryCandidates.add(
        binder::RequesterModuleAncestryCandidate(requester.clone(), zc::mv(chain)));
  };
  addAncestry(userModule, zc::none);
  addAncestry(coreRoot, zc::none);
  addAncestry(marker, coreRoot);
  addAncestry(prelude, coreRoot);
  zc::Vector<binder::StructuralModuleCatalogEntry> resolverCatalog;
  for (size_t index = 0; index < 4; ++index) {
    auto handle = registries.modules().find(*moduleKeys[index]);
    resolverCatalog.add(binder::StructuralModuleCatalogEntry(
        moduleKeys[index]->clone(), ZC_REQUIRE_NONNULL(handle), sourceKeys[index]->clone()));
  }
  auto frozenResolver = binder::StructuralModuleResolver::freeze(
      ZC_REQUIRE_NONNULL(context), registries,
      binder::ModuleResolutionEnvironmentRecord(zc::mv(environmentRoots), zc::mv(sourceRevisions),
                                                zc::mv(generatedRevisions), zc::mv(resolverAliases),
                                                zc::mv(ancestryCandidates)),
      zc::mv(resolverCatalog));
  ZC_REQUIRE(frozenResolver.is<binder::StructuralModuleResolver>());
  auto resolver = zc::mv(frozenResolver.get<binder::StructuralModuleResolver>());

  zc::Vector<binder::ModuleDependencyRequest> requests;
  zc::Vector<DetachedModuleDependencySiteSet> sites;
  for (size_t index = 0; index < parsedInputs.size(); ++index) {
    auto derived = binder::ModuleDependencyRequestDeriver::derive(
        parsedInputs[index].module, parsedInputs[index].parsedModule, resolver);
    ZC_REQUIRE(derived.is<zc::Vector<binder::ModuleDependencyRequest>>());
    auto moduleRequests = zc::mv(derived.get<zc::Vector<binder::ModuleDependencyRequest>>());
    zc::Vector<DetachedModuleDependencySite> detached;
    for (auto& request : moduleRequests) {
      DetachedModuleDependencySiteKind kind = DetachedModuleDependencySiteKind::Import;
      if (request.kind() == identity::ModuleDependencyKind::ForeignReexport) {
        kind = DetachedModuleDependencySiteKind::ForeignReexport;
      } else if (request.kind() == identity::ModuleDependencyKind::ModuleAlias) {
        kind = DetachedModuleDependencySiteKind::ModuleAlias;
      }
      for (const auto& syntax : request.syntaxSites()) {
        auto site = DetachedModuleDependencySite::from(
            kind, cloneModulePath(request.normalizedPath()), syntax.schemaPreorderOrdinal);
        detached.add(zc::mv(ZC_REQUIRE_NONNULL(site)));
      }
      requests.add(zc::mv(request));
    }
    if (withCycle && sameModule(*moduleKeys[index], coreRoot)) {
      zc::Vector<identity::ModulePathSegment> inventedPath;
      inventedPath.add(scalar<identity::ModulePathSegment>("core"_zc));
      inventedPath.add(scalar<identity::ModulePathSegment>("prelude"_zc));
      auto invented = DetachedModuleDependencySite::from(DetachedModuleDependencySiteKind::Import,
                                                         zc::mv(inventedPath), 999);
      detached.add(zc::mv(ZC_REQUIRE_NONNULL(invented)));
    }
    auto siteSet = DetachedModuleDependencySiteSet::from(
        moduleKeys[index]->clone(), sourceKeys[index]->clone(),
        parsedInputs[index].parsedModule.contentDigest(), zc::mv(detached));
    if (includeMarker || !sameModule(*moduleKeys[index], marker)) {
      sites.add(zc::mv(ZC_REQUIRE_NONNULL(siteSet)));
    }
  }

  zc::Vector<SelectedModuleRecord> userEntries;
  userEntries.add(SelectedModuleRecord(userModule.clone(), userFile.clone()));
  auto userCatalog = SelectedModuleCatalog::from(userCrate.clone(), zc::mv(userEntries));
  ZC_REQUIRE(userCatalog != zc::none);

  zc::Vector<SelectedModuleRecord> coreEntries;
  coreEntries.add(SelectedModuleRecord(coreRoot.clone(), coreFile.clone()));
  coreEntries.add(SelectedModuleRecord(prelude.clone(), preludeFile.clone()));
  if (includeMarker) { coreEntries.add(SelectedModuleRecord(marker.clone(), markerFile.clone())); }
  auto coreCatalog = SelectedModuleCatalog::from(core.clone(), zc::mv(coreEntries));
  ZC_REQUIRE(coreCatalog != zc::none);

  zc::Vector<SelectedModuleCatalog> catalogs;
  catalogs.add(zc::mv(ZC_REQUIRE_NONNULL(userCatalog)));
  catalogs.add(zc::mv(ZC_REQUIRE_NONNULL(coreCatalog)));

  zc::Vector<identity::RequesterModuleAncestry> ancestries;
  for (const auto& authorityAncestry : resolver.requesterAncestryInputs()) {
    if (includeMarker || !sameModule(authorityAncestry.requester(), marker)) {
      ancestries.add(authorityAncestry.clone());
    }
  }

  zc::TreeMap<zc::String, incremental_module_resolution_query::CanonicalModuleCatalogBucket>
      bucketMap;
  const auto addBucket = [&](const identity::CrateKey& owner,
                             zc::ArrayPtr<const identity::ModulePathSegment> path) {
    auto bucket = resolver.catalogPathBucketInput(owner, path);
    ZC_REQUIRE(bucket != zc::none);
    auto canonical =
        incremental_module_resolution_query::CanonicalModuleCatalogBucket::fromVerified(
            ZC_REQUIRE_NONNULL(bucket));
    auto encoded = canonical.key().encode();
    auto key = zc::encodeHex(encoded.asPtr());
    if (bucketMap.find(key) == zc::none) { bucketMap.insert(zc::mv(key), zc::mv(canonical)); }
  };
  for (const auto& entry : resolver.catalog()) { addBucket(entry.key.crate(), entry.key.path()); }
  for (const auto& request : requests) {
    zc::Maybe<const identity::RequesterModuleAncestry&> selectedAncestry;
    for (const auto& candidate : resolver.requesterAncestryInputs()) {
      if (sameModule(candidate.requester(), request.key().requester())) {
        selectedAncestry = candidate;
      }
    }
    ZC_REQUIRE(selectedAncestry != zc::none);
    for (const auto& ancestor : ZC_REQUIRE_NONNULL(selectedAncestry).ancestry()) {
      auto path = cloneModulePath(ancestor.path());
      for (const auto& segment : request.normalizedPath()) { path.add(segment.clone()); }
      addBucket(request.key().requester().crate(), path.asPtr());
    }
    addBucket(request.key().requester().crate(), request.normalizedPath());
    ZC_IF_SOME(alias, request.key().dependencyAlias()) {
      for (const auto& root : resolver.dependencyAliasRootInputs()) {
        if (!sameCrate(root.requester, request.key().requester().crate()) ||
            root.alias.text() != alias) {
          continue;
        }
        auto path = cloneModulePath(root.target.path());
        for (size_t index = 1; index < request.normalizedPath().size(); ++index) {
          path.add(request.normalizedPath()[index].clone());
        }
        addBucket(root.target.crate(), path.asPtr());
      }
    }
  }
  addBucket(core, prelude.path());
  zc::Vector<incremental_module_resolution_query::CanonicalModuleCatalogBucket> buckets(
      bucketMap.size());
  for (auto& entry : bucketMap) { buckets.add(zc::mv(entry.value)); }

  zc::Vector<incremental_module_resolution_query::CanonicalModuleSearchRoots> searchRoots;
  auto projectedUserRoots =
      incremental_module_resolution_query::CanonicalModuleSearchRoots::fromVerified(
          userCrate, resolver.searchRootInputs());
  ZC_REQUIRE(projectedUserRoots != zc::none);
  searchRoots.add(zc::mv(ZC_REQUIRE_NONNULL(projectedUserRoots)));
  searchRoots.add(coreInputs.projections()[0].searchRoots().clone());
  zc::Vector<ConfiguredDependencyAlias> aliases;
  zc::TreeMap<zc::String, ConfiguredDependencyAlias> aliasMap;
  for (const auto& request : requests) {
    auto alias = identity::DependencyAlias::fromCanonical(request.normalizedPath().front().text());
    if (alias == zc::none) { continue; }
    auto aliasKey = incremental_module_resolution_query::DependencyAliasRootQueryKey::from(
        request.key().requester().crate().clone(), ZC_REQUIRE_NONNULL(alias).clone());
    ZC_REQUIRE(aliasKey != zc::none);
    auto target = incremental_module_resolution_query::ExplicitModuleTarget::absent();
    for (const auto& root : resolver.dependencyAliasRootInputs()) {
      if (sameCrate(root.requester, request.key().requester().crate()) &&
          root.alias.text() == ZC_REQUIRE_NONNULL(alias).text()) {
        target =
            incremental_module_resolution_query::ExplicitModuleTarget::present(root.target.clone());
      }
    }
    auto encoded = ZC_REQUIRE_NONNULL(aliasKey).encode();
    auto key = zc::encodeHex(encoded.asPtr());
    if (aliasMap.find(key) == zc::none) {
      aliasMap.insert(zc::mv(key), ConfiguredDependencyAlias{zc::mv(ZC_REQUIRE_NONNULL(aliasKey)),
                                                             zc::mv(target)});
    }
  }
  for (auto& entry : aliasMap) { aliases.add(zc::mv(entry.value)); }
  zc::Vector<ConfiguredCratePrelude> preludes;
  preludes.add(ConfiguredCratePrelude{
      userCrate.clone(),
      incremental_module_resolution_query::ExplicitModuleTarget::present(prelude.clone())});
  preludes.add(ConfiguredCratePrelude{
      core.clone(), incremental_module_resolution_query::ExplicitModuleTarget::absent()});
  zc::Vector<identity::CrateKey> projectedCore;
  if (!omitCoreProjection) { projectedCore.add(core.clone()); }

  auto registry = targetRegistry();
  auto request = packageRequest(registry);
  const ModuleGraphInputTransactionAuthority authority{request, coreInputs, resolver, registries,
                                                       parsedInputs.asPtr()};
  return VerifiedModuleGraphInputTransaction::prepare(
      authority, contextRoots(core), zc::mv(projectedCore), zc::mv(catalogs), zc::mv(sites),
      zc::mv(ancestries), zc::mv(buckets), zc::mv(searchRoots), zc::mv(aliases), zc::mv(preludes),
      prior);
}

query::QueryDatabase database(basic::ThreadPool& scheduler) {
  query::QueryDatabase result(scheduler);
  ZC_REQUIRE(registerModuleGraphQueries(result));
  ZC_REQUIRE(
      incremental_module_resolution_query::registerIncrementalModuleResolutionQueries(result));
  ZC_REQUIRE(result.registerInputKind<incremental_binding_query::UserPackageActiveSourcesInput>() !=
             zc::none);
  ZC_REQUIRE(result.registerDerivedKind<incremental_binding_query::ActiveSourcesQuery>() !=
             zc::none);
  ZC_REQUIRE(result.registerInputKind<identity::source_query::SourceSnapshotInput>() != zc::none);
  ZC_REQUIRE(core_library_query::registerCoreLibraryQueryProvider(result));
  ZC_REQUIRE(result.registerDerivedKind<incremental_binding_query::ActiveCratesQuery>() !=
             zc::none);
  ZC_REQUIRE(registerStableModuleGraphQueries(result));
  return result;
}

void stageSource(query::InputTransaction& transaction, const identity::SourceFileKey& source) {
  auto immutable = identity::ImmutableSourceSnapshot::from(
      source.clone(), zc::heapArray<uint8_t>("module test;\n"_zc.asBytes()));
  ZC_REQUIRE(immutable != zc::none);
  auto key = identity::source_query::StableSourceQueryKey::fromVerified(source);
  auto snapshot =
      identity::source_query::CanonicalSourceSnapshot::fromVerified(ZC_REQUIRE_NONNULL(immutable));
  ZC_REQUIRE(key != zc::none);
  ZC_REQUIRE(snapshot != zc::none);
  ZC_REQUIRE(transaction.set<identity::source_query::SourceSnapshotInput>(
      ZC_REQUIRE_NONNULL(key), ZC_REQUIRE_NONNULL(snapshot)));
}

void stageUserActiveSource(query::InputTransaction& transaction,
                           const identity::SourceFileKey& source) {
  auto key = incremental_binding_query::StableCrateQueryKey::fromVerified(source.crate());
  auto stable = identity::source_query::StableSourceQueryKey::fromVerified(source);
  ZC_REQUIRE(key != zc::none);
  ZC_REQUIRE(stable != zc::none);
  zc::Vector<identity::source_query::StableSourceQueryKey> sources;
  sources.add(zc::mv(ZC_REQUIRE_NONNULL(stable)));
  auto value = incremental_binding_query::CanonicalSourceSet::from(zc::mv(sources));
  ZC_REQUIRE(value != zc::none);
  ZC_REQUIRE(transaction.set<incremental_binding_query::UserPackageActiveSourcesInput>(
      ZC_REQUIRE_NONNULL(key), ZC_REQUIRE_NONNULL(value)));
}

void stageCoreDistribution(query::InputTransaction& transaction) {
  auto distribution = core_library_test::admittedCoreDistribution();
  auto record = source::core::CoreDistributionInputRecord::from(
      distribution.record().clone(), distribution.distributionDigest(),
      distribution.policyTemplate().clone());
  ZC_REQUIRE(record != zc::none);
  ZC_REQUIRE(transaction.set<core_library_query::CoreDistributionInput>(
      identity::ToolchainUnitKey::core(), ZC_REQUIRE_NONNULL(record)));
  auto core = coreCrate();
  for (const auto& admitted : distribution.snapshots()) {
    auto source = identity::SourceFileKey::from(
        core.clone(), identity::SourceOriginKey::coreFile(identity::ToolchainUnitKey::core(),
                                                          admitted.path().clone()));
    auto immutable = identity::ImmutableSourceSnapshot::from(
        source.clone(), zc::heapArray<uint8_t>(admitted.bytes()));
    auto key = identity::source_query::StableSourceQueryKey::fromVerified(source);
    auto snapshot = identity::source_query::CanonicalSourceSnapshot::fromVerified(
        ZC_REQUIRE_NONNULL(immutable));
    ZC_REQUIRE(key != zc::none);
    ZC_REQUIRE(snapshot != zc::none);
    ZC_REQUIRE(transaction.set<identity::source_query::SourceSnapshotInput>(
        ZC_REQUIRE_NONNULL(key), ZC_REQUIRE_NONNULL(snapshot)));
  }
}

void stageBucket(query::InputTransaction& transaction, const identity::CrateKey& owner,
                 zc::Vector<identity::ModulePathSegment>&& path,
                 zc::Maybe<identity::ModuleKey>&& selected = zc::none) {
  auto key = identity::ModuleCatalogPathBucketKey::from(owner.clone(), zc::mv(path));
  ZC_REQUIRE(key != zc::none);
  auto bucket = selected == zc::none
                    ? identity::ModuleCatalogPathBucket::absent(ZC_REQUIRE_NONNULL(key).clone())
                    : identity::ModuleCatalogPathBucket::present(
                          ZC_REQUIRE_NONNULL(key).clone(), zc::mv(ZC_REQUIRE_NONNULL(selected)));
  ZC_REQUIRE(bucket != zc::none);
  auto canonical = incremental_module_resolution_query::CanonicalModuleCatalogBucket::fromVerified(
      ZC_REQUIRE_NONNULL(bucket));
  ZC_REQUIRE(transaction.set<incremental_module_resolution_query::ModuleCatalogPathBucketInput>(
      ZC_REQUIRE_NONNULL(key), canonical));
}

}  // namespace

ZC_TEST("Selected module catalog has an exact canonical codec") {
  auto owner = crate();
  auto selectedModule = module(owner, "test"_zc);
  auto selectedSource = localSource(owner, "test.zom"_zc);
  zc::Vector<SelectedModuleRecord> entries;
  entries.add(SelectedModuleRecord(selectedModule.clone(), selectedSource.clone()));
  auto catalog = SelectedModuleCatalog::from(owner.clone(), zc::mv(entries));
  ZC_REQUIRE(catalog != zc::none);

  auto encoded = SelectedModuleCatalogInput::encodeValue(ZC_REQUIRE_NONNULL(catalog));
  auto decoded = SelectedModuleCatalogInput::decodeValue(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded).encodeCanonical().asPtr() == encoded.asPtr());
  ZC_EXPECT(SelectedModuleCatalogInput::decodeValue(encoded.slice(0, encoded.size() - 1)) ==
            zc::none);

  auto foreign = coreCrate();
  zc::Vector<SelectedModuleRecord> foreignEntries;
  foreignEntries.add(SelectedModuleRecord(selectedModule.clone(), selectedSource.clone()));
  ZC_EXPECT(SelectedModuleCatalog::from(zc::mv(foreign), zc::mv(foreignEntries)) == zc::none);
}

ZC_TEST("Detached dependency sites reject duplicate stable ordinals") {
  auto owner = crate();
  auto selectedModule = module(owner, "test"_zc);
  auto selectedSource = localSource(owner, "test.zom"_zc);
  zc::Vector<identity::ModulePathSegment> firstPath;
  firstPath.add(scalar<identity::ModulePathSegment>("one"_zc));
  zc::Vector<identity::ModulePathSegment> secondPath;
  secondPath.add(scalar<identity::ModulePathSegment>("two"_zc));
  auto first = DetachedModuleDependencySite::from(DetachedModuleDependencySiteKind::Import,
                                                  zc::mv(firstPath), 7);
  auto second = DetachedModuleDependencySite::from(DetachedModuleDependencySiteKind::ModuleAlias,
                                                   zc::mv(secondPath), 7);
  ZC_REQUIRE(first != zc::none);
  ZC_REQUIRE(second != zc::none);
  zc::Vector<DetachedModuleDependencySite> sites;
  sites.add(zc::mv(ZC_REQUIRE_NONNULL(first)));
  sites.add(zc::mv(ZC_REQUIRE_NONNULL(second)));
  ZC_EXPECT(DetachedModuleDependencySiteSet::from(zc::mv(selectedModule), zc::mv(selectedSource),
                                                  digest(0x62), zc::mv(sites)) == zc::none);
}

ZC_TEST("Module graph input transaction commits its complete authority exactly once") {
  basic::ThreadPool scheduler(2);
  auto queries = database(scheduler);
  auto empty = VerifiedModuleGraphInputLedger::empty();
  auto first = preparedTransaction(empty, true);
  ZC_REQUIRE(first != zc::none);
  ZC_REQUIRE(ZC_REQUIRE_NONNULL(first).commit(queries));
  ZC_EXPECT(!ZC_REQUIRE_NONNULL(first).commit(queries));

  auto marker = module(coreCrate(), "core"_zc, "marker"_zc);
  auto present = queries.snapshot().probeInput<ModuleDependencySiteInput>(marker);
  ZC_REQUIRE(!present.isRuntimeFailure());
  ZC_REQUIRE(present.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(first).nextLedger().entries().size() != 0);
}

ZC_TEST("Independent module graph input verifier rejects incomplete core roots") {
  auto prior = VerifiedModuleGraphInputLedger::empty();
  ZC_EXPECT(preparedTransaction(prior, true, false, true) == zc::none);
}

ZC_TEST("Independent module graph input verifier rejects an omitted module family") {
  auto prior = VerifiedModuleGraphInputLedger::empty();
  ZC_EXPECT(preparedTransaction(prior, false) == zc::none);
}

ZC_TEST("Derived module queries project the sole catalog through prelude resolution") {
  basic::ThreadPool scheduler(2);
  auto queries = database(scheduler);
  auto empty = VerifiedModuleGraphInputLedger::empty();
  auto graphInputs = preparedTransaction(empty, true);
  ZC_REQUIRE(graphInputs != zc::none);
  ZC_REQUIRE(ZC_REQUIRE_NONNULL(graphInputs).commit(queries));

  auto userCrate = crate();
  auto userModule = module(userCrate, "test"_zc);
  auto userSource = localSource(userCrate, "test.zom"_zc);
  auto sourceInputs = queries.beginInputTransaction();
  ZC_REQUIRE(sourceInputs != zc::none);
  ZC_IF_SOME(transaction, sourceInputs) {
    stageSource(transaction, userSource);
    stageUserActiveSource(transaction, userSource);
    ZC_REQUIRE(transaction.commit() != zc::none);
  }

  auto snapshot = queries.snapshot();
  auto selected = snapshot.get<SelectedModuleSourceQuery>(userModule);
  ZC_REQUIRE(!selected.isRuntimeFailure());
  ZC_REQUIRE(selected.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(selected.value().sameAs(userSource));

  auto active = snapshot.get<ActiveModulesQuery>(userCrate);
  ZC_REQUIRE(!active.isRuntimeFailure());
  ZC_REQUIRE(active.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(active.value().modules().size() == 1);
  ZC_EXPECT(active.value().modules()[0].encode().asPtr() == userModule.encode().asPtr());

  auto sites = snapshot.get<ModuleDependencySitesQuery>(userModule);
  ZC_REQUIRE(!sites.isRuntimeFailure());
  ZC_REQUIRE(sites.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(sites.value().sites().size() == 0);

  auto requests = snapshot.get<ModuleDependencyRequestsQuery>(userModule);
  ZC_REQUIRE(!requests.isRuntimeFailure());
  ZC_REQUIRE(requests.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(requests.value().requests().size() == 1);
  ZC_EXPECT(requests.value().requests()[0].dependencyKind() ==
            identity::ModuleDependencyKind::Prelude);

  auto dependencies = snapshot.get<ModuleDependenciesQuery>(userModule);
  ZC_REQUIRE(!dependencies.isRuntimeFailure());
  ZC_REQUIRE(dependencies.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(dependencies.value().dependencies().size() == 1);
  ZC_EXPECT(dependencies.value().dependencies()[0].encode().asPtr() ==
            module(coreCrate(), "core"_zc, "prelude"_zc).encode().asPtr());
}

ZC_TEST("Stable graph and independent SCC queries cover the complete core root") {
  basic::ThreadPool scheduler(2);
  auto queries = database(scheduler);
  auto empty = VerifiedModuleGraphInputLedger::empty();
  auto graphInputs = preparedTransaction(empty, true);
  ZC_REQUIRE(graphInputs != zc::none);
  ZC_REQUIRE(ZC_REQUIRE_NONNULL(graphInputs).commit(queries));
  auto sourceInputs = queries.beginInputTransaction();
  ZC_REQUIRE(sourceInputs != zc::none);
  ZC_IF_SOME(transaction, sourceInputs) {
    stageCoreDistribution(transaction);
    ZC_REQUIRE(transaction.commit() != zc::none);
  }

  auto roots =
      incremental_binding_query::CompilationRootSetQueryKey::singletonToolchainCore(coreCrate());
  ZC_REQUIRE(roots != zc::none);
  auto snapshot = queries.snapshot();
  auto graph = snapshot.get<ModuleGraphQuery>(ZC_REQUIRE_NONNULL(roots));
  ZC_REQUIRE(!graph.isRuntimeFailure());
  ZC_REQUIRE(graph.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(graph.value().modules().size() == 3);
  ZC_EXPECT(graph.value().edges().size() == 1);

  auto scc = snapshot.get<ModuleGraphSccQuery>(ZC_REQUIRE_NONNULL(roots));
  ZC_REQUIRE(!scc.isRuntimeFailure());
  ZC_REQUIRE(scc.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(scc.value().components().size() == 3);
  ZC_EXPECT(!scc.value().hasCycle(graph.value()));

  auto graphCodec = ModuleGraphRecord::decodeCanonical(graph.value().encodeCanonical().asPtr());
  auto sccCodec = ModuleGraphSccRecord::decodeCanonical(scc.value().encodeCanonical().asPtr());
  ZC_EXPECT(graphCodec != zc::none);
  ZC_EXPECT(sccCodec != zc::none);

  auto core = coreCrate();
  auto contextualCore =
      core_library_query::ContextualCoreCrateKey::from(contextRoots(core), core.clone());
  ZC_REQUIRE(contextualCore != zc::none);
  auto coreGraph =
      snapshot.get<core_library_query::CoreModuleGraphQuery>(ZC_REQUIRE_NONNULL(contextualCore));
  ZC_REQUIRE(!coreGraph.isRuntimeFailure());
  ZC_REQUIRE(coreGraph.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(coreGraph.value().core().encode().asPtr() == core.encode().asPtr());
  ZC_EXPECT(coreGraph.value().modules().size() == 3);
  ZC_EXPECT(coreGraph.value().edges().size() == 1);
  auto expectedCoreContext = identity::CoreSemanticContextFingerprint::compute(core);
  ZC_REQUIRE(expectedCoreContext != zc::none);
  ZC_EXPECT(coreGraph.value().coreContext().digest() ==
            ZC_REQUIRE_NONNULL(expectedCoreContext).digest());
  auto coreGraphCodec = core_library_query::CoreModuleGraphRecord::decodeCanonical(
      coreGraph.value().encodeCanonical().asPtr());
  ZC_REQUIRE(coreGraphCodec != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(coreGraphCodec).revision() == coreGraph.value().revision());
}

ZC_TEST("Nested dependency failure globally precedes an earlier outside edge") {
  basic::ThreadPool scheduler(2);
  auto queries = database(scheduler);
  auto empty = VerifiedModuleGraphInputLedger::empty();
  auto graphInputs = preparedTransaction(empty, true);
  ZC_REQUIRE(graphInputs != zc::none);
  ZC_REQUIRE(ZC_REQUIRE_NONNULL(graphInputs).commit(queries));
  auto sourceInputs = queries.beginInputTransaction();
  ZC_REQUIRE(sourceInputs != zc::none);
  ZC_IF_SOME(transaction, sourceInputs) {
    stageCoreDistribution(transaction);
    ZC_REQUIRE(transaction.commit() != zc::none);
  }

  auto core = coreCrate();
  auto earlier = module(core, "core"_zc);
  auto later = module(core, "core"_zc, "marker"_zc);
  auto snapshot = queries.snapshot();
  auto earlierSource = snapshot.get<SelectedModuleSourceQuery>(earlier);
  auto laterSource = snapshot.get<SelectedModuleSourceQuery>(later);
  ZC_REQUIRE(earlierSource.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(laterSource.kind() == query::QueryValueKind::Value);
  auto earlierStable =
      identity::source_query::StableSourceQueryKey::fromVerified(earlierSource.value());
  auto laterStable =
      identity::source_query::StableSourceQueryKey::fromVerified(laterSource.value());
  ZC_REQUIRE(earlierStable != zc::none);
  ZC_REQUIRE(laterStable != zc::none);
  auto earlierSnapshot =
      snapshot.get<identity::source_query::SourceSnapshotInput>(ZC_REQUIRE_NONNULL(earlierStable));
  auto laterSnapshot =
      snapshot.get<identity::source_query::SourceSnapshotInput>(ZC_REQUIRE_NONNULL(laterStable));
  ZC_REQUIRE(earlierSnapshot.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(laterSnapshot.kind() == query::QueryValueKind::Value);

  auto mutation = queries.beginInputTransaction();
  ZC_REQUIRE(mutation != zc::none);
  ZC_IF_SOME(transaction, mutation) {
    auto outsidePath = modulePath("outside"_zc);
    zc::Vector<DetachedModuleDependencySite> outsideSites;
    auto outsideSite = DetachedModuleDependencySite::from(
        DetachedModuleDependencySiteKind::Import, cloneModulePath(outsidePath.asPtr()), 900);
    outsideSites.add(zc::mv(ZC_REQUIRE_NONNULL(outsideSite)));
    auto outsideSet = DetachedModuleDependencySiteSet::from(
        earlier.clone(), earlierSource.value().clone(), earlierSnapshot.value().contentDigest(),
        zc::mv(outsideSites));
    ZC_REQUIRE(transaction.set<ModuleDependencySiteInput>(earlier, ZC_REQUIRE_NONNULL(outsideSet)));

    auto missingPath = modulePath("missing"_zc);
    zc::Vector<DetachedModuleDependencySite> missingSites;
    auto missingSite = DetachedModuleDependencySite::from(
        DetachedModuleDependencySiteKind::Import, cloneModulePath(missingPath.asPtr()), 901);
    missingSites.add(zc::mv(ZC_REQUIRE_NONNULL(missingSite)));
    auto missingSet = DetachedModuleDependencySiteSet::from(
        later.clone(), laterSource.value().clone(), laterSnapshot.value().contentDigest(),
        zc::mv(missingSites));
    ZC_REQUIRE(transaction.set<ModuleDependencySiteInput>(later, ZC_REQUIRE_NONNULL(missingSet)));

    for (const auto name : {"outside"_zc, "missing"_zc}) {
      auto alias = identity::DependencyAlias::fromCanonical(name);
      auto key = incremental_module_resolution_query::DependencyAliasRootQueryKey::from(
          core.clone(), zc::mv(ZC_REQUIRE_NONNULL(alias)));
      ZC_REQUIRE(transaction.set<incremental_module_resolution_query::DependencyAliasRootInput>(
          ZC_REQUIRE_NONNULL(key),
          incremental_module_resolution_query::ExplicitModuleTarget::absent()));
    }

    auto inactive = module(core, "outside"_zc);
    stageBucket(transaction, core, modulePath("core"_zc, "outside"_zc));
    zc::Maybe<identity::ModuleKey> inactiveTarget(inactive.clone());
    stageBucket(transaction, core, modulePath("outside"_zc), zc::mv(inactiveTarget));
    stageBucket(transaction, core, modulePath("core"_zc, "marker"_zc, "missing"_zc));
    stageBucket(transaction, core, modulePath("core"_zc, "missing"_zc));
    stageBucket(transaction, core, modulePath("missing"_zc));
    ZC_REQUIRE(transaction.commit() != zc::none);
  }

  auto roots = incremental_binding_query::CompilationRootSetQueryKey::singletonToolchainCore(core);
  ZC_REQUIRE(roots != zc::none);
  auto graph = queries.snapshot().get<ModuleGraphQuery>(ZC_REQUIRE_NONNULL(roots));
  ZC_REQUIRE(!graph.isRuntimeFailure());
  ZC_REQUIRE(graph.kind() == query::QueryValueKind::SemanticFailure);
  auto failure = ModuleDependencyFailureRecord::decodeCanonical(graph.semanticFailureBytes());
  ZC_REQUIRE(failure != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(failure).kind() == ModuleDependencyFailureKind::Missing);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(failure).request().requester().encode().asPtr() ==
            later.encode().asPtr());
}

ZC_TEST("Tarjan provider and Kosaraju verifier agree after a tracked cycle mutation") {
  basic::ThreadPool scheduler(2);
  auto queries = database(scheduler);
  auto empty = VerifiedModuleGraphInputLedger::empty();
  auto graphInputs = preparedTransaction(empty, true);
  ZC_REQUIRE(graphInputs != zc::none);
  ZC_REQUIRE(ZC_REQUIRE_NONNULL(graphInputs).commit(queries));
  auto sourceInputs = queries.beginInputTransaction();
  ZC_REQUIRE(sourceInputs != zc::none);
  ZC_IF_SOME(transaction, sourceInputs) {
    stageCoreDistribution(transaction);
    ZC_REQUIRE(transaction.commit() != zc::none);
  }

  auto core = coreCrate();
  auto root = module(core, "core"_zc);
  auto marker = module(core, "core"_zc, "marker"_zc);
  auto snapshot = queries.snapshot();
  auto rootSource = snapshot.get<SelectedModuleSourceQuery>(root);
  auto markerSource = snapshot.get<SelectedModuleSourceQuery>(marker);
  ZC_REQUIRE(rootSource.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(markerSource.kind() == query::QueryValueKind::Value);
  auto rootStable = identity::source_query::StableSourceQueryKey::fromVerified(rootSource.value());
  auto markerStable =
      identity::source_query::StableSourceQueryKey::fromVerified(markerSource.value());
  ZC_REQUIRE(rootStable != zc::none);
  ZC_REQUIRE(markerStable != zc::none);
  auto rootSnapshot =
      snapshot.get<identity::source_query::SourceSnapshotInput>(ZC_REQUIRE_NONNULL(rootStable));
  auto markerSnapshot =
      snapshot.get<identity::source_query::SourceSnapshotInput>(ZC_REQUIRE_NONNULL(markerStable));
  ZC_REQUIRE(rootSnapshot.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(markerSnapshot.kind() == query::QueryValueKind::Value);

  auto mutation = queries.beginInputTransaction();
  ZC_REQUIRE(mutation != zc::none);
  ZC_IF_SOME(transaction, mutation) {
    zc::Vector<DetachedModuleDependencySite> rootSites;
    auto rootSite = DetachedModuleDependencySite::from(DetachedModuleDependencySiteKind::Import,
                                                       modulePath("marker"_zc), 910);
    rootSites.add(zc::mv(ZC_REQUIRE_NONNULL(rootSite)));
    auto rootSet = DetachedModuleDependencySiteSet::from(root.clone(), rootSource.value().clone(),
                                                         rootSnapshot.value().contentDigest(),
                                                         zc::mv(rootSites));
    ZC_REQUIRE(transaction.set<ModuleDependencySiteInput>(root, ZC_REQUIRE_NONNULL(rootSet)));

    zc::Vector<DetachedModuleDependencySite> markerSites;
    auto markerSite = DetachedModuleDependencySite::from(DetachedModuleDependencySiteKind::Import,
                                                         modulePath("core"_zc), 911);
    markerSites.add(zc::mv(ZC_REQUIRE_NONNULL(markerSite)));
    auto markerSet = DetachedModuleDependencySiteSet::from(
        marker.clone(), markerSource.value().clone(), markerSnapshot.value().contentDigest(),
        zc::mv(markerSites));
    ZC_REQUIRE(transaction.set<ModuleDependencySiteInput>(marker, ZC_REQUIRE_NONNULL(markerSet)));

    for (const auto name : {"marker"_zc, "core"_zc}) {
      auto alias = identity::DependencyAlias::fromCanonical(name);
      auto key = incremental_module_resolution_query::DependencyAliasRootQueryKey::from(
          core.clone(), zc::mv(ZC_REQUIRE_NONNULL(alias)));
      ZC_REQUIRE(transaction.set<incremental_module_resolution_query::DependencyAliasRootInput>(
          ZC_REQUIRE_NONNULL(key),
          incremental_module_resolution_query::ExplicitModuleTarget::absent()));
    }
    zc::Maybe<identity::ModuleKey> markerTarget(marker.clone());
    stageBucket(transaction, core, modulePath("core"_zc, "marker"_zc), zc::mv(markerTarget));
    stageBucket(transaction, core, modulePath("marker"_zc));
    stageBucket(transaction, core, modulePath("core"_zc, "marker"_zc, "core"_zc));
    stageBucket(transaction, core, modulePath("core"_zc, "core"_zc));
    zc::Maybe<identity::ModuleKey> rootTarget(root.clone());
    stageBucket(transaction, core, modulePath("core"_zc), zc::mv(rootTarget));
    ZC_REQUIRE(transaction.commit() != zc::none);
  }

  auto roots = incremental_binding_query::CompilationRootSetQueryKey::singletonToolchainCore(core);
  ZC_REQUIRE(roots != zc::none);
  auto finalSnapshot = queries.snapshot();
  auto graph = finalSnapshot.get<ModuleGraphQuery>(ZC_REQUIRE_NONNULL(roots));
  auto scc = finalSnapshot.get<ModuleGraphSccQuery>(ZC_REQUIRE_NONNULL(roots));
  ZC_REQUIRE(graph.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(scc.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(graph.value().edges().size() == 3);
  ZC_EXPECT(scc.value().components().size() == 2);
  ZC_EXPECT(scc.value().hasCycle(graph.value()));
}

ZC_TEST("Independent transaction verifier rejects sites not backed by parsed syntax") {
  auto empty = VerifiedModuleGraphInputLedger::empty();
  auto graphInputs = preparedTransaction(empty, true, true);
  ZC_EXPECT(graphInputs == zc::none);
}

}  // namespace zomlang::compiler::driver::module_graph_query
