// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zc/ztest/test.h"
#include "zomlang/compiler/binder/module-skeleton-query.h"
#include "zomlang/compiler/binder/stable-binding-codec.h"
#include "zomlang/compiler/binder/stable/header/verifier.h"
#include "zomlang/tests/unittests/compiler/binder/stable/header/fixture.h"

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

  zc::Maybe<MemberVisibility> definitionVisibility;
  ZC_IF_SOME(visibility, definition.visibility()) { definitionVisibility = visibility; }
  auto declaration = test::requireStableHeaderValue(StableDeclarationFact::from(
      definition.queryKey().clone(), definition.record().clone(),
      StableScopeOwnerKey::module(definition.queryKey().module().clone()), definition.kind(),
      definition.nameSpace(), definition.name().clone(), definition.activation(),
      zc::mv(definitionVisibility)));
  auto definitionBindingKeyBytes = DefinitionBindingHeader::encodeKey(definition.queryKey());
  auto decodedDefinitionBindingKey =
      DefinitionBindingHeader::decodeKey(definitionBindingKeyBytes.asPtr());
  ZC_REQUIRE(decodedDefinitionBindingKey != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedDefinitionBindingKey) == definition.queryKey());
  ZC_EXPECT(DefinitionBindingHeader::decodeKey(
                withTrailingByte(definitionBindingKeyBytes.asPtr()).asPtr()) == zc::none);
  auto definitionBindingBytes = DefinitionBindingHeader::encodeValue(declaration);
  auto decodedDefinitionBinding =
      DefinitionBindingHeader::decodeValue(definitionBindingBytes.asPtr());
  ZC_REQUIRE(decodedDefinitionBinding != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedDefinitionBinding) == declaration);
  ZC_EXPECT(DefinitionBindingHeader::decodeValue(
                withTrailingByte(definitionBindingBytes.asPtr()).asPtr()) == zc::none);

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

  auto implementationBindingKeyBytes =
      ImplementationBindingHeader::encodeKey(implementation.authority());
  auto decodedImplementationBindingKey =
      ImplementationBindingHeader::decodeKey(implementationBindingKeyBytes.asPtr());
  ZC_REQUIRE(decodedImplementationBindingKey != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedImplementationBindingKey) == implementation.authority());
  ZC_EXPECT(ImplementationBindingHeader::decodeKey(
                withTrailingByte(implementationBindingKeyBytes.asPtr()).asPtr()) == zc::none);

  auto occurrence = test::requireStableHeaderValue(StableImplementationOccurrenceFact::from(
      implementation.queryKey().clone(), implementation.authority().clone(),
      implementation.record().clone(),
      StableScopeOwnerKey::module(implementation.authority().module().clone())));
  zc::Vector<StableImplementationOccurrenceFact> occurrences;
  occurrences.add(zc::mv(occurrence));
  auto implementationBinding = test::requireStableHeaderValue(
      StableBindingSequenceBuilder<StableImplementationOccurrenceFact>::from(zc::mv(occurrences)));
  auto implementationBindingBytes = ImplementationBindingHeader::encodeValue(implementationBinding);
  auto decodedImplementationBinding =
      ImplementationBindingHeader::decodeValue(implementationBindingBytes.asPtr());
  ZC_REQUIRE(decodedImplementationBinding != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedImplementationBinding) == implementationBinding);
  ZC_EXPECT(ImplementationBindingHeader::decodeValue(
                withTrailingByte(implementationBindingBytes.asPtr()).asPtr()) == zc::none);

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
  ZC_EXPECT(ImplementationBindingHeader::descriptor.domain ==
            "zom.query.implementation-binding-header"_zc);
  ZC_EXPECT(ImplementationBindingHeader::descriptor.retention == query::RetentionClass::Retained);
  ZC_EXPECT(DefinitionBindingHeader::descriptor.domain == "zom.query.definition-binding-header"_zc);
  ZC_EXPECT(DefinitionBindingHeader::descriptor.retention == query::RetentionClass::Retained);
}

ZC_TEST("StableBindingQueryTest.LookupProjectionValuesUseExactCanonicalCodecs") {
  test::StableHeaderFixture fixture(test::stableHeaderSourceFile("projection-codecs"_zc));
  auto definition = fixture.definitionHeader("Box"_zc);
  auto name = test::requireStableHeaderValue(
      BindingNameKey::from(definition.nameSpace(), definition.name().clone()));

  auto nameBytes = StableBindingCodec<BindingNameKey>::encode(name);
  auto decodedName = StableBindingCodec<BindingNameKey>::decode(nameBytes.asPtr());
  ZC_REQUIRE(decodedName != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedName).nameSpace() == name.nameSpace());
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedName).name() == name.name());
  ZC_EXPECT(StableBindingCodec<BindingNameKey>::decode(
                withTrailingByte(nameBytes.asPtr()).asPtr()) == zc::none);

  zc::Vector<BindingNameKey> names;
  names.add(name.clone());
  auto exportNames = test::requireStableHeaderValue(
      StableBindingSequenceBuilder<BindingNameKey>::from(zc::mv(names)));
  auto exportNamesBytes =
      StableBindingCodec<CanonicalSequence<BindingNameKey>>::encode(exportNames);
  auto decodedExportNames =
      StableBindingCodec<CanonicalSequence<BindingNameKey>>::decode(exportNamesBytes.asPtr());
  ZC_REQUIRE(decodedExportNames != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedExportNames) == exportNames);
  ZC_EXPECT(StableBindingCodec<CanonicalSequence<BindingNameKey>>::decode(
                withTrailingByte(exportNamesBytes.asPtr()).asPtr()) == zc::none);

  auto exportedBindingKey =
      StableExportedBindingQueryKey::from(definition.queryKey().module().clone(), name.clone());
  auto exportedBindingKeyBytes = ExportedBinding::encodeKey(exportedBindingKey);
  auto decodedExportedBindingKey = ExportedBinding::decodeKey(exportedBindingKeyBytes.asPtr());
  ZC_REQUIRE(decodedExportedBindingKey != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedExportedBindingKey) == exportedBindingKey);
  ZC_EXPECT(ExportedBinding::decodeKey(withTrailingByte(exportedBindingKeyBytes.asPtr()).asPtr()) ==
            zc::none);
  zc::Maybe<MemberVisibility> exportedVisibility;
  ZC_IF_SOME(value, definition.visibility()) { exportedVisibility = value; }
  auto exportedBinding = test::requireStableHeaderValue(StableExportedBinding::from(
      name.clone(), StableBindingTargetKey::definition(definition.queryKey().clone()),
      StableBindingTargetKey::definition(definition.queryKey().clone()), zc::mv(exportedVisibility),
      true));
  auto exportedBindingBytes = ExportedBinding::encodeValue(exportedBinding);
  auto decodedExportedBinding = ExportedBinding::decodeValue(exportedBindingBytes.asPtr());
  ZC_REQUIRE(decodedExportedBinding != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedExportedBinding) == exportedBinding);
  ZC_EXPECT(ExportedBinding::decodeValue(withTrailingByte(exportedBindingBytes.asPtr()).asPtr()) ==
            zc::none);

  auto target = StableBindingTargetKey::definition(definition.queryKey().clone());
  zc::Vector<StableBindingTargetKey> targets;
  targets.add(target.clone());
  auto bucket = test::requireStableHeaderValue(
      StableBindingSequenceBuilder<StableBindingTargetKey>::from(zc::mv(targets)));
  auto bucketBytes = StableBindingCodec<CanonicalSequence<StableBindingTargetKey>>::encode(bucket);
  auto decodedBucket =
      StableBindingCodec<CanonicalSequence<StableBindingTargetKey>>::decode(bucketBytes.asPtr());
  ZC_REQUIRE(decodedBucket != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedBucket) == bucket);
  ZC_EXPECT(StableBindingCodec<CanonicalSequence<StableBindingTargetKey>>::decode(
                withTrailingByte(bucketBytes.asPtr()).asPtr()) == zc::none);

  auto visibilityBytes = StableBindingCodec<MemberVisibility>::encode(MemberVisibility::Public);
  auto decodedVisibility = StableBindingCodec<MemberVisibility>::decode(visibilityBytes.asPtr());
  ZC_REQUIRE(decodedVisibility != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedVisibility) == MemberVisibility::Public);
  ZC_EXPECT(StableBindingCodec<MemberVisibility>::decode(
                withTrailingByte(visibilityBytes.asPtr()).asPtr()) == zc::none);

  auto visibilityKeyBytes = BindingVisibility::encodeKey(target);
  auto decodedVisibilityKey = BindingVisibility::decodeKey(visibilityKeyBytes.asPtr());
  ZC_REQUIRE(decodedVisibilityKey != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedVisibilityKey) == target);
  ZC_EXPECT(BindingVisibility::decodeKey(withTrailingByte(visibilityKeyBytes.asPtr()).asPtr()) ==
            zc::none);
  zc::Maybe<MemberVisibility> publicVisibility = MemberVisibility::Public;
  auto bindingVisibilityBytes = BindingVisibility::encodeValue(publicVisibility);
  auto decodedBindingVisibility = BindingVisibility::decodeValue(bindingVisibilityBytes.asPtr());
  ZC_REQUIRE(decodedBindingVisibility != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedBindingVisibility) == publicVisibility);
  ZC_EXPECT(BindingVisibility::decodeValue(
                withTrailingByte(bindingVisibilityBytes.asPtr()).asPtr()) == zc::none);
  zc::Maybe<MemberVisibility> noVisibility;
  auto noVisibilityBytes = BindingVisibility::encodeValue(noVisibility);
  auto decodedNoVisibility = BindingVisibility::decodeValue(noVisibilityBytes.asPtr());
  ZC_REQUIRE(decodedNoVisibility != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedNoVisibility) == noVisibility);

  zc::Maybe<StableImportFact> noImport;
  auto noImportBytes = ImportTarget::encodeValue(noImport);
  auto decodedNoImport = ImportTarget::decodeValue(noImportBytes.asPtr());
  ZC_REQUIRE(decodedNoImport != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedNoImport) == noImport);
  ZC_EXPECT(ImportTarget::decodeValue(withTrailingByte(noImportBytes.asPtr()).asPtr()) == zc::none);

  ZC_EXPECT(ExportedBinding::descriptor.domain == "zom.query.exported-binding"_zc);
  ZC_EXPECT(ExportedBinding::descriptor.retention == query::RetentionClass::Retained);
  ZC_EXPECT(ImportTarget::descriptor.domain == "zom.query.import-target"_zc);
  ZC_EXPECT(ImportTarget::descriptor.retention == query::RetentionClass::Retained);
  ZC_EXPECT(BindingVisibility::descriptor.domain == "zom.query.binding-visibility"_zc);
  ZC_EXPECT(BindingVisibility::descriptor.retention == query::RetentionClass::Retained);
}

}  // namespace zomlang::compiler::binder
