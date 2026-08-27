// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/driver/query/binding/active-identity-membership-query.h"

#include "zc/ztest/test.h"
#include "tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::driver::incremental_binding_query {
namespace {

template <typename Value>
Value require(zc::Maybe<Value>&& value) {
  return zc::mv(ZC_REQUIRE_NONNULL(value));
}

zc::Array<uint8_t> withTrailingByte(zc::ArrayPtr<const uint8_t> bytes) {
  auto result = zc::heapArray<uint8_t>(bytes.size() + 1);
  result.first(bytes.size()).copyFrom(bytes);
  result.back() = 0;
  return result;
}

identity::CanonicalNameReference name(zc::StringPtr text) {
  zc::Vector<identity::SemanticIdentifier> suffix;
  suffix.add(tests::test_identity_detail::scalar<identity::SemanticIdentifier>(text));
  return require(identity::CanonicalNameReference::from(identity::CanonicalNameRoot::relative(),
                                                        zc::mv(suffix)));
}

identity::DeclaredDefinitionName declaredName(zc::StringPtr text) {
  return tests::test_identity_detail::scalar<identity::DeclaredDefinitionName>(text);
}

identity::DefinitionIdentityRecord definitionRecord() {
  zc::Vector<identity::EnclosingStableOwnerKey> owners;
  uint8_t digestBytes[32] = {};
  digestBytes[0] = 1;
  auto overload = require(identity::OverloadHeaderDigest::fromBytes(zc::arrayPtr(digestBytes)));
  return require(identity::DefinitionIdentityRecord::from(
      tests::test_identity_detail::module(), zc::mv(owners), identity::DefinitionKind::Function,
      identity::DefinitionNamespace::Value, declaredName("function"_zc), zc::mv(overload)));
}

identity::ImplIdentityRecord implementationRecord() {
  zc::Vector<identity::CanonicalHeaderTypeSyntax> arguments;
  auto trait =
      require(identity::CanonicalTraitReference::from(name("Trait"_zc), zc::mv(arguments)));
  zc::Vector<identity::CanonicalGenericParameter> generics;
  zc::Vector<identity::CanonicalBoundObligation> obligations;
  auto self =
      require(identity::CanonicalHeaderTypeSyntax::predefined(identity::PredefinedTypeKind::I32));
  auto header = require(identity::ImplHeader::from(
      zc::mv(generics), identity::ImplPolarity::Positive, identity::ImplSafety::Safe, zc::mv(trait),
      zc::mv(self), zc::mv(obligations)));
  zc::Vector<identity::EnclosingStableOwnerKey> owners;
  return identity::ImplIdentityRecord::from(tests::test_identity_detail::module(), zc::mv(owners),
                                            zc::mv(header));
}

binder::IdentitySyntaxSiteKey headerSite(uint32_t component) {
  zc::Vector<uint32_t> path;
  path.add(component);
  return require(binder::IdentitySyntaxSiteKey::from(
      tests::test_identity_detail::module(), tests::test_identity_detail::source(), zc::mv(path)));
}

CompilationRootSetQueryKey contextRoots() {
  return require(
      CompilationRootSetQueryKey::singletonToolchainCore(tests::test_identity_detail::coreCrate()));
}

struct ImplementationFixture final {
  identity::ImplIdentityRecord record;
  binder::StableImplementationQueryKey query;
  binder::StableImplementationOccurrenceQueryKey occurrence;
};

ImplementationFixture implementationFixture() {
  auto record = implementationRecord();
  auto implementation = identity::ImplKey::compute(record);
  auto occurrence = binder::ImplSourceOccurrenceKey::from(implementation.clone(), headerSite(1));
  auto occurrenceKey = require(binder::StableImplementationOccurrenceQueryKey::from(
      tests::test_identity_detail::module(), zc::mv(occurrence)));
  return ImplementationFixture{record.clone(),
                               binder::StableImplementationQueryKey::from(
                                   tests::test_identity_detail::module(), zc::mv(implementation)),
                               zc::mv(occurrenceKey)};
}

}  // namespace

ZC_TEST("ActiveCompilationUnitMembership canonicalizes active crates") {
  auto crate = tests::test_identity_detail::crate();
  zc::Vector<identity::CrateKey> crates;
  crates.add(crate.clone());
  auto membership = ActiveCompilationUnitMembership::from(tests::test_identity_detail::userUnit(),
                                                          zc::mv(crates));
  ZC_REQUIRE(membership != zc::none);

  auto encoded = ZC_REQUIRE_NONNULL(membership).encodeCanonical();
  auto decoded = ActiveCompilationUnitMembership::decodeCanonical(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded) == ZC_REQUIRE_NONNULL(membership));
  ZC_EXPECT(ZC_REQUIRE_NONNULL(membership).clone() == ZC_REQUIRE_NONNULL(membership));
  ZC_EXPECT(ZC_REQUIRE_NONNULL(membership).activeCrates().size() == 1);
  ZC_EXPECT(ActiveCompilationUnitMembership::decodeCanonical(
                withTrailingByte(encoded.asPtr()).asPtr()) == zc::none);
}

ZC_TEST("ActiveCompilationUnitMembership rejects empty duplicate and foreign crates") {
  zc::Vector<identity::CrateKey> empty;
  ZC_EXPECT(ActiveCompilationUnitMembership::from(tests::test_identity_detail::userUnit(),
                                                  zc::mv(empty)) == zc::none);

  auto crate = tests::test_identity_detail::crate();
  zc::Vector<identity::CrateKey> duplicates;
  duplicates.add(crate.clone());
  duplicates.add(crate.clone());
  ZC_EXPECT(ActiveCompilationUnitMembership::from(tests::test_identity_detail::userUnit(),
                                                  zc::mv(duplicates)) == zc::none);

  zc::Vector<identity::CrateKey> foreign;
  foreign.add(tests::test_identity_detail::coreCrate());
  ZC_EXPECT(ActiveCompilationUnitMembership::from(tests::test_identity_detail::userUnit(),
                                                  zc::mv(foreign)) == zc::none);
}

ZC_TEST("ActiveMembershipResult preserves active and inactive crate states") {
  constexpr zc::StringPtr domain = "zom.test.active-membership"_zc;
  auto active = ActiveMembershipResult<identity::CrateKey>::active(
      domain, tests::test_identity_detail::crate());
  ZC_REQUIRE(active != zc::none);
  auto encoded = ZC_REQUIRE_NONNULL(active).encodeCanonical(domain);
  auto decoded =
      ActiveMembershipResult<identity::CrateKey>::decodeCanonical(domain, encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded).isActive());
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded).record().encode().asPtr() ==
            tests::test_identity_detail::crate().encode().asPtr());

  auto inactive = ActiveMembershipResult<identity::CrateKey>::inactive();
  auto inactiveEncoded = inactive.encodeCanonical(domain);
  auto inactiveDecoded =
      ActiveMembershipResult<identity::CrateKey>::decodeCanonical(domain, inactiveEncoded.asPtr());
  ZC_REQUIRE(inactiveDecoded != zc::none);
  ZC_EXPECT(!ZC_REQUIRE_NONNULL(inactiveDecoded).isActive());
}

ZC_TEST("ActiveImplementationMembershipRecord seals its authority occurrence") {
  auto fixture = implementationFixture();
  zc::Vector<binder::StableImplementationOccurrenceQueryKey> occurrences;
  occurrences.add(fixture.occurrence.clone());
  auto membership =
      ActiveImplementationMembershipRecord::from(fixture.query.clone(), fixture.record.clone(),
                                                 fixture.occurrence.clone(), zc::mv(occurrences));
  ZC_REQUIRE(membership != zc::none);

  auto encoded = ZC_REQUIRE_NONNULL(membership).encodeCanonical();
  auto decoded = ActiveImplementationMembershipRecord::decodeCanonical(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded).encodeCanonical().asPtr() == encoded.asPtr());
  ZC_EXPECT(ActiveImplementationMembershipRecord::decodeCanonical(
                withTrailingByte(encoded.asPtr()).asPtr()) == zc::none);

  zc::Vector<binder::StableImplementationOccurrenceQueryKey> duplicated;
  duplicated.add(fixture.occurrence.clone());
  duplicated.add(fixture.occurrence.clone());
  ZC_EXPECT(ActiveImplementationMembershipRecord::from(
                fixture.query.clone(), fixture.record.clone(), fixture.occurrence.clone(),
                zc::mv(duplicated)) == zc::none);
}

ZC_TEST("ActiveGenericParameterMembership seals implementation ownership") {
  auto fixture = implementationFixture();
  zc::Vector<binder::StableImplementationOccurrenceQueryKey> occurrences;
  occurrences.add(fixture.occurrence.clone());
  auto authority = ImplementationGenericAuthority::from(
      fixture.query.clone(), fixture.occurrence.clone(), zc::mv(occurrences));
  ZC_REQUIRE(authority != zc::none);

  auto record = identity::GenericParameterIdentityRecord::type(
      identity::StableGenericParameterOwnerKey::implementation(
          fixture.query.implementation().clone()),
      0);
  auto query = binder::StableGenericParameterQueryKey::from(
      tests::test_identity_detail::module(), identity::GenericParameterKey::compute(record));
  auto membership = ActiveGenericParameterMembership::from(
      zc::mv(query), record.clone(),
      ActiveGenericParameterOwner::implementation(zc::mv(ZC_REQUIRE_NONNULL(authority))), 0);
  ZC_REQUIRE(membership != zc::none);

  auto encoded = ZC_REQUIRE_NONNULL(membership).encodeCanonical();
  auto decoded = ActiveGenericParameterMembership::decodeCanonical(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded).encodeCanonical().asPtr() == encoded.asPtr());

  zc::Vector<binder::StableImplementationOccurrenceQueryKey> repeated;
  repeated.add(fixture.occurrence.clone());
  auto repeatedAuthority = ImplementationGenericAuthority::from(
      fixture.query.clone(), fixture.occurrence.clone(), zc::mv(repeated));
  ZC_REQUIRE(repeatedAuthority != zc::none);
  auto wrongOrdinal = ActiveGenericParameterMembership::from(
      binder::StableGenericParameterQueryKey::from(tests::test_identity_detail::module(),
                                                   identity::GenericParameterKey::compute(record)),
      zc::mv(record),
      ActiveGenericParameterOwner::implementation(zc::mv(ZC_REQUIRE_NONNULL(repeatedAuthority))),
      1);
  ZC_EXPECT(wrongOrdinal == zc::none);
}

ZC_TEST("ActiveCallableParameterMembershipRecord seals callable ownership") {
  auto definition = definitionRecord();
  auto definitionKey = identity::DefinitionKey::compute(definition);
  auto owner = binder::StableDefinitionQueryKey::from(tests::test_identity_detail::module(),
                                                      definitionKey.clone());
  auto record = identity::CallableParameterIdentityRecord::from(
      zc::mv(definitionKey), identity::CallableParameterPosition::ordinary(0));
  auto query = binder::StableCallableParameterQueryKey::from(
      tests::test_identity_detail::module(), identity::CallableParameterKey::compute(record));
  zc::Maybe<identity::DeclaredDefinitionName> parameterName = declaredName("value"_zc);
  auto membership = ActiveCallableParameterMembershipRecord::from(
      zc::mv(query), record.clone(), owner.clone(), headerSite(2), record.position(),
      zc::mv(parameterName), true);
  ZC_REQUIRE(membership != zc::none);

  auto encoded = ZC_REQUIRE_NONNULL(membership).encodeCanonical();
  auto decoded = ActiveCallableParameterMembershipRecord::decodeCanonical(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded).encodeCanonical().asPtr() == encoded.asPtr());

  zc::Maybe<identity::DeclaredDefinitionName> noName;
  auto unnamedOrdinary = ActiveCallableParameterMembershipRecord::from(
      binder::StableCallableParameterQueryKey::from(
          tests::test_identity_detail::module(), identity::CallableParameterKey::compute(record)),
      zc::mv(record), zc::mv(owner), headerSite(2),
      identity::CallableParameterPosition::ordinary(0), zc::mv(noName), true);
  ZC_EXPECT(unnamedOrdinary == zc::none);
}

ZC_TEST("Active membership queries project and validate every authority domain") {
  auto roots = contextRoots();
  auto definition = definitionRecord();
  auto definitionKey = identity::DefinitionKey::compute(definition);
  auto definitionQuery = binder::StableDefinitionQueryKey::from(
      tests::test_identity_detail::module(), definitionKey.clone());
  auto definitionContext = ContextualDefinitionKey::from(roots.clone(), definitionQuery.clone());
  auto projectedDefinition = ActiveDefinitionMembershipQuery::projectGlobalKey(definitionContext);
  ZC_REQUIRE(projectedDefinition != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(projectedDefinition) == definitionKey);
  ZC_EXPECT(ActiveDefinitionMembershipQuery::sameAuthority(definition, definition.clone()));
  ZC_EXPECT(ActiveDefinitionMembershipQuery::validateAuthority(
      definitionContext, ZC_REQUIRE_NONNULL(projectedDefinition), definition));

  auto implementation = implementationFixture();
  zc::Vector<binder::StableImplementationOccurrenceQueryKey> occurrences;
  occurrences.add(implementation.occurrence.clone());
  auto implementationRecord = require(ActiveImplementationMembershipRecord::from(
      implementation.query.clone(), implementation.record.clone(),
      implementation.occurrence.clone(), zc::mv(occurrences)));
  auto implementationContext =
      ContextualImplementationKey::from(roots.clone(), implementation.query.clone());
  auto projectedImplementation =
      ActiveImplementationMembershipQuery::projectGlobalKey(implementationContext);
  ZC_REQUIRE(projectedImplementation != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(projectedImplementation) == implementation.query.implementation());
  ZC_EXPECT(ActiveImplementationMembershipQuery::sameAuthority(implementationRecord,
                                                               implementationRecord.clone()));
  ZC_EXPECT(ActiveImplementationMembershipQuery::validateAuthority(
      implementationContext, ZC_REQUIRE_NONNULL(projectedImplementation), implementationRecord));

  auto genericRecord = identity::GenericParameterIdentityRecord::type(
      identity::StableGenericParameterOwnerKey::definition(definitionKey.clone()), 0);
  auto genericQuery = binder::StableGenericParameterQueryKey::from(
      tests::test_identity_detail::module(), identity::GenericParameterKey::compute(genericRecord));
  auto genericMembership = require(ActiveGenericParameterMembership::from(
      genericQuery.clone(), genericRecord.clone(),
      ActiveGenericParameterOwner::definition(definitionQuery.clone(), headerSite(3)), 0));
  auto genericContext = ContextualGenericParameterKey::from(roots.clone(), genericQuery.clone());
  auto projectedGeneric = ActiveGenericParameterMembershipQuery::projectGlobalKey(genericContext);
  ZC_REQUIRE(projectedGeneric != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(projectedGeneric) == genericQuery.parameter());
  ZC_EXPECT(ActiveGenericParameterMembershipQuery::sameAuthority(genericMembership,
                                                                 genericMembership.clone()));
  ZC_EXPECT(ActiveGenericParameterMembershipQuery::validateAuthority(
      genericContext, ZC_REQUIRE_NONNULL(projectedGeneric), genericMembership));

  auto callableRecord = identity::CallableParameterIdentityRecord::from(
      definitionKey.clone(), identity::CallableParameterPosition::ordinary(0));
  auto callableQuery = binder::StableCallableParameterQueryKey::from(
      tests::test_identity_detail::module(),
      identity::CallableParameterKey::compute(callableRecord));
  zc::Maybe<identity::DeclaredDefinitionName> callableName = declaredName("value"_zc);
  auto callableMembership = require(ActiveCallableParameterMembershipRecord::from(
      callableQuery.clone(), callableRecord.clone(), definitionQuery.clone(), headerSite(4),
      callableRecord.position(), zc::mv(callableName), true));
  auto callableContext = ContextualCallableParameterKey::from(roots.clone(), callableQuery.clone());
  auto projectedCallable =
      ActiveCallableParameterMembershipQuery::projectGlobalKey(callableContext);
  ZC_REQUIRE(projectedCallable != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(projectedCallable) == callableQuery.parameter());
  ZC_EXPECT(ActiveCallableParameterMembershipQuery::sameAuthority(callableMembership,
                                                                  callableMembership.clone()));
  ZC_EXPECT(ActiveCallableParameterMembershipQuery::validateAuthority(
      callableContext, ZC_REQUIRE_NONNULL(projectedCallable), callableMembership));
}

}  // namespace zomlang::compiler::driver::incremental_binding_query
