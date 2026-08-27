// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/stable/header/verifier.h"

#include "zc/ztest/test.h"
#include "zomlang/tests/unittests/compiler/binder/stable/header/fixture.h"

namespace zomlang::compiler::binder {
namespace {

identity::ModuleKey foreignModule() {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(tests::test_identity_detail::scalar<identity::ModulePathSegment>("foreign"_zc));
  return test::requireStableHeaderValue(
      identity::ModuleKey::from(tests::test_identity_detail::crate(), zc::mv(path)));
}

}  // namespace

ZC_TEST("StableHeaderVerifier selects definition authority from complete context") {
  test::StableHeaderFixture fixture(test::stableHeaderSourceFile("definition"_zc));
  auto header = fixture.definitionHeader("Box"_zc);
  const auto& entry = fixture.definition("Box"_zc);
  auto query =
      StableDefinitionQueryKey::from(tests::test_identity_detail::module(), entry.key().clone());

  ZC_EXPECT(StableHeaderVerifier::verifyDefinition(fixture.context(), query, header));

  auto foreignQuery = StableDefinitionQueryKey::from(foreignModule(), entry.key().clone());
  ZC_EXPECT(!StableHeaderVerifier::verifyDefinition(fixture.context(), foreignQuery, header));
}

ZC_TEST("StableHeaderVerifier verifies every equal implementation occurrence independently") {
  test::StableHeaderFixture fixture(test::stableHeaderSourceFile("implementations"_zc));
  auto first = fixture.implementationHeader(0);
  auto second = fixture.implementationHeader(1);
  auto firstQuery = test::requireStableHeaderValue(StableImplementationOccurrenceQueryKey::from(
      tests::test_identity_detail::module(), first.queryKey().occurrence().clone()));
  auto secondQuery = test::requireStableHeaderValue(StableImplementationOccurrenceQueryKey::from(
      tests::test_identity_detail::module(), second.queryKey().occurrence().clone()));

  ZC_EXPECT(first.record().encode().asPtr() == second.record().encode().asPtr());
  ZC_EXPECT(first.queryKey().occurrence().implementation() ==
            second.queryKey().occurrence().implementation());
  ZC_EXPECT(!first.queryKey().occurrence().sameAs(second.queryKey().occurrence()));
  ZC_EXPECT(
      StableHeaderVerifier::verifyImplementationOccurrence(fixture.context(), firstQuery, first));
  ZC_EXPECT(
      StableHeaderVerifier::verifyImplementationOccurrence(fixture.context(), secondQuery, second));
  ZC_EXPECT(
      !StableHeaderVerifier::verifyImplementationOccurrence(fixture.context(), firstQuery, second));
  ZC_EXPECT(
      !StableHeaderVerifier::verifyImplementationOccurrence(fixture.context(), secondQuery, first));
}

ZC_TEST("StableHeaderVerifier rejects a candidate from another complete source context") {
  test::StableHeaderFixture selected(test::stableHeaderSourceFile("selected"_zc));
  test::StableHeaderFixture foreign(test::stableHeaderSourceFile("foreign"_zc));
  auto definition = foreign.definitionHeader("Box"_zc);
  const auto& definitionEntry = selected.definition("Box"_zc);
  auto definitionQuery = StableDefinitionQueryKey::from(tests::test_identity_detail::module(),
                                                        definitionEntry.key().clone());
  ZC_EXPECT(
      !StableHeaderVerifier::verifyDefinition(selected.context(), definitionQuery, definition));

  auto implementation = foreign.implementationHeader(0);
  auto implementationQuery =
      test::requireStableHeaderValue(StableImplementationOccurrenceQueryKey::from(
          tests::test_identity_detail::module(),
          selected.implementationSite(0).occurrence().clone()));
  ZC_EXPECT(!StableHeaderVerifier::verifyImplementationOccurrence(
      selected.context(), implementationQuery, implementation));
}

}  // namespace zomlang::compiler::binder
