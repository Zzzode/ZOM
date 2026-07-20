// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/incremental-module-resolution-query.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/basic/thread-pool.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::driver::incremental_module_resolution_query {
namespace {

using tests::test_identity_detail::crate;
using tests::test_identity_detail::package;
using tests::test_identity_detail::scalar;

identity::SourceFileKey source(zc::StringPtr name) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(scalar<identity::CanonicalPathSegment>(name));
  return identity::SourceFileKey::from(
      crate(), identity::SourceOriginKey::localFile(
                   identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments))));
}

identity::ModuleKey module(zc::StringPtr first, zc::StringPtr second = nullptr) {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(scalar<identity::ModulePathSegment>(first));
  if (second != nullptr) { path.add(scalar<identity::ModulePathSegment>(second)); }
  auto result = identity::ModuleKey::from(crate(), zc::mv(path));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::SemanticContextBrand context(identity::SemanticContextFactory& factory) {
  auto result = factory.issue();
  return ZC_REQUIRE_NONNULL(result);
}

identity::SemanticIdentityRegistrySet registries(identity::SemanticContextFactory& factory,
                                                 identity::SemanticContextBrand owner) {
  auto result = identity::SemanticIdentityRegistrySet::create(factory, owner);
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

struct Fixture final {
  Fixture() : owner(context(factory)), identities(registries(factory, owner)) {
    ZC_REQUIRE(identities.collectPackage(package()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(identities.freezePackages() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(identities.collectCrate(crate()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(identities.freezeCrates() == identity::FrozenRegistryFailure::None);
    for (const auto name : {"root.zom"_zc, "child.zom"_zc, "unrelated.zom"_zc}) {
      auto snapshot = identity::ImmutableSourceSnapshot::from(
          source(name), zc::heapArray<uint8_t>(16, uint8_t{0x41}));
      ZC_REQUIRE(snapshot != zc::none);
      ZC_IF_SOME(value, snapshot) {
        ZC_REQUIRE(identities.collectSourceFile(zc::mv(value)) ==
                   identity::FrozenRegistryFailure::None);
      }
    }
    ZC_REQUIRE(identities.freezeSourceFiles() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(identities.collectModule(module("root"_zc)) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(identities.collectModule(module("root"_zc, "child"_zc)) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(identities.collectModule(module("root"_zc, "unrelated"_zc)) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(identities.freezeModules() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(identities.freezeStableIdentities() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(identities.freezeGenericParameters() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(identities.freezeCallableParameters() == identity::FrozenRegistryFailure::None);

    root = ZC_REQUIRE_NONNULL(identities.modules().find(module("root"_zc)));
    child = ZC_REQUIRE_NONNULL(identities.modules().find(module("root"_zc, "child"_zc)));
    unrelated = ZC_REQUIRE_NONNULL(identities.modules().find(module("root"_zc, "unrelated"_zc)));

    zc::Vector<identity::CanonicalPathSegment> noRootSegments;
    zc::Vector<binder::ModuleSearchRoot> searchRoots;
    searchRoots.add(binder::ModuleSearchRoot::workspace(
        crate(), identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(noRootSegments))));
    zc::Vector<binder::ModuleSourceSnapshotRevision> snapshots;
    for (const auto& snapshot : identities.sourceSnapshots()) {
      snapshots.add(binder::ModuleSourceSnapshotRevision(snapshot.source().clone(),
                                                         snapshot.contentDigest()));
    }
    zc::Vector<binder::GeneratedModuleSourceRevision> generated;
    zc::Vector<binder::ModuleDependencyAliasRoot> aliases;
    zc::Vector<binder::RequesterModuleAncestryCandidate> ancestry;
    ancestry.add(binder::RequesterModuleAncestryCandidate(module("root"_zc),
                                                          moduleChain(module("root"_zc))));
    ancestry.add(binder::RequesterModuleAncestryCandidate(
        module("root"_zc, "child"_zc),
        moduleChain(module("root"_zc, "child"_zc), module("root"_zc))));
    ancestry.add(binder::RequesterModuleAncestryCandidate(
        module("root"_zc, "unrelated"_zc),
        moduleChain(module("root"_zc, "unrelated"_zc), module("root"_zc))));
    zc::Vector<binder::StructuralModuleCatalogEntry> catalog;
    catalog.add(
        binder::StructuralModuleCatalogEntry(module("root"_zc), root, source("root.zom"_zc)));
    catalog.add(binder::StructuralModuleCatalogEntry(module("root"_zc, "child"_zc), child,
                                                     source("child.zom"_zc)));
    catalog.add(binder::StructuralModuleCatalogEntry(module("root"_zc, "unrelated"_zc), unrelated,
                                                     source("unrelated.zom"_zc)));
    auto frozen = binder::StructuralModuleResolver::freeze(
        owner, identities,
        binder::ModuleResolutionEnvironmentRecord(zc::mv(searchRoots), zc::mv(snapshots),
                                                  zc::mv(generated), zc::mv(aliases),
                                                  zc::mv(ancestry)),
        zc::mv(catalog));
    ZC_REQUIRE(frozen.is<binder::StructuralModuleResolver>());
    resolverValue = zc::mv(frozen.get<binder::StructuralModuleResolver>());
  }

  static zc::Vector<identity::ModuleKey> moduleChain(identity::ModuleKey&& first) {
    zc::Vector<identity::ModuleKey> result;
    result.add(zc::mv(first));
    return result;
  }

  static zc::Vector<identity::ModuleKey> moduleChain(identity::ModuleKey&& first,
                                                     identity::ModuleKey&& second) {
    auto result = moduleChain(zc::mv(first));
    result.add(zc::mv(second));
    return result;
  }

  binder::ModuleDependencyRequest request() const {
    const auto& resolver = ZC_REQUIRE_NONNULL(resolverValue);
    zc::Vector<identity::ModulePathSegment> path;
    path.add(scalar<identity::ModulePathSegment>("child"_zc));
    auto key = resolver.resolutionKey(root, identity::ModuleDependencyKind::Import, zc::mv(path));
    ZC_REQUIRE(key != zc::none);
    zc::Maybe<identity::SourceSpan> span;
    for (const auto& snapshot : identities.sourceSnapshots()) {
      if (snapshot.source().sameAs(source("root.zom"_zc))) { span = snapshot.span(0, 1); }
    }
    ZC_REQUIRE(span != zc::none);
    zc::Vector<binder::ModuleSyntaxDependencySite> sites;
    ZC_IF_SOME(value, span) {
      sites.add(binder::ModuleSyntaxDependencySite(ast::NodeId(1), zc::mv(value), 0));
    }
    zc::Maybe<binder::ModuleDependencyRequest> result;
    ZC_IF_SOME(value, key) {
      result = binder::ModuleDependencyRequest::source(
          root, zc::mv(value), resolver.environmentRevision(), zc::mv(sites));
    }
    return zc::mv(ZC_REQUIRE_NONNULL(result));
  }

  binder::StructuralModuleResolver& resolver() { return ZC_REQUIRE_NONNULL(resolverValue); }

  identity::SemanticContextFactory factory;
  identity::SemanticContextBrand owner;
  identity::SemanticIdentityRegistrySet identities;
  identity::ModuleId root;
  identity::ModuleId child;
  identity::ModuleId unrelated;
  zc::Maybe<binder::StructuralModuleResolver> resolverValue;
};

query::QueryDatabase database(basic::ThreadPool& scheduler) {
  query::QueryDatabase result(scheduler);
  ZC_REQUIRE(registerIncrementalModuleResolutionQueries(result));
  return result;
}

ZC_TEST("ResolveModuleRequestQuery stages and demands exact candidates") {
  Fixture fixture;
  basic::ThreadPool scheduler(4);
  auto queries = database(scheduler);
  zc::Vector<binder::ModuleDependencyRequest> requests;
  requests.add(fixture.request());
  auto pending = queries.beginInputTransaction();
  ZC_REQUIRE(pending != zc::none);
  ZC_IF_SOME(transaction, pending) {
    ZC_REQUIRE(stageModuleResolutionQueryInputs(transaction, fixture.resolver(), requests.asPtr()));
    ZC_REQUIRE(transaction.commit() != zc::none);
  }
  auto snapshot = queries.snapshot();
  auto candidates = snapshot.get<ResolveModuleRequestQuery>(requests[0].key());
  ZC_REQUIRE(!candidates.isRuntimeFailure());
  ZC_REQUIRE(candidates.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(candidates.value().candidates().size() == 1);
  ZC_EXPECT(candidates.value().candidates()[0].encode().asPtr() ==
            module("root"_zc, "child"_zc).encode().asPtr());
  auto resolution =
      fixture.resolver().materializeQueryResolution(zc::mv(requests[0]), candidates.value());
  ZC_REQUIRE(resolution.is<binder::ModulePathResolution>());
  ZC_REQUIRE(resolution.get<binder::ModulePathResolution>().is<binder::ResolvedModulePath>());
  ZC_EXPECT(
      resolution.get<binder::ModulePathResolution>().get<binder::ResolvedModulePath>().target ==
      fixture.child);
}

ZC_TEST("ResolveModuleRequestQuery fails closed when exact inputs are absent") {
  Fixture fixture;
  basic::ThreadPool scheduler(2);
  auto queries = database(scheduler);
  auto request = fixture.request();
  auto result = queries.snapshot().get<ResolveModuleRequestQuery>(request.key());
  ZC_EXPECT(result.isRuntimeFailure());
}

ZC_TEST("ResolveModuleRequestQuery shields unrelated catalog bucket changes") {
  Fixture fixture;
  basic::ThreadPool scheduler(4);
  auto queries = database(scheduler);
  zc::Vector<binder::ModuleDependencyRequest> requests;
  requests.add(fixture.request());
  auto pending = queries.beginInputTransaction();
  ZC_REQUIRE(pending != zc::none);
  ZC_IF_SOME(transaction, pending) {
    ZC_REQUIRE(stageModuleResolutionQueryInputs(transaction, fixture.resolver(), requests.asPtr()));
    ZC_REQUIRE(transaction.commit() != zc::none);
  }
  auto first = queries.snapshot();
  auto firstResult = first.get<ResolveModuleRequestQuery>(requests[0].key());
  auto firstMetadata = first.metadata<ResolveModuleRequestQuery>(requests[0].key());
  ZC_REQUIRE(!firstResult.isRuntimeFailure());
  ZC_REQUIRE(firstMetadata != zc::none);

  zc::Vector<identity::ModulePathSegment> unrelatedPath;
  unrelatedPath.add(scalar<identity::ModulePathSegment>("root"_zc));
  unrelatedPath.add(scalar<identity::ModulePathSegment>("unrelated"_zc));
  auto unrelatedKey = identity::ModuleCatalogPathBucketKey::from(crate(), zc::mv(unrelatedPath));
  ZC_REQUIRE(unrelatedKey != zc::none);
  auto update = queries.beginInputTransaction();
  ZC_REQUIRE(update != zc::none);
  ZC_IF_SOME(transaction, update) {
    ZC_IF_SOME(key, unrelatedKey) {
      auto absent = identity::ModuleCatalogPathBucket::absent(key.clone());
      auto value = CanonicalModuleCatalogBucket::fromVerified(absent);
      ZC_REQUIRE(transaction.set<ModuleCatalogPathBucketInput>(key, value));
    }
    ZC_REQUIRE(transaction.commit() != zc::none);
  }

  auto second = queries.snapshot();
  auto secondResult = second.get<ResolveModuleRequestQuery>(requests[0].key());
  auto secondMetadata = second.metadata<ResolveModuleRequestQuery>(requests[0].key());
  ZC_REQUIRE(!secondResult.isRuntimeFailure());
  ZC_REQUIRE(secondMetadata != zc::none);
  ZC_EXPECT(secondResult.value().encode().asPtr() == firstResult.value().encode().asPtr());
  ZC_IF_SOME(firstValue, firstMetadata) {
    ZC_IF_SOME(secondValue, secondMetadata) {
      ZC_EXPECT(secondValue.changedAt() == firstValue.changedAt());
      ZC_EXPECT(secondValue.verifiedAt() > firstValue.verifiedAt());
    }
  }
}

}  // namespace
}  // namespace zomlang::compiler::driver::incremental_module_resolution_query
