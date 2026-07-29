// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "stable-header-test-fixture.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/binder/stable-binding-codec.h"
#include "zomlang/compiler/binder/stable-header-verifier.h"

namespace zomlang::compiler::binder {

ZC_TEST("StableBindingQueryTest.HeadersRejectFieldAndParameterMutations") {
  test::StableHeaderFixture fixture(test::stableHeaderSourceFile("mutations"_zc));
  auto definition = fixture.definitionHeader("Box"_zc);
  zc::Vector<StableHeaderGenericParameter> noGenericValues;
  auto noGenericParameters = test::requireStableHeaderValue(
      StableBindingSequenceBuilder<StableHeaderGenericParameter>::from(zc::mv(noGenericValues)));
  zc::Maybe<MemberVisibility> visibility;
  ZC_IF_SOME(value, definition.visibility()) { visibility = value; }
  auto mutatedDefinition = test::requireStableHeaderValue(StableDefinitionHeader::from(
      definition.queryKey().clone(), definition.record().clone(),
      definition.authoritySite().clone(), definition.kind(), definition.nameSpace(),
      definition.name().clone(), DefinitionActivation::ImportSurface, zc::mv(visibility),
      definition.bodyDisposition(), zc::mv(noGenericParameters),
      definition.callableParameters().clone(), definition.declaredScopeRoles().clone()));
  ZC_EXPECT(!StableHeaderVerifier::verifyDefinition(fixture.context(), definition.queryKey(),
                                                    mutatedDefinition));

  auto implementation = fixture.implementationHeader(0);
  zc::Vector<StableHeaderGenericParameter> noImplementationGenericValues;
  auto noImplementationGenericParameters = test::requireStableHeaderValue(
      StableBindingSequenceBuilder<StableHeaderGenericParameter>::from(
          zc::mv(noImplementationGenericValues)));
  auto mutatedImplementation =
      test::requireStableHeaderValue(StableImplementationOccurrenceHeader::from(
          implementation.queryKey().clone(), implementation.authority().clone(),
          implementation.record().clone(), zc::mv(noImplementationGenericParameters),
          implementation.declaredScopeRoles().clone(), ImplementationSourceForm::BodylessMarker));
  ZC_EXPECT(!StableHeaderVerifier::verifyImplementationOccurrence(
      fixture.context(), implementation.queryKey(), mutatedImplementation));
}

}  // namespace zomlang::compiler::binder
