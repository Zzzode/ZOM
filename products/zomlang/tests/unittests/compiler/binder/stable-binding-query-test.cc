// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "stable-header-test-fixture.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/binder/module-skeleton-query.h"
#include "zomlang/compiler/binder/stable-binding-codec.h"
#include "zomlang/compiler/binder/stable-header-verifier.h"

namespace zomlang::compiler::binder {
namespace {

CanonicalSequence<diagnostics::DiagnosticFact> noDiagnostics() {
  zc::Vector<diagnostics::DiagnosticFact> values;
  auto result = StableBindingSequenceBuilder<diagnostics::DiagnosticFact>::from(zc::mv(values));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

zc::Array<uint8_t> withTrailingByte(zc::ArrayPtr<const uint8_t> bytes) {
  auto result = zc::heapArray<uint8_t>(bytes.size() + 1);
  result.first(bytes.size()).copyFrom(bytes);
  result.back() = 0;
  return result;
}

}  // namespace

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

ZC_TEST("StableBindingQueryTest.HeaderDescriptorsUseExactCanonicalCodecs") {
  test::StableHeaderFixture fixture(test::stableHeaderSourceFile("descriptor-codecs"_zc));

  auto definition = fixture.definitionHeader("Box"_zc);
  auto definitionKeyBytes = DefinitionHeaderSyntax::encodeKey(definition.queryKey());
  auto decodedDefinitionKey = DefinitionHeaderSyntax::decodeKey(definitionKeyBytes.asPtr());
  ZC_REQUIRE(decodedDefinitionKey != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedDefinitionKey) == definition.queryKey());
  ZC_EXPECT(DefinitionHeaderSyntax::decodeKey(
                withTrailingByte(definitionKeyBytes.asPtr()).asPtr()) == zc::none);

  auto definitionResult = DefinitionHeaderSyntax::Value::value(zc::mv(definition), noDiagnostics());
  auto definitionValueBytes = DefinitionHeaderSyntax::encodeValue(definitionResult);
  auto decodedDefinitionValue = DefinitionHeaderSyntax::decodeValue(definitionValueBytes.asPtr());
  ZC_REQUIRE(decodedDefinitionValue != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedDefinitionValue) == definitionResult);
  ZC_EXPECT(DefinitionHeaderSyntax::decodeValue(
                withTrailingByte(definitionValueBytes.asPtr()).asPtr()) == zc::none);

  auto implementation = fixture.implementationHeader(0);
  auto implementationKeyBytes =
      ImplementationOccurrenceHeaderSyntax::encodeKey(implementation.queryKey());
  auto decodedImplementationKey =
      ImplementationOccurrenceHeaderSyntax::decodeKey(implementationKeyBytes.asPtr());
  ZC_REQUIRE(decodedImplementationKey != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedImplementationKey) == implementation.queryKey());
  ZC_EXPECT(ImplementationOccurrenceHeaderSyntax::decodeKey(
                withTrailingByte(implementationKeyBytes.asPtr()).asPtr()) == zc::none);

  auto implementationResult =
      ImplementationOccurrenceHeaderSyntax::Value::value(zc::mv(implementation), noDiagnostics());
  auto implementationValueBytes =
      ImplementationOccurrenceHeaderSyntax::encodeValue(implementationResult);
  auto decodedImplementationValue =
      ImplementationOccurrenceHeaderSyntax::decodeValue(implementationValueBytes.asPtr());
  ZC_REQUIRE(decodedImplementationValue != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedImplementationValue) == implementationResult);
  ZC_EXPECT(ImplementationOccurrenceHeaderSyntax::decodeValue(
                withTrailingByte(implementationValueBytes.asPtr()).asPtr()) == zc::none);

  ZC_EXPECT(DefinitionHeaderSyntax::descriptor.domain == "zom.query.definition-header-syntax"_zc);
  ZC_EXPECT(ImplementationOccurrenceHeaderSyntax::descriptor.domain ==
            "zom.query.implementation-occurrence-header-syntax"_zc);
  ZC_EXPECT(DefinitionHeaderSyntax::descriptor.retention == query::RetentionClass::Retained);
  ZC_EXPECT(ImplementationOccurrenceHeaderSyntax::descriptor.retention ==
            query::RetentionClass::Retained);
}

}  // namespace zomlang::compiler::binder
