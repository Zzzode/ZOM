// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/graph/materialized-module-skeleton.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/binder/stable/stable-binding-codec.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::binder {
namespace {

template <typename T>
T require(zc::Maybe<T>&& value) {
  return zc::mv(ZC_REQUIRE_NONNULL(value));
}

identity::SemanticContextBrand context(identity::SemanticContextFactory& factory) {
  return require(factory.issue());
}

identity::ModuleKey module() { return tests::test_identity_detail::module(); }

identity::ContextFingerprint fingerprint() {
  return identity::ContextFingerprint::fromCanonicalDigest(identity::Sha256Digest());
}

identity::DeclaredDefinitionName declaredName(zc::StringPtr text) {
  return tests::test_identity_detail::scalar<identity::DeclaredDefinitionName>(text);
}

identity::CanonicalNameReference name(zc::StringPtr text) {
  zc::Vector<identity::SemanticIdentifier> suffix;
  suffix.add(tests::test_identity_detail::scalar<identity::SemanticIdentifier>(text));
  return require(identity::CanonicalNameReference::from(identity::CanonicalNameRoot::relative(),
                                                        zc::mv(suffix)));
}

identity::CanonicalHeaderTypeSyntax namedType(zc::StringPtr text) {
  zc::Vector<identity::CanonicalHeaderTypeSyntax> arguments;
  return identity::CanonicalHeaderTypeSyntax::named(
      identity::CanonicalNamedHeaderType::from(name(text), zc::mv(arguments)));
}

identity::ImplIdentityRecord implementationRecord() {
  zc::Vector<identity::CanonicalHeaderTypeSyntax> arguments;
  auto trait =
      require(identity::CanonicalTraitReference::from(name("Trait"_zc), zc::mv(arguments)));
  zc::Vector<identity::CanonicalGenericParameter> generics;
  zc::Vector<identity::CanonicalBoundObligation> obligations;
  auto header = require(identity::ImplHeader::from(
      zc::mv(generics), identity::ImplPolarity::Positive, identity::ImplSafety::Safe, zc::mv(trait),
      namedType("T"_zc), zc::mv(obligations)));
  zc::Vector<identity::EnclosingStableOwnerKey> owners;
  return identity::ImplIdentityRecord::from(module(), zc::mv(owners), zc::mv(header));
}

IdentitySyntaxSiteKey headerSite(uint32_t component) {
  zc::Vector<uint32_t> path;
  if (component != 0) { path.add(component); }
  return require(
      IdentitySyntaxSiteKey::from(module(), tests::test_identity_detail::source(), zc::mv(path)));
}

template <typename T>
void sortCanonical(zc::Vector<T>& values) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    const auto currentBytes = StableBindingCodec<T>::encode(current);
    size_t insertion = index;
    while (insertion != 0) {
      const auto previousBytes = StableBindingCodec<T>::encode(values[insertion - 1]);
      if (previousBytes.asPtr() < currentBytes.asPtr()) { break; }
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
}

template <typename T>
CanonicalSequence<T> sequence(zc::Vector<T>&& values) {
  sortCanonical(values);
  return require(StableBindingSequenceBuilder<T>::from(zc::mv(values)));
}

template <typename T>
CanonicalSequence<T> singleton(T&& value) {
  zc::Vector<T> values;
  values.add(zc::mv(value));
  return sequence(zc::mv(values));
}

BoundModuleSkeleton skeleton(uint32_t implementationOccurrences) {
  auto moduleKey = module();
  zc::Vector<identity::EnclosingStableOwnerKey> owners;
  zc::Maybe<identity::OverloadHeaderDigest> noOverload;
  auto record = require(identity::DefinitionIdentityRecord::from(
      moduleKey.clone(), zc::mv(owners), identity::DefinitionKind::Class,
      identity::DefinitionNamespace::Type, declaredName("Owner"_zc), zc::mv(noOverload)));
  auto definition = identity::DefinitionKey::compute(record);
  auto definitionQuery = StableDefinitionQueryKey::from(moduleKey.clone(), definition.clone());
  zc::Maybe<MemberVisibility> noVisibility;
  auto declaration = require(StableDeclarationFact::from(
      definitionQuery.clone(), record.clone(), StableScopeOwnerKey::module(moduleKey.clone()),
      identity::DefinitionKind::Class, Namespace::Type, declaredName("Owner"_zc),
      DefinitionActivation::ModuleSkeleton, zc::mv(noVisibility)));

  auto implementationRecordValue = implementationRecord();
  auto implementation = identity::ImplKey::compute(implementationRecordValue);
  zc::Vector<StableImplementationOccurrenceFact> occurrences;
  for (uint32_t index = 0; index < implementationOccurrences; ++index) {
    auto occurrence = ImplSourceOccurrenceKey::from(implementation.clone(), headerSite(index + 1));
    auto occurrenceQuery = require(
        StableImplementationOccurrenceQueryKey::from(moduleKey.clone(), zc::mv(occurrence)));
    occurrences.add(require(StableImplementationOccurrenceFact::from(
        zc::mv(occurrenceQuery),
        StableImplementationQueryKey::from(moduleKey.clone(), implementation.clone()),
        implementationRecordValue.clone(), StableScopeOwnerKey::module(moduleKey.clone()))));
  }

  auto genericRecord = identity::GenericParameterIdentityRecord::type(
      identity::StableGenericParameterOwnerKey::definition(definition.clone()), 0);
  auto generic = require(StableGenericParameterDeclarationFact::from(
      StableGenericParameterQueryKey::from(moduleKey.clone(),
                                           identity::GenericParameterKey::compute(genericRecord)),
      genericRecord.clone(), StableHeaderSite::definition(headerSite(0)),
      StableScopeOwnerKey::module(moduleKey.clone()), declaredName("T"_zc)));
  auto callableRecord = identity::CallableParameterIdentityRecord::from(
      definition.clone(), identity::CallableParameterPosition::ordinary(0));
  zc::Maybe<identity::DeclaredDefinitionName> parameterName = declaredName("value"_zc);
  auto callable = require(StableCallableParameterDeclarationFact::from(
      StableCallableParameterQueryKey::from(
          moduleKey.clone(), identity::CallableParameterKey::compute(callableRecord)),
      callableRecord.clone(), StableHeaderSite::definition(headerSite(0)),
      StableScopeOwnerKey::module(moduleKey.clone()), zc::mv(parameterName)));

  zc::Maybe<StableScopeOwnerKey> noParent;
  auto scope = require(StableScopeFact::from(StableScopeOwnerKey::module(moduleKey.clone()),
                                             zc::mv(noParent), ScopeKind::Module));
  auto moduleBody = require(StableOwnerBodyQueryKey::from(
      moduleKey.clone(), StableBodyOwnerKey::module(moduleKey.clone())));
  auto definitionBody = require(StableOwnerBodyQueryKey::from(
      moduleKey.clone(), StableBodyOwnerKey::definition(definition.clone())));
  zc::Vector<StableOwnerBodyQueryKey> bodies;
  bodies.add(zc::mv(moduleBody));
  bodies.add(zc::mv(definitionBody));
  sortCanonical(bodies);
  auto bodyOwners =
      require(StableBindingSequenceBuilder<StableOwnerBodyQueryKey>::fromNonEmpty(zc::mv(bodies)));

  return require(BoundModuleSkeleton::from(
      zc::mv(moduleKey), singleton(zc::mv(scope)), CanonicalSequence<StableNodeScopeFact>::empty(),
      singleton(zc::mv(declaration)), sequence(zc::mv(occurrences)), singleton(zc::mv(generic)),
      singleton(zc::mv(callable)), CanonicalSequence<StableModuleAliasFact>::empty(),
      CanonicalSequence<StableImportFact>::empty(),
      CanonicalSequence<StableLocalExportFact>::empty(), zc::mv(bodyOwners),
      CanonicalSequence<StableFailedLookupFact>::empty()));
}

}  // namespace

ZC_TEST("MaterializedModuleSkeletonIdentities materializes all stable identity domains") {
  identity::SemanticContextFactory factory;
  const auto owner = context(factory);
  auto interners = require(identity::IdentityInternerSet::create(factory, owner));
  auto stable = skeleton(2);
  auto semanticFingerprint = fingerprint();

  auto materialized = require(MaterializedModuleSkeletonIdentities::from(
      owner, query::DatabaseRevision(7), semanticFingerprint, stable, interners));

  ZC_EXPECT(materialized.context() == owner);
  ZC_EXPECT(materialized.revision() == query::DatabaseRevision(7));
  ZC_EXPECT(materialized.fingerprint().digest() == semanticFingerprint.digest());
  ZC_EXPECT(materialized.module().belongsTo(owner));
  ZC_EXPECT(ZC_REQUIRE_NONNULL(interners.module(materialized.module())).key().encode().asPtr() ==
            stable.module().encode().asPtr());
  ZC_EXPECT(materialized.stableWitness() == stable);
  ZC_EXPECT(materialized.stableWitness().scopes().values().size() == 1);
  ZC_EXPECT(materialized.stableWitness().declarations().values().size() == 1);
  ZC_EXPECT(materialized.stableWitness().implementationOccurrences().values().size() == 2);
  ZC_EXPECT(materialized.stableWitness().genericParameterDeclarations().values().size() == 1);
  ZC_EXPECT(materialized.stableWitness().callableParameterDeclarations().values().size() == 1);
  ZC_EXPECT(materialized.stableWitness().bodyOwners().values().size() == 2);
  ZC_REQUIRE(materialized.definitions().size() == 1);
  ZC_REQUIRE(materialized.implementations().size() == 1);
  ZC_REQUIRE(materialized.genericParameters().size() == 1);
  ZC_REQUIRE(materialized.callableParameters().size() == 1);
  ZC_EXPECT(materialized.definitions()[0].key() ==
            stable.declarations().values()[0].queryKey().definition());
  ZC_EXPECT(materialized.implementations()[0].key() ==
            stable.implementationOccurrences().values()[0].authority().implementation());
  ZC_EXPECT(materialized.genericParameters()[0].key() ==
            stable.genericParameterDeclarations().values()[0].queryKey().parameter());
  ZC_EXPECT(materialized.callableParameters()[0].key() ==
            stable.callableParameterDeclarations().values()[0].queryKey().parameter());
  ZC_EXPECT(ZC_REQUIRE_NONNULL(interners.definition(materialized.definitions()[0].handle()))
                .record()
                .encode()
                .asPtr() == materialized.definitions()[0].record().encode().asPtr());
  ZC_EXPECT(ZC_REQUIRE_NONNULL(interners.implementation(materialized.implementations()[0].handle()))
                .record()
                .encode()
                .asPtr() == materialized.implementations()[0].record().encode().asPtr());
  ZC_EXPECT(
      ZC_REQUIRE_NONNULL(interners.genericParameter(materialized.genericParameters()[0].handle()))
          .record()
          .encode()
          .asPtr() == materialized.genericParameters()[0].record().encode().asPtr());
  ZC_EXPECT(
      ZC_REQUIRE_NONNULL(interners.callableParameter(materialized.callableParameters()[0].handle()))
          .record()
          .encode()
          .asPtr() == materialized.callableParameters()[0].record().encode().asPtr());

  auto cloned = materialized.clone();
  ZC_EXPECT(cloned.revision() == materialized.revision());
  ZC_EXPECT(cloned.fingerprint().digest() == materialized.fingerprint().digest());
  ZC_EXPECT(cloned.module() == materialized.module());
  ZC_EXPECT(cloned.stableWitness() == materialized.stableWitness());
}

ZC_TEST("MaterializedModuleSkeletonIdentities rejects a foreign interner context") {
  identity::SemanticContextFactory factory;
  const auto owner = context(factory);
  const auto foreign = context(factory);
  auto interners = require(identity::IdentityInternerSet::create(factory, owner));
  auto stable = skeleton(1);
  auto semanticFingerprint = fingerprint();

  ZC_EXPECT(MaterializedModuleSkeletonIdentities::from(foreign, query::DatabaseRevision(1),
                                                       semanticFingerprint, stable,
                                                       interners) == zc::none);
}

ZC_TEST("MaterializedModuleSkeletonIdentities rejects a zero revision") {
  identity::SemanticContextFactory factory;
  const auto owner = context(factory);
  auto interners = require(identity::IdentityInternerSet::create(factory, owner));
  auto stable = skeleton(1);

  ZC_EXPECT(MaterializedModuleSkeletonIdentities::from(
                owner, query::DatabaseRevision(), fingerprint(), stable, interners) == zc::none);
}

}  // namespace zomlang::compiler::binder
