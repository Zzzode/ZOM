// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/basic/string-pool.h"
#include "compiler/basic/thread-pool.h"
#include "compiler/basic/zomlang-opts.h"
#include "compiler/binder/graph/module-dependency-requests.h"
#include "compiler/binder/graph/parsed-module-graph-input.h"
#include "compiler/diagnostics/fact/source-diagnostic-draft-buffer.h"
#include "compiler/driver/query/module-graph/module-graph-query.h"
#include "compiler/identity/canonical/canonical-encoder.h"
#include "compiler/identity/source/source-query-input.h"
#include "compiler/identity/source/source-snapshot.h"
#include "compiler/ir/target/target-registry.h"
#include "compiler/parser/parser.h"
#include "compiler/source/core-distribution.h"
#include "compiler/source/manager.h"
#include "tests/unittests/compiler/binder/graph/parsed-module-query-test-fixture.h"
#include "tests/unittests/compiler/driver/core/core-library-test-fixture.h"
#include "tests/unittests/compiler/test-semantic-identities.h"
#include "zc/core/encoding.h"
#include "zc/core/map.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::driver::module_graph_query {
namespace {

namespace resolution_query = incremental_module_resolution_query;

using tests::test_identity_detail::coreCrate;
using tests::test_identity_detail::crate;
using tests::test_identity_detail::digest;
using tests::test_identity_detail::scalar;

constexpr zc::StringPtr kUserSourceText = "module test;\n"_zc;

identity::ModuleKey module(const identity::CrateKey& owner, zc::StringPtr first,
                           zc::StringPtr second = nullptr) {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(scalar<identity::ModulePathSegment>(first));
  if (second != nullptr) { path.add(scalar<identity::ModulePathSegment>(second)); }
  auto result = identity::ModuleKey::from(owner.clone(), zc::mv(path));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::SourceFileKey localSource(const identity::CrateKey& owner, zc::StringPtr name) {
  zc::Vector<identity::CanonicalPathSegment> path;
  path.add(scalar<identity::CanonicalPathSegment>(name));
  return identity::SourceFileKey::from(
      owner.clone(), identity::SourceOriginKey::localFile(
                         identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(path))));
}

identity::SourceFileKey coreSource(const identity::CrateKey& owner, zc::StringPtr modulePath,
                                   zc::StringPtr fileName) {
  zc::Vector<identity::CanonicalPathSegment> path;
  if (modulePath != "core"_zc) { path.add(scalar<identity::CanonicalPathSegment>("core"_zc)); }
  path.add(scalar<identity::CanonicalPathSegment>(fileName));
  return identity::SourceFileKey::from(
      owner.clone(),
      identity::SourceOriginKey::coreFile(identity::ToolchainUnitKey::core(),
                                          identity::CanonicalRelativePath::from(zc::mv(path))));
}

zc::Vector<identity::ModulePathSegment> modulePath(zc::StringPtr first,
                                                   zc::StringPtr second = nullptr) {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(scalar<identity::ModulePathSegment>(first));
  if (second != nullptr) { path.add(scalar<identity::ModulePathSegment>(second)); }
  return path;
}

package::RegisteredTargetProfileName profileName() {
  auto value = package::RegisteredTargetProfileName::from("module-graph-input-test"_zc);
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
  path.add(scalar<identity::CanonicalPathSegment>("test.zom"_zc));
  zc::Vector<package::VerifiedCompilationRoot> roots;
  roots.add(package::VerifiedCompilationRoot::from(
      tests::test_identity_detail::package(), identity::CrateTargetKind::Library,
      scalar<identity::TargetName>("test"_zc), 2026, false,
      identity::CanonicalRelativePath::from(zc::mv(path))));
  auto request = package::VerifiedPackageCompilationRequest::from(
      zc::mv(roots), targetSelection(registry), targetSelection(registry),
      package::SelectedLanguageOptions{}, package::PackageLockMode::PreferLocked);
  return zc::mv(ZC_REQUIRE_NONNULL(request));
}

/// \brief Parses, snapshots, and independently verifies one source text.
struct ParsedSource final {
  explicit ParsedSource(zc::ArrayPtr<const uint8_t> text)
      : sources(zc::heap<source::SourceManager>()),
        buffer(sources->addMemBufferCopy(text, "module.zom")) {
    diagnostics::SourceDiagnosticDraftBuffer facts(*sources, buffer);
    parser::Parser parser(*sources, facts, options, strings, buffer);
    auto parsed = parser.parse();
    ZC_REQUIRE(parsed != zc::none);
    ZC_REQUIRE(!facts.hasErrors());
    tree = zc::mv(ZC_REQUIRE_NONNULL(parsed));
    tokens = parser.takeTokenSnapshot();
    ZC_REQUIRE(tokens != zc::none);
  }

  identity::ImmutableSourceSnapshot snapshot(const identity::SourceFileKey& source) const {
    auto value = identity::ImmutableSourceSnapshot::from(
        source.clone(), zc::heapArray<uint8_t>(sources->getEntireTextForBuffer(buffer)));
    return zc::mv(ZC_REQUIRE_NONNULL(value));
  }

  binder::VerifiedParsedModule verify(identity::SemanticContextBrand context,
                                      const identity::SourceFileKey& source) {
    auto retained = zc::mv(ZC_REQUIRE_NONNULL(tokens));
    return binder::test::requireVerifiedParsedSource(context, snapshot(source), *sources, buffer,
                                                     zc::mv(retained), zc::mv(tree));
  }

  zc::Own<source::SourceManager> sources;
  basic::LangOptions options;
  basic::StringPool strings;
  source::BufferId buffer;
  ast::Tree tree;
  zc::Maybe<parser::ParsedTokenSnapshot> tokens;
};

/// \brief Complete live authority from which a structural-input transaction is prepared.
///
/// The transaction verifier rebuilds every family from these authorities alone, so
/// a fixture that disagrees with the verifier cannot prepare at all. That property
/// is what makes the mutations below meaningful: they corrupt one family and the
/// independent verifier is the only thing that can reject it.
struct TransactionFixture final {
  TransactionFixture()
      : registry(targetRegistry()),
        request(packageRequest(registry)),
        policyTemplate(ZC_REQUIRE_NONNULL(source::core::initialCoreMarkerPolicyTemplate())),
        addresses(zc::heap<Addresses>()),
        parsedStorage(zc::heap<zc::Vector<binder::VerifiedParsedModule>>()),
        parsedInputStorage(zc::heap<zc::Vector<binder::ParsedModuleGraphInput>>()),
        resolverStorage(zc::heap<zc::Maybe<binder::StructuralModuleResolver>>()),
        snapshots(zc::heap<zc::Vector<identity::ImmutableSourceSnapshot>>()) {
    build();
  }

  struct Addresses final {
    Addresses()
        : userCrate(crate()),
          coreKey(coreCrate()),
          userModule(module(userCrate, "test"_zc)),
          coreRoot(module(coreKey, "core"_zc)),
          marker(module(coreKey, "core"_zc, "marker"_zc)),
          prelude(module(coreKey, "core"_zc, "prelude"_zc)),
          userFile(localSource(userCrate, "test.zom"_zc)),
          coreFile(coreSource(coreKey, "core"_zc, "core.zom"_zc)),
          markerFile(coreSource(coreKey, "core.marker"_zc, "marker.zom"_zc)),
          preludeFile(coreSource(coreKey, "core.prelude"_zc, "prelude.zom"_zc)) {}

    identity::CrateKey userCrate;
    identity::CrateKey coreKey;
    identity::ModuleKey userModule;
    identity::ModuleKey coreRoot;
    identity::ModuleKey marker;
    identity::ModuleKey prelude;
    identity::SourceFileKey userFile;
    identity::SourceFileKey coreFile;
    identity::SourceFileKey markerFile;
    identity::SourceFileKey preludeFile;
  };

  ModuleGraphInputTransactionAuthority authority() const {
    return ModuleGraphInputTransactionAuthority{request, policyTemplate, resolver(),
                                                parsedInputStorage->asPtr()};
  }

  /// \brief Prepares one transaction, optionally with a site no parsed syntax backs
  /// or with the toolchain core crate left out of the projected roots.
  zc::Maybe<VerifiedModuleGraphInputTransaction> prepare(
      const VerifiedModuleGraphInputLedger& prior, bool withInventedSite = false,
      bool omitCoreProjection = false) const;

  /// \brief Installs the source-byte and active-source sidecar families.
  ///
  /// The structural transaction owns the module graph's topology. Turning that
  /// topology into an evaluated graph additionally demands the source bytes and
  /// the active-source verdict for each crate, which every session stages beside
  /// it.
  bool stageSidecarInputs(query::QueryDatabase& database) const;

  const Addresses& address() const { return *addresses; }
  const binder::StructuralModuleResolver& resolver() const {
    ZC_IF_SOME(value, *resolverStorage) { return value; }
    ZC_FAIL_REQUIRE("transaction fixture froze no structural resolver");
  }
  const package::VerifiedPackageCompilationRequest& packageRequestValue() const { return request; }

  void build();

  ir::TargetRegistrySnapshot registry;
  package::VerifiedPackageCompilationRequest request;
  source::core::CoreStandardMarkerPolicyTemplate policyTemplate;
  zc::Own<Addresses> addresses;
  zc::Own<zc::Vector<binder::VerifiedParsedModule>> parsedStorage;
  zc::Own<zc::Vector<binder::ParsedModuleGraphInput>> parsedInputStorage;
  zc::Own<zc::Maybe<binder::StructuralModuleResolver>> resolverStorage;
  zc::Own<zc::Vector<identity::ImmutableSourceSnapshot>> snapshots;
};

void TransactionFixture::build() {
  const auto& key = *addresses;
  auto coreInputs = core_library_test::coreSourceInputs();
  ZC_REQUIRE(coreInputs.size() == 3);

  ParsedSource userParsed(kUserSourceText.asBytes());
  ParsedSource coreParsed(coreInputs[0].bytes.asPtr());
  ParsedSource markerParsed(coreInputs[1].bytes.asPtr());
  ParsedSource preludeParsed(coreInputs[2].bytes.asPtr());

  identity::SemanticContextFactory contextFactory;
  auto context = contextFactory.issue();
  ZC_REQUIRE(context != zc::none);
  auto identities =
      identity::IdentityInternerSet::create(contextFactory, ZC_REQUIRE_NONNULL(context));
  ZC_REQUIRE(identities != zc::none);

  auto userSnapshot = userParsed.snapshot(key.userFile);
  auto coreSnapshot = coreParsed.snapshot(key.coreFile);
  auto markerSnapshot = markerParsed.snapshot(key.markerFile);
  auto preludeSnapshot = preludeParsed.snapshot(key.preludeFile);
  snapshots->add(userSnapshot.clone());
  snapshots->add(coreSnapshot.clone());
  snapshots->add(markerSnapshot.clone());
  snapshots->add(preludeSnapshot.clone());

  zc::Vector<binder::VerifiedParsedModule> parsedModules(4);
  parsedModules.add(userParsed.verify(ZC_REQUIRE_NONNULL(context), key.userFile));
  parsedModules.add(coreParsed.verify(ZC_REQUIRE_NONNULL(context), key.coreFile));
  parsedModules.add(markerParsed.verify(ZC_REQUIRE_NONNULL(context), key.markerFile));
  parsedModules.add(preludeParsed.verify(ZC_REQUIRE_NONNULL(context), key.preludeFile));

  const identity::ModuleKey* moduleKeys[] = {&key.userModule, &key.coreRoot, &key.marker,
                                             &key.prelude};
  const identity::SourceFileKey* sourceKeys[] = {&key.userFile, &key.coreFile, &key.markerFile,
                                                 &key.preludeFile};
  zc::Vector<identity::ModuleId> moduleHandles(4);
  ZC_IF_SOME(interner, identities) {
    for (const auto* moduleKeyValue : moduleKeys) {
      auto handle = interner.internModule(ZC_REQUIRE_NONNULL(context), *moduleKeyValue);
      ZC_REQUIRE(handle.is<identity::ModuleId>());
      moduleHandles.add(handle.get<identity::ModuleId>());
    }
  }
  // `ParsedModuleGraphInput` borrows the verified module, so the modules must reach
  // their final storage before any input refers to them.
  for (auto& parsed : parsedModules) { parsedStorage->add(zc::mv(parsed)); }
  zc::Vector<binder::ParsedModuleGraphInput> parsedInputs(4);
  for (size_t index = 0; index < 4; ++index) {
    parsedInputs.add(binder::ParsedModuleGraphInput{moduleHandles[index], (*parsedStorage)[index]});
  }

  zc::Vector<binder::ModuleSearchRoot> environmentRoots;
  zc::Vector<identity::CanonicalPathSegment> workspacePath;
  environmentRoots.add(binder::ModuleSearchRoot::workspace(
      key.userCrate.clone(),
      identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(workspacePath))));
  auto coreRootSearch =
      binder::ModuleSearchRoot::toolchainCore(key.coreKey.clone(), policyTemplate.revision());
  ZC_REQUIRE(coreRootSearch != zc::none);
  environmentRoots.add(zc::mv(ZC_REQUIRE_NONNULL(coreRootSearch)));

  zc::Vector<binder::ModuleSourceSnapshotRevision> sourceRevisions;
  const identity::ImmutableSourceSnapshot* snapshotValues[] = {&userSnapshot, &coreSnapshot,
                                                               &markerSnapshot, &preludeSnapshot};
  for (const auto* snapshot : snapshotValues) {
    sourceRevisions.add(binder::ModuleSourceSnapshotRevision(snapshot->source().clone(),
                                                             snapshot->contentDigest()));
  }
  zc::Vector<binder::GeneratedModuleSourceRevision> generatedRevisions;
  // The core crate spells `core::marker`; the user crate has no alias root, so the
  // only configured alias is the toolchain core every user crate resolves by name.
  zc::Vector<binder::ModuleDependencyAliasRoot> resolverAliases;
  resolverAliases.add(binder::ModuleDependencyAliasRoot(
      key.userCrate.clone(), scalar<identity::DependencyAlias>("core"_zc), key.coreRoot.clone()));
  zc::Vector<binder::RequesterModuleAncestryCandidate> ancestryCandidates;
  const auto addAncestry = [&](const identity::ModuleKey& requester,
                               zc::Maybe<const identity::ModuleKey&> parent) {
    zc::Vector<identity::ModuleKey> chain;
    chain.add(requester.clone());
    ZC_IF_SOME(value, parent) { chain.add(value.clone()); }
    ancestryCandidates.add(
        binder::RequesterModuleAncestryCandidate(requester.clone(), zc::mv(chain)));
  };
  addAncestry(key.userModule, zc::none);
  addAncestry(key.coreRoot, zc::none);
  addAncestry(key.marker, key.coreRoot);
  addAncestry(key.prelude, key.coreRoot);

  zc::Vector<binder::StructuralModuleCatalogEntry> resolverCatalog;
  for (size_t index = 0; index < 4; ++index) {
    resolverCatalog.add(binder::StructuralModuleCatalogEntry(
        moduleKeys[index]->clone(), moduleHandles[index], sourceKeys[index]->clone()));
  }
  auto frozen = binder::StructuralModuleResolver::freeze(
      ZC_REQUIRE_NONNULL(context),
      binder::ModuleResolutionEnvironmentRecord(zc::mv(environmentRoots), zc::mv(sourceRevisions),
                                                zc::mv(generatedRevisions), zc::mv(resolverAliases),
                                                zc::mv(ancestryCandidates)),
      zc::mv(resolverCatalog));
  ZC_REQUIRE(frozen.is<binder::StructuralModuleResolver>());
  *resolverStorage = zc::mv(frozen.get<binder::StructuralModuleResolver>());

  for (auto& parsed : parsedInputs) { parsedInputStorage->add(zc::mv(parsed)); }
}

zc::Maybe<VerifiedModuleGraphInputTransaction> TransactionFixture::prepare(
    const VerifiedModuleGraphInputLedger& prior, bool withInventedSite,
    bool omitCoreProjection) const {
  const auto& key = *addresses;
  const auto& resolverValue = resolver();
  const identity::ModuleKey* moduleKeys[] = {&key.userModule, &key.coreRoot, &key.marker,
                                             &key.prelude};
  const identity::SourceFileKey* sourceKeys[] = {&key.userFile, &key.coreFile, &key.markerFile,
                                                 &key.preludeFile};

  zc::Vector<binder::ModuleDependencyRequest> requests;
  zc::Vector<DetachedModuleDependencySiteSet> sites;
  for (size_t index = 0; index < parsedInputStorage->size(); ++index) {
    auto derived = binder::ModuleDependencyRequestDeriver::derive(
        (*parsedInputStorage)[index].module, (*parsedInputStorage)[index].parsedModule,
        resolverValue);
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
            kind,
            modulePath(request.normalizedPath()[0].text(), request.normalizedPath().size() > 1
                                                               ? request.normalizedPath()[1].text()
                                                               : nullptr),
            syntax.schemaPreorderOrdinal);
        ZC_REQUIRE(site != zc::none);
        detached.add(zc::mv(ZC_REQUIRE_NONNULL(site)));
      }
      requests.add(zc::mv(request));
    }
    if (withInventedSite && moduleKeys[index] == &key.coreRoot) {
      auto invented = DetachedModuleDependencySite::from(DetachedModuleDependencySiteKind::Import,
                                                         modulePath("core"_zc, "prelude"_zc), 999);
      ZC_REQUIRE(invented != zc::none);
      detached.add(zc::mv(ZC_REQUIRE_NONNULL(invented)));
    }
    auto siteSet = DetachedModuleDependencySiteSet::from(
        moduleKeys[index]->clone(), sourceKeys[index]->clone(),
        (*parsedInputStorage)[index].parsedModule.contentDigest(), zc::mv(detached));
    ZC_REQUIRE(siteSet != zc::none);
    sites.add(zc::mv(ZC_REQUIRE_NONNULL(siteSet)));
  }

  zc::Vector<SelectedModuleRecord> userEntries;
  userEntries.add(SelectedModuleRecord(key.userModule.clone(), key.userFile.clone()));
  auto userCatalog = SelectedModuleCatalog::from(key.userCrate.clone(), zc::mv(userEntries));
  ZC_REQUIRE(userCatalog != zc::none);

  zc::Vector<SelectedModuleRecord> coreEntries;
  coreEntries.add(SelectedModuleRecord(key.coreRoot.clone(), key.coreFile.clone()));
  coreEntries.add(SelectedModuleRecord(key.marker.clone(), key.markerFile.clone()));
  coreEntries.add(SelectedModuleRecord(key.prelude.clone(), key.preludeFile.clone()));
  auto coreCatalog = SelectedModuleCatalog::from(key.coreKey.clone(), zc::mv(coreEntries));
  ZC_REQUIRE(coreCatalog != zc::none);

  zc::Vector<SelectedModuleCatalog> catalogs;
  catalogs.add(zc::mv(ZC_REQUIRE_NONNULL(userCatalog)));
  catalogs.add(zc::mv(ZC_REQUIRE_NONNULL(coreCatalog)));

  zc::Vector<identity::RequesterModuleAncestry> ancestries;
  for (const auto& ancestry : resolverValue.requesterAncestryInputs()) {
    ancestries.add(ancestry.clone());
  }

  zc::TreeMap<zc::String, resolution_query::CanonicalModuleCatalogBucket> bucketMap;
  const auto addBucket = [&](const identity::CrateKey& owner,
                             zc::ArrayPtr<const identity::ModulePathSegment> path) {
    auto bucket = resolverValue.catalogPathBucketInput(owner, path);
    ZC_REQUIRE(bucket != zc::none);
    auto canonical =
        resolution_query::CanonicalModuleCatalogBucket::fromVerified(ZC_REQUIRE_NONNULL(bucket));
    auto encoded = canonical.key().encode();
    auto slot = zc::encodeHex(encoded.asPtr());
    if (bucketMap.find(slot) == zc::none) { bucketMap.insert(zc::mv(slot), zc::mv(canonical)); }
  };
  for (const auto& entry : resolverValue.catalog()) {
    addBucket(entry.key.crate(), entry.key.path());
  }
  for (const auto& request : requests) {
    zc::Maybe<const identity::RequesterModuleAncestry&> selectedAncestry;
    for (const auto& candidate : resolverValue.requesterAncestryInputs()) {
      if (candidate.requester().encode().asPtr() == request.key().requester().encode().asPtr()) {
        selectedAncestry = candidate;
      }
    }
    ZC_REQUIRE(selectedAncestry != zc::none);
    for (const auto& ancestor : ZC_REQUIRE_NONNULL(selectedAncestry).ancestry()) {
      auto path = modulePath(ancestor.path()[0].text(),
                             ancestor.path().size() > 1 ? ancestor.path()[1].text() : nullptr);
      for (const auto& segment : request.normalizedPath()) { path.add(segment.clone()); }
      addBucket(request.key().requester().crate(), path.asPtr());
    }
    auto requestPath = modulePath(
        request.normalizedPath()[0].text(),
        request.normalizedPath().size() > 1 ? request.normalizedPath()[1].text() : nullptr);
    addBucket(request.key().requester().crate(), requestPath.asPtr());
    ZC_IF_SOME(alias, request.key().dependencyAlias()) {
      for (const auto& root : resolverValue.dependencyAliasRootInputs()) {
        if (root.requester.encode().asPtr() != request.key().requester().crate().encode().asPtr() ||
            root.alias.text() != alias) {
          continue;
        }
        auto path =
            modulePath(root.target.path()[0].text(),
                       root.target.path().size() > 1 ? root.target.path()[1].text() : nullptr);
        for (size_t index = 1; index < request.normalizedPath().size(); ++index) {
          path.add(request.normalizedPath()[index].clone());
        }
        addBucket(root.target.crate(), path.asPtr());
      }
    }
  }
  addBucket(key.coreKey, key.prelude.path());
  zc::Vector<resolution_query::CanonicalModuleCatalogBucket> buckets(bucketMap.size());
  for (auto& entry : bucketMap) { buckets.add(zc::mv(entry.value)); }

  zc::Vector<resolution_query::CanonicalModuleSearchRoots> searchRoots(catalogs.size());
  for (const auto& catalog : catalogs) {
    auto roots = resolution_query::CanonicalModuleSearchRoots::fromVerified(
        catalog.crate(), resolverValue.searchRootInputs());
    ZC_REQUIRE(roots != zc::none);
    searchRoots.add(zc::mv(ZC_REQUIRE_NONNULL(roots)));
  }

  zc::TreeMap<zc::String, ConfiguredDependencyAlias> aliasMap;
  for (const auto& request : requests) {
    auto alias = identity::DependencyAlias::fromCanonical(request.normalizedPath()[0].text());
    if (alias == zc::none) { continue; }
    auto aliasKey = resolution_query::DependencyAliasRootQueryKey::from(
        request.key().requester().crate().clone(), ZC_REQUIRE_NONNULL(alias).clone());
    ZC_REQUIRE(aliasKey != zc::none);
    auto target = resolution_query::ExplicitModuleTarget::absent();
    for (const auto& root : resolverValue.dependencyAliasRootInputs()) {
      if (root.requester.encode().asPtr() == request.key().requester().crate().encode().asPtr() &&
          root.alias.text() == ZC_REQUIRE_NONNULL(alias).text()) {
        target = resolution_query::ExplicitModuleTarget::present(root.target.clone());
      }
    }
    auto encoded = ZC_REQUIRE_NONNULL(aliasKey).encode();
    auto slot = zc::encodeHex(encoded.asPtr());
    if (aliasMap.find(slot) == zc::none) {
      aliasMap.insert(zc::mv(slot), ConfiguredDependencyAlias{zc::mv(ZC_REQUIRE_NONNULL(aliasKey)),
                                                              zc::mv(target)});
    }
  }
  zc::Vector<ConfiguredDependencyAlias> aliases(aliasMap.size());
  for (auto& entry : aliasMap) { aliases.add(zc::mv(entry.value)); }

  zc::Vector<ConfiguredCratePrelude> preludes(catalogs.size());
  for (const auto& catalog : catalogs) {
    auto target = resolution_query::ExplicitModuleTarget::absent();
    if (catalog.crate().unit().kind() != identity::CompilationUnitKind::Toolchain) {
      target = resolution_query::ExplicitModuleTarget::present(key.prelude.clone());
    }
    preludes.add(ConfiguredCratePrelude{catalog.crate().clone(), zc::mv(target)});
  }

  zc::Vector<identity::CrateKey> projectedCore;
  if (!omitCoreProjection) { projectedCore.add(key.coreKey.clone()); }
  auto contextRoots = incremental_binding_query::CompilationRootSetQueryKey::fromVerified(
      request, projectedCore.asPtr());
  ZC_REQUIRE(contextRoots != zc::none);

  return VerifiedModuleGraphInputTransaction::prepare(
      authority(), query::DatabaseRevision(), zc::mv(ZC_REQUIRE_NONNULL(contextRoots)),
      zc::mv(projectedCore), zc::mv(catalogs), zc::mv(sites), zc::mv(ancestries), zc::mv(buckets),
      zc::mv(searchRoots), zc::mv(aliases), zc::mv(preludes), prior.clone());
}

bool TransactionFixture::stageSidecarInputs(query::QueryDatabase& database) const {
  const auto& key = *addresses;
  auto opened = database.beginInputTransaction(database.snapshot().revision());
  if (!opened.isOpened()) { return false; }
  auto transaction = zc::mv(opened).takeTransaction();

  zc::Vector<identity::source_query::StableSourceQueryKey> userSources;
  zc::Vector<identity::source_query::StableSourceQueryKey> coreSources;
  for (const auto& snapshot : *snapshots) {
    auto stable = identity::source_query::StableSourceQueryKey::fromVerified(snapshot.source());
    if (stable == zc::none) { return false; }
    auto canonical = identity::source_query::CanonicalSourceSnapshot::fromVerified(snapshot);
    if (canonical == zc::none || !transaction
                                      .set<identity::source_query::SourceSnapshotInput>(
                                          ZC_REQUIRE_NONNULL(stable), ZC_REQUIRE_NONNULL(canonical))
                                      .isApplied()) {
      return false;
    }
    if (snapshot.source().crate().unit().kind() == identity::CompilationUnitKind::Toolchain) {
      coreSources.add(ZC_REQUIRE_NONNULL(stable).clone());
    } else {
      userSources.add(ZC_REQUIRE_NONNULL(stable).clone());
    }
  }

  auto userCrate = incremental_binding_query::StableCrateQueryKey::fromVerified(key.userCrate);
  auto coreCrateKey = incremental_binding_query::StableCrateQueryKey::fromVerified(key.coreKey);
  if (userCrate == zc::none || coreCrateKey == zc::none) { return false; }
  auto userSet = incremental_binding_query::CanonicalSourceSet::from([&] {
    zc::Vector<identity::source_query::StableSourceQueryKey> copies(0);
    for (const auto& source : userSources) { copies.add(source.clone()); }
    return zc::mv(copies);
  }());
  auto coreSet = incremental_binding_query::CanonicalSourceSet::from([&] {
    zc::Vector<identity::source_query::StableSourceQueryKey> copies(0);
    for (const auto& source : coreSources) { copies.add(source.clone()); }
    return zc::mv(copies);
  }());
  if (userSet == zc::none || coreSet == zc::none) { return false; }
  if (!transaction
           .set<incremental_binding_query::UserPackageActiveSourcesInput>(
               ZC_REQUIRE_NONNULL(userCrate), ZC_REQUIRE_NONNULL(userSet))
           .isApplied() ||
      !transaction
           .set<incremental_binding_query::ToolchainCoreActiveSourcesInput>(
               ZC_REQUIRE_NONNULL(coreCrateKey), ZC_REQUIRE_NONNULL(coreSet))
           .isApplied()) {
    transaction.abandon();
    return false;
  }
  return transaction.commit().isCommitted();
}

class QueryTestSemanticContextResources final : public query::SemanticContextCapabilityResources {};

query::QueryDatabase queryDatabase(basic::ThreadPool& queryScheduler) {
  auto resources = zc::heap<QueryTestSemanticContextResources>();
  auto arena = zc::arc<query::SemanticContextCapabilityArena>(zc::mv(resources));
  query::QueryDatabase result(queryScheduler, query::productionQueryDescriptorInventory(),
                              zc::mv(arena));
  ZC_REQUIRE(registerModuleGraphQueries(result));
  // `ModuleGraph` and `ModuleGraphScc` are the stable topology queries proper; the
  // structural registration above covers only the input and materialization family.
  ZC_REQUIRE(registerStableModuleGraphQueries(result));
  // The structural transaction installs the resolution families, and evaluating the
  // committed graph additionally demands the source-byte and active-source
  // families. The session stages all of them through the binding adapter.
  ZC_REQUIRE(incremental_binding_query::registerIncrementalBindingQueryAdapter(result));
  return result;
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

// The transaction is the only path that installs the structural input families, and
// it must do so atomically: one commit publishes every family, and the object is
// closed afterwards. A second commit that silently succeeded would let one
// session's inputs leak into the next revision.
ZC_TEST("Module graph input transaction commits its complete authority exactly once") {
  TransactionFixture fixture;
  basic::ThreadPool scheduler(2);
  auto database = queryDatabase(scheduler);

  auto transaction = fixture.prepare(VerifiedModuleGraphInputLedger::empty());
  ZC_REQUIRE(transaction != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(transaction).commit(database).isCommitted());
  // One commit closes the transaction; replaying it must not reinstall anything.
  ZC_EXPECT(!ZC_REQUIRE_NONNULL(transaction).commit(database).isCommitted());

  // Every family the payload declares is installed, and the ledger that records
  // them is non-empty and byte-canonical.
  const auto& ledger = ZC_REQUIRE_NONNULL(transaction).nextLedger();
  ZC_EXPECT(ledger.entries().size() != 0);
  auto reencoded = VerifiedModuleGraphInputLedger::from([&] {
    zc::Vector<ModuleGraphInputLedgerEntry> copies(ledger.entries().size());
    for (const auto& entry : ledger.entries()) { copies.add(entry.clone()); }
    return zc::mv(copies);
  }());
  ZC_REQUIRE(reencoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(reencoded).encodeCanonical().asPtr() ==
            ledger.encodeCanonical().asPtr());

  const auto& payload = ZC_REQUIRE_NONNULL(transaction).payload();
  auto decoded =
      VerifiedModuleGraphInputPayload::decodeCanonical(payload.encodeCanonical().asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded) == payload);
  ZC_EXPECT(payload.selectedModuleCatalogs().size() == 2);
  ZC_EXPECT(payload.catalogBuckets().size() != 0);
  ZC_EXPECT(payload.searchRoots().size() == 2);
  ZC_EXPECT(payload.configuredPreludes().size() == 2);
}

// The verifier reconstructs the whole payload from the live authorities and is
// never allowed to trust the candidate. Dropping the projected core crate leaves a
// context-root set that no longer covers the toolchain core, so preparation must
// fail even though every remaining family is internally well formed.
ZC_TEST("Independent module graph input verifier rejects incomplete core roots") {
  TransactionFixture fixture;

  // Baseline: the complete authority prepares and its payload independently
  // verifies. Without this, a rejection below could be a fixture bug.
  auto complete = fixture.prepare(VerifiedModuleGraphInputLedger::empty());
  ZC_REQUIRE(complete != zc::none);
  ZC_EXPECT(VerifiedModuleGraphInputVerifier::verify(fixture.authority(),
                                                     ZC_REQUIRE_NONNULL(complete).payload()));
  ZC_EXPECT(ZC_REQUIRE_NONNULL(complete).payload().projectedCoreCrates().size() == 1);

  // The projected core crate is the only thing that turns the toolchain root into
  // a covered crate. Removing it must be rejected by the independent verifier.
  ZC_EXPECT(fixture.prepare(VerifiedModuleGraphInputLedger::empty(), false, true) == zc::none);
}

// Dependency sites are coordinates into parsed syntax. A site with no backing
// import, reexport, or module alias has no ordinal in the tree, so an independently
// reconstructed set can never contain it.
ZC_TEST("Independent transaction verifier rejects sites not backed by parsed syntax") {
  TransactionFixture fixture;
  auto invented = fixture.prepare(VerifiedModuleGraphInputLedger::empty(), true);
  ZC_EXPECT(invented == zc::none);

  auto honest = fixture.prepare(VerifiedModuleGraphInputLedger::empty(), false);
  ZC_REQUIRE(honest != zc::none);
  ZC_EXPECT(VerifiedModuleGraphInputVerifier::verify(fixture.authority(),
                                                     ZC_REQUIRE_NONNULL(honest).payload()));
}

// The stable graph and its SCC decomposition are separate retained queries over
// the same committed inputs. They must describe one node set: an SCC record that
// covers fewer modules than its graph would let a later pass treat a live module
// as unreachable, and one that covers more would name a module no crate owns.
ZC_TEST("Stable graph and independent SCC queries cover the complete core root") {
  TransactionFixture fixture;
  const auto& key = fixture.address();
  basic::ThreadPool scheduler(2);
  auto database = queryDatabase(scheduler);

  auto transaction = fixture.prepare(VerifiedModuleGraphInputLedger::empty());
  ZC_REQUIRE(transaction != zc::none);
  ZC_REQUIRE(ZC_REQUIRE_NONNULL(transaction).commit(database).isCommitted());
  ZC_REQUIRE(fixture.stageSidecarInputs(database));

  // The toolchain core projects its own crate graph without any package request;
  // that is the root whose complete module set the transaction already published.
  auto roots =
      incremental_binding_query::CompilationRootSetQueryKey::singletonToolchainCore(key.coreKey);
  ZC_REQUIRE(roots != zc::none);
  auto snapshot = database.snapshot();
  auto graph = snapshot.get<ModuleGraph>(ZC_REQUIRE_NONNULL(roots));
  ZC_REQUIRE(!graph.isRuntimeFailure());
  ZC_REQUIRE(graph.kind() == query::QueryValueKind::Value);
  // All three core modules: root, marker, prelude.
  ZC_EXPECT(graph.value().modules().size() == 3);
  // The prelude re-exports core::marker.
  ZC_EXPECT(graph.value().edges().size() == 1);

  auto scc = snapshot.get<ModuleGraphScc>(ZC_REQUIRE_NONNULL(roots));
  ZC_REQUIRE(!scc.isRuntimeFailure());
  ZC_REQUIRE(scc.kind() == query::QueryValueKind::Value);
  const auto& components = scc.value();
  ZC_EXPECT(!components.hasCycle(graph.value()));

  size_t covered = 0;
  for (const auto& component : components.components()) {
    ZC_EXPECT(component.modules().size() > 0);
    // Every component member is a graph module, and no component is cyclic.
    for (const auto& module : component.modules()) {
      bool present = false;
      for (const auto& graphModule : graph.value().modules()) {
        if (graphModule.encode().asPtr() == module.encode().asPtr()) { present = true; }
      }
      ZC_EXPECT(present);
    }
    ZC_EXPECT(!component.cyclic());
    covered += component.modules().size();
  }
  ZC_EXPECT(covered == graph.value().modules().size());

  // Both records are the closed canonical form of what they claim to encode.
  auto graphCodec = ModuleGraphRecord::decodeCanonical(graph.value().encodeCanonical().asPtr());
  ZC_REQUIRE(graphCodec != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(graphCodec).encodeCanonical().asPtr() ==
            graph.value().encodeCanonical().asPtr());
  auto sccCodec = ModuleGraphSccRecord::decodeCanonical(components.encodeCanonical().asPtr());
  ZC_REQUIRE(sccCodec != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(sccCodec).encodeCanonical().asPtr() ==
            components.encodeCanonical().asPtr());
}

// A missing dependency is a semantic failure of the graph, and it is selected
// globally: a nested requester's failure must win over an unsatisfiable edge that
// sorts earlier. Reporting the earlier edge would blame a module whose own inputs
// are fine.
ZC_TEST("Nested dependency failure globally precedes an earlier outside edge") {
  TransactionFixture fixture;
  const auto& key = fixture.address();
  basic::ThreadPool scheduler(2);
  auto database = queryDatabase(scheduler);

  auto transaction = fixture.prepare(VerifiedModuleGraphInputLedger::empty());
  ZC_REQUIRE(transaction != zc::none);
  ZC_REQUIRE(ZC_REQUIRE_NONNULL(transaction).commit(database).isCommitted());
  ZC_REQUIRE(fixture.stageSidecarInputs(database));

  auto roots =
      incremental_binding_query::CompilationRootSetQueryKey::singletonToolchainCore(key.coreKey);
  ZC_REQUIRE(roots != zc::none);
  // The core root sorts before the prelude, so a graph that walked its edges first
  // would report the root. Declare the prelude's own dependency missing and the
  // failure must instead name the prelude.
  auto preludeMissing = identity::ModuleKey::from(key.coreKey.clone(), [&] {
    zc::Vector<identity::ModulePathSegment> path;
    path.add(scalar<identity::ModulePathSegment>("prelude"_zc));
    path.add(scalar<identity::ModulePathSegment>("missing"_zc));
    return zc::mv(path);
  }());
  ZC_REQUIRE(preludeMissing != zc::none);

  auto mutation = database.beginInputTransaction(database.snapshot().revision());
  ZC_REQUIRE(mutation.isOpened());
  {
    auto write = zc::mv(mutation).takeTransaction();
    zc::Vector<DetachedModuleDependencySite> sites;
    zc::Vector<identity::ModulePathSegment> missingPath;
    missingPath.add(scalar<identity::ModulePathSegment>("missing"_zc));
    auto site = DetachedModuleDependencySite::from(DetachedModuleDependencySiteKind::Import,
                                                   zc::mv(missingPath), 0);
    ZC_REQUIRE(site != zc::none);
    sites.add(zc::mv(ZC_REQUIRE_NONNULL(site)));
    // Re-stamping the site set needs the prelude's committed digest; a substituted
    // digest would make the family inconsistent and the graph would reject earlier.
    auto committedSites = database.snapshot().get<ModuleDependencySiteInput>(key.prelude);
    ZC_REQUIRE(committedSites.kind() == query::QueryValueKind::Value);
    auto updated =
        DetachedModuleDependencySiteSet::from(key.prelude.clone(), key.preludeFile.clone(),
                                              committedSites.value().sourceDigest(), zc::mv(sites));
    ZC_REQUIRE(updated != zc::none);
    ZC_REQUIRE(
        write.set<ModuleDependencySiteInput>(key.prelude, ZC_REQUIRE_NONNULL(updated)).isApplied());
    auto alias = identity::DependencyAlias::fromCanonical("missing"_zc);
    ZC_REQUIRE(alias != zc::none);
    auto aliasKey = resolution_query::DependencyAliasRootQueryKey::from(
        key.coreKey.clone(), zc::mv(ZC_REQUIRE_NONNULL(alias)));
    ZC_REQUIRE(aliasKey != zc::none);
    ZC_REQUIRE(
        write
            .set<resolution_query::DependencyAliasRootInput>(
                ZC_REQUIRE_NONNULL(aliasKey), resolution_query::ExplicitModuleTarget::absent())
            .isApplied());
    // Resolution demands an explicit present-or-absent catalog bucket for every
    // path it considers. The prelude's ancestry is `[core::prelude, core]`, so the
    // candidates are `core::prelude::missing`, `core::missing`, and the bare
    // `missing`. Recording all three absent is what makes this a *missing*
    // dependency rather than an unrepresentable resolution request.
    const auto stageAbsentBucket = [&](zc::Vector<identity::ModulePathSegment>&& path) {
      auto bucketKey =
          identity::ModuleCatalogPathBucketKey::from(key.coreKey.clone(), zc::mv(path));
      ZC_REQUIRE(bucketKey != zc::none);
      auto bucket =
          identity::ModuleCatalogPathBucket::absent(ZC_REQUIRE_NONNULL(bucketKey).clone());
      auto canonical = resolution_query::CanonicalModuleCatalogBucket::fromVerified(zc::mv(bucket));
      ZC_REQUIRE(write
                     .set<resolution_query::ModuleCatalogPathBucketInput>(
                         ZC_REQUIRE_NONNULL(bucketKey), zc::mv(canonical))
                     .isApplied());
    };
    stageAbsentBucket([&] {
      zc::Vector<identity::ModulePathSegment> path;
      path.add(scalar<identity::ModulePathSegment>("core"_zc));
      path.add(scalar<identity::ModulePathSegment>("prelude"_zc));
      path.add(scalar<identity::ModulePathSegment>("missing"_zc));
      return zc::mv(path);
    }());
    stageAbsentBucket([&] {
      zc::Vector<identity::ModulePathSegment> path;
      path.add(scalar<identity::ModulePathSegment>("core"_zc));
      path.add(scalar<identity::ModulePathSegment>("missing"_zc));
      return zc::mv(path);
    }());
    stageAbsentBucket([&] {
      zc::Vector<identity::ModulePathSegment> path;
      path.add(scalar<identity::ModulePathSegment>("missing"_zc));
      return zc::mv(path);
    }());
    ZC_REQUIRE(write.commit().isCommitted());
  }

  auto graph = database.snapshot().get<ModuleGraph>(ZC_REQUIRE_NONNULL(roots));
  ZC_REQUIRE(!graph.isRuntimeFailure());
  ZC_REQUIRE(graph.kind() == query::QueryValueKind::SemanticFailure);
  auto failure = ModuleDependencyFailureRecord::decodeCanonical(graph.semanticFailureBytes());
  ZC_REQUIRE(failure != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(failure).kind() == ModuleDependencyFailureKind::Missing);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(failure).request().requester().encode().asPtr() ==
            key.prelude.encode().asPtr());
}

}  // namespace zomlang::compiler::driver::module_graph_query
