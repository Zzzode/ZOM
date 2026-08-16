// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/query/binding/owner-body-query.h"

#include "zc/ztest/test.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::driver::incremental_binding_query {
namespace {

template <typename T>
T require(zc::Maybe<T>&& value) {
  return zc::mv(ZC_REQUIRE_NONNULL(value));
}

CompilationRootSetQueryKey contextRoots() {
  zc::Vector<CompilationRootKey> roots;
  roots.add(require(CompilationRootKey::userPackage(tests::test_identity_detail::package())));
  return require(CompilationRootSetQueryKey::from(zc::mv(roots)));
}

binder::StableOwnerBodyQueryKey stableModuleBody() {
  return require(binder::StableOwnerBodyQueryKey::from(
      tests::test_identity_detail::module(),
      binder::StableBodyOwnerKey::module(tests::test_identity_detail::module())));
}

ContextualModuleKey contextualModule() {
  return ContextualModuleKey::from(contextRoots(), tests::test_identity_detail::module());
}

ContextualBodyOwnerKey contextualBody() {
  return ContextualBodyOwnerKey::from(contextRoots(), stableModuleBody());
}

zc::Array<uint8_t> withTrailingByte(zc::ArrayPtr<const uint8_t> bytes) {
  auto result = zc::heapArray<uint8_t>(bytes.size() + 1);
  result.first(bytes.size()).copyFrom(bytes);
  result.back() = 0;
  return result;
}

template <typename Descriptor, typename Key>
void expectKeyCodec(Key&& key) {
  auto encoded = Descriptor::encodeKey(key);
  auto decoded = Descriptor::decodeKey(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded) == key);
  ZC_EXPECT(Descriptor::decodeKey(withTrailingByte(encoded.asPtr()).asPtr()) == zc::none);
}

}  // namespace

ZC_TEST("OwnerBodyQueryTest.ContextualKeysAndAdmissionAreExact") {
  expectKeyCodec<ModuleBodyOwnersQuery>(contextualModule());
  expectKeyCodec<OwnerBodySyntaxQuery>(contextualBody());
  expectKeyCodec<OwnerBodyProvenanceQuery>(contextualBody());
  expectKeyCodec<binder::ModuleBindingAllocationPlanQuery>(contextualModule());
  ZC_EXPECT(ModuleBodyOwnersQuery::descriptor.retention == query::RetentionClass::Retained);
  ZC_EXPECT(OwnerBodySyntaxQuery::descriptor.retention == query::RetentionClass::Evictable);
  ZC_EXPECT(OwnerBodyProvenanceQuery::descriptor.admission ==
            query::CapabilityAdmission::FinalSealedSnapshot);
  ZC_EXPECT(binder::ModuleBindingAllocationPlanQuery::descriptor.retention ==
            query::RetentionClass::Retained);
}

ZC_TEST("OwnerBodyQueryTest.ValuesAndWitnessesRejectTrailingBytes") {
  zc::Vector<binder::StableBodyOwnerKey> owners;
  owners.add(binder::StableBodyOwnerKey::module(tests::test_identity_detail::module()));
  auto ownerInventory = require(
      binder::ModuleBodyOwners::from(tests::test_identity_detail::module(), zc::mv(owners)));
  auto ownerBytes = ModuleBodyOwnersQuery::encodeValue(ownerInventory);
  auto decodedOwners = ModuleBodyOwnersQuery::decodeValue(ownerBytes.asPtr());
  ZC_REQUIRE(decodedOwners != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedOwners) == ownerInventory);
  ZC_EXPECT(ModuleBodyOwnersQuery::decodeValue(withTrailingByte(ownerBytes.asPtr()).asPtr()) ==
            zc::none);

  zc::Vector<binder::DetachedModuleBodyNode> nodes;
  auto syntax = require(binder::ModuleBodySyntax::from(0, zc::mv(nodes)));
  auto bodySyntax = require(binder::OwnerBodySyntax::from(
      binder::StableBodyOwnerKey::module(tests::test_identity_detail::module()),
      tests::test_identity_detail::module(), zc::mv(syntax)));
  auto syntaxBytes = OwnerBodySyntaxQuery::encodeValue(bodySyntax);
  auto decodedSyntax = OwnerBodySyntaxQuery::decodeValue(syntaxBytes.asPtr());
  ZC_REQUIRE(decodedSyntax != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedSyntax) == bodySyntax);
  ZC_EXPECT(OwnerBodySyntaxQuery::decodeValue(withTrailingByte(syntaxBytes.asPtr()).asPtr()) ==
            zc::none);

  zc::Vector<binder::ModuleBodyProvenanceEntry> entries;
  auto provenance = require(
      binder::ModuleBodyProvenance::from(tests::test_identity_detail::source(), zc::mv(entries)));
  auto bodyProvenance = require(binder::OwnerBodyProvenance::from(
      binder::StableBodyOwnerKey::module(tests::test_identity_detail::module()),
      zc::mv(provenance)));
  auto witness =
      query::CapabilityCandidateContract<OwnerBodyProvenanceQuery>::encode(bodyProvenance);
  auto decodedProvenance =
      query::CapabilityCandidateContract<OwnerBodyProvenanceQuery>::decode(witness.bytes());
  ZC_REQUIRE(decodedProvenance != zc::none);
  ZC_EXPECT(*ZC_REQUIRE_NONNULL(decodedProvenance) == bodyProvenance);
  ZC_EXPECT(query::CapabilityCandidateContract<OwnerBodyProvenanceQuery>::decode(
                withTrailingByte(witness.bytes()).asPtr()) == zc::none);
}

ZC_TEST("OwnerBodyQueryTest.KeyFailureCodecPreservesBodyOwner") {
  zc::Maybe<binder::LocalSyntaxPath> noPath;
  auto failure = require(binder::BinderKeyFailure::from(
      binder::BinderKeyFailureKind::DefinitionWithoutBody,
      binder::BinderQueryOwner::body(stableModuleBody()), zc::mv(noPath)));
  using Contract = query::CapabilityFailureContract<OwnerBodyProvenanceQuery,
                                                    query::KeyRejection<binder::BinderKeyFailure>>;
  auto encoded = Contract::encode(failure);
  auto decoded = Contract::decode(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded) == failure);
  ZC_EXPECT(Contract::decode(withTrailingByte(encoded.asPtr()).asPtr()) == zc::none);
}

}  // namespace zomlang::compiler::driver::incremental_binding_query
