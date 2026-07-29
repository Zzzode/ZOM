// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/identity/canonical-identity-interner-set.h"

#include "zc/core/thread.h"
#include "zc/ztest/test.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::identity {
namespace {

template <typename T>
T require(zc::Maybe<T>&& value) {
  return zc::mv(ZC_REQUIRE_NONNULL(value));
}

SemanticContextBrand context(SemanticContextFactory& factory) { return require(factory.issue()); }

DeclaredDefinitionName declaredName(zc::StringPtr text) {
  return tests::test_identity_detail::scalar<DeclaredDefinitionName>(text);
}

CanonicalNameReference name(zc::StringPtr text) {
  zc::Vector<SemanticIdentifier> suffix;
  suffix.add(tests::test_identity_detail::scalar<SemanticIdentifier>(text));
  return require(CanonicalNameReference::from(CanonicalNameRoot::relative(), zc::mv(suffix)));
}

CanonicalHeaderTypeSyntax namedType(zc::StringPtr text) {
  zc::Vector<CanonicalHeaderTypeSyntax> arguments;
  return CanonicalHeaderTypeSyntax::named(
      CanonicalNamedHeaderType::from(name(text), zc::mv(arguments)));
}

CanonicalImplHeader implHeader(zc::StringPtr traitName) {
  zc::Vector<CanonicalHeaderTypeSyntax> arguments;
  auto trait = require(CanonicalTraitReference::from(name(traitName), zc::mv(arguments)));
  zc::Vector<CanonicalGenericParameter> generics;
  zc::Vector<CanonicalBoundObligation> obligations;
  return require(CanonicalImplHeader::from(zc::mv(generics), ImplPolarity::Positive,
                                           ImplSafety::Safe, zc::mv(trait), namedType("T"_zc),
                                           zc::mv(obligations)));
}

DefinitionIdentityRecord definitionRecord(zc::StringPtr nameValue) {
  zc::Vector<EnclosingStableOwnerKey> owners;
  zc::Maybe<OverloadHeaderDigest> noOverload;
  return require(DefinitionIdentityRecord::from(
      tests::test_identity_detail::module(), zc::mv(owners), DefinitionKind::Class,
      DefinitionNamespace::Type, declaredName(nameValue), zc::mv(noOverload)));
}

ImplIdentityRecord implementationRecord(zc::StringPtr traitName = "Trait"_zc) {
  zc::Vector<EnclosingStableOwnerKey> owners;
  return ImplIdentityRecord::from(tests::test_identity_detail::module(), zc::mv(owners),
                                  implHeader(traitName));
}

template <typename Handle>
Handle interned(IdentityInternResult<Handle>&& result) {
  ZC_REQUIRE(result.template is<Handle>());
  return result.template get<Handle>();
}

}  // namespace

ZC_TEST("CanonicalIdentityInternerSet interns and reverses all eight domains") {
  SemanticContextFactory factory;
  const auto owner = context(factory);
  auto interners = require(CanonicalIdentityInternerSet::create(factory, owner));

  const auto unit =
      interned(interners.internCompilationUnit(owner, tests::test_identity_detail::userUnit()));
  const auto repeatedUnit =
      interned(interners.internCompilationUnit(owner, tests::test_identity_detail::userUnit()));
  const auto crate = interned(interners.internCrate(owner, tests::test_identity_detail::crate()));
  const auto source =
      interned(interners.internSourceFile(owner, tests::test_identity_detail::source()));
  const auto module =
      interned(interners.internModule(owner, tests::test_identity_detail::module()));

  auto definitionAuthority = definitionRecord("Owner"_zc);
  const auto definitionKey = DefinitionKey::compute(definitionAuthority);
  const auto definition =
      interned(interners.internDefinition(owner, definitionKey, definitionAuthority));

  auto implementationAuthority = implementationRecord();
  const auto implementationKey = ImplKey::compute(implementationAuthority);
  const auto implementation =
      interned(interners.internImplementation(owner, implementationKey, implementationAuthority));

  auto genericAuthority = GenericParameterIdentityRecord::type(
      StableGenericParameterOwnerKey::definition(definitionKey.clone()), 0);
  const auto genericKey = GenericParameterKey::compute(genericAuthority);
  const auto generic =
      interned(interners.internGenericParameter(owner, genericKey, genericAuthority));

  auto callableAuthority = CallableParameterIdentityRecord::from(
      definitionKey.clone(), CallableParameterPosition::ordinary(0));
  const auto callableKey = CallableParameterKey::compute(callableAuthority);
  const auto callable =
      interned(interners.internCallableParameter(owner, callableKey, callableAuthority));

  ZC_EXPECT(unit.belongsTo(owner));
  ZC_EXPECT(repeatedUnit == unit);
  ZC_EXPECT(crate.belongsTo(owner));
  ZC_EXPECT(source.belongsTo(owner));
  ZC_EXPECT(module.belongsTo(owner));
  ZC_EXPECT(definition.belongsTo(owner));
  ZC_EXPECT(implementation.belongsTo(owner));
  ZC_EXPECT(generic.belongsTo(owner));
  ZC_EXPECT(callable.belongsTo(owner));
  ZC_EXPECT(ZC_REQUIRE_NONNULL(interners.compilationUnit(unit)).handle() == unit);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(interners.crate(crate)).handle() == crate);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(interners.sourceFile(source)).handle() == source);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(interners.module(module)).handle() == module);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(interners.definition(definition)).record().encode().asPtr() ==
            definitionAuthority.encode().asPtr());
  ZC_EXPECT(
      ZC_REQUIRE_NONNULL(interners.implementation(implementation)).record().encode().asPtr() ==
      implementationAuthority.encode().asPtr());
  ZC_EXPECT(ZC_REQUIRE_NONNULL(interners.genericParameter(generic)).record().encode().asPtr() ==
            genericAuthority.encode().asPtr());
  ZC_EXPECT(ZC_REQUIRE_NONNULL(interners.callableParameter(callable)).record().encode().asPtr() ==
            callableAuthority.encode().asPtr());
}

ZC_TEST("CanonicalIdentityInternerSet coalesces concurrent equal admission") {
  SemanticContextFactory factory;
  const auto owner = context(factory);
  auto interners = require(CanonicalIdentityInternerSet::create(factory, owner));
  auto authority = definitionRecord("Concurrent"_zc);
  const auto key = DefinitionKey::compute(authority);
  DefId handles[8];
  {
    zc::Own<zc::Thread> workers[8];
    for (size_t index = 0; index < 8; ++index) {
      workers[index] = zc::heap<zc::Thread>([&, index]() {
        handles[index] = interned(interners.internDefinition(owner, key, authority));
      });
    }
  }
  ZC_REQUIRE(handles[0].isValid());
  for (size_t index = 1; index < 8; ++index) { ZC_EXPECT(handles[index] == handles[0]); }
}

ZC_TEST("CanonicalIdentityInternerSet rejects collisions malformed records and foreign brands") {
  SemanticContextFactory factory;
  const auto owner = context(factory);
  const auto foreign = context(factory);
  auto interners = require(CanonicalIdentityInternerSet::create(factory, owner));
  auto first = implementationRecord("Trait"_zc);
  auto different = implementationRecord("Other"_zc);
  const auto key = ImplKey::compute(first);

  ZC_REQUIRE(interners.internImplementation(owner, key, first).is<ImplId>());
  auto collision = interners.internImplementation(owner, key, different);
  ZC_REQUIRE(collision.is<IdentityInternerFailure>());
  ZC_EXPECT(collision.get<IdentityInternerFailure>() ==
            IdentityInternerFailure::CanonicalCollision);

  const auto differentKey = ImplKey::compute(different);
  auto malformed = interners.internImplementation(owner, differentKey, first);
  ZC_REQUIRE(malformed.is<IdentityInternerFailure>());
  ZC_EXPECT(malformed.get<IdentityInternerFailure>() == IdentityInternerFailure::MalformedRecord);

  auto foreignResult =
      interners.internCompilationUnit(foreign, tests::test_identity_detail::userUnit());
  ZC_REQUIRE(foreignResult.is<IdentityInternerFailure>());
  ZC_EXPECT(foreignResult.get<IdentityInternerFailure>() == IdentityInternerFailure::ForeignBrand);

  auto foreignInterners = require(CanonicalIdentityInternerSet::create(factory, foreign));
  const auto foreignHandle = interned(
      foreignInterners.internCompilationUnit(foreign, tests::test_identity_detail::userUnit()));
  ZC_EXPECT(interners.compilationUnit(foreignHandle) == zc::none);
}

ZC_TEST("CanonicalIdentityInternerSet rejects duplicate construction authority") {
  SemanticContextFactory factory;
  const auto owner = context(factory);
  auto interners = require(CanonicalIdentityInternerSet::create(factory, owner));
  ZC_EXPECT(interners.context() == owner);
  ZC_EXPECT(CanonicalIdentityInternerSet::create(factory, owner) == zc::none);
}

}  // namespace zomlang::compiler::identity
