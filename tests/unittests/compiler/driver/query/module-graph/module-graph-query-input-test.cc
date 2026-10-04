// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/driver/query/module-graph/module-graph-query.h"
#include "compiler/identity/canonical/canonical-encoder.h"
#include "compiler/identity/source/source-snapshot.h"
#include "tests/unittests/compiler/test-semantic-identities.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::driver::module_graph_query {
namespace {

using tests::test_identity_detail::coreCrate;
using tests::test_identity_detail::crate;
using tests::test_identity_detail::digest;
using tests::test_identity_detail::scalar;

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

}  // namespace zomlang::compiler::driver::module_graph_query
