// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/driver/query/binding/named-item-query.h"

#include "zc/ztest/test.h"
#include "tests/unittests/compiler/test-semantic-identities.h"

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

identity::DefinitionKey definitionKey() {
  zc::Vector<identity::EnclosingStableOwnerKey> owners;
  zc::Maybe<identity::OverloadHeaderDigest> noOverload;
  auto record = require(identity::DefinitionIdentityRecord::from(
      tests::test_identity_detail::module(), zc::mv(owners), identity::DefinitionKind::Class,
      identity::DefinitionNamespace::Type,
      tests::test_identity_detail::scalar<identity::DeclaredDefinitionName>("Item"_zc),
      zc::mv(noOverload)));
  return identity::DefinitionKey::compute(record);
}

ContextualDefinitionKey contextualDefinition() {
  auto definition = definitionKey();
  return ContextualDefinitionKey::from(
      contextRoots(), binder::StableDefinitionQueryKey::from(tests::test_identity_detail::module(),
                                                             zc::mv(definition)));
}

zc::Array<uint8_t> withTrailingByte(zc::ArrayPtr<const uint8_t> bytes) {
  auto result = zc::heapArray<uint8_t>(bytes.size() + 1);
  result.first(bytes.size()).copyFrom(bytes);
  result.back() = 0;
  return result;
}

template <typename Descriptor>
void expectKeyCodec() {
  auto key = contextualDefinition();
  auto encoded = Descriptor::encodeKey(key);
  auto decoded = Descriptor::decodeKey(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded) == key);
  ZC_EXPECT(Descriptor::decodeKey(withTrailingByte(encoded.asPtr()).asPtr()) == zc::none);
}

}  // namespace

ZC_TEST("NamedItemQueryTest.ContextualKeysAndAdmissionAreExact") {
  expectKeyCodec<NamedItemSyntaxQuery>();
  expectKeyCodec<NamedItemProvenanceQuery>();
  ZC_EXPECT(NamedItemSyntaxQuery::descriptor.retention == query::RetentionClass::Evictable);
  ZC_EXPECT(NamedItemProvenanceQuery::descriptor.admission ==
            query::CapabilityAdmission::FinalSealedSnapshot);
}

ZC_TEST("NamedItemQueryTest.SyntaxAndProvenanceCodecsRejectTrailingBytes") {
  zc::Vector<binder::DetachedModuleBodyNode> nodes;
  nodes.add(binder::DetachedModuleBodyNode::definitionBoundary(definitionKey()));
  auto syntax = require(binder::ModuleBodySyntax::from(1, zc::mv(nodes)));
  auto itemSyntax =
      require(binder::NamedItemSyntax::from(tests::test_identity_detail::module(), zc::mv(syntax)));
  auto syntaxBytes = NamedItemSyntaxQuery::encodeValue(itemSyntax);
  auto decodedSyntax = NamedItemSyntaxQuery::decodeValue(syntaxBytes.asPtr());
  ZC_REQUIRE(decodedSyntax != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedSyntax) == itemSyntax);
  ZC_EXPECT(NamedItemSyntaxQuery::decodeValue(withTrailingByte(syntaxBytes.asPtr()).asPtr()) ==
            zc::none);

  zc::Vector<binder::ModuleBodyProvenanceEntry> entries;
  zc::Vector<uint32_t> pathComponents;
  pathComponents.add(0);
  entries.add(binder::ModuleBodyProvenanceEntry{
      require(binder::LocalSyntaxPath::from(zc::mv(pathComponents))), ast::NodeId(1), 0, 1});
  auto provenance = require(
      binder::ModuleBodyProvenance::from(tests::test_identity_detail::source(), zc::mv(entries)));
  auto itemProvenance = require(binder::NamedItemProvenance::from(zc::mv(provenance)));
  auto witness =
      query::CapabilityCandidateContract<NamedItemProvenanceQuery>::encode(itemProvenance);
  auto decodedProvenance =
      query::CapabilityCandidateContract<NamedItemProvenanceQuery>::decode(witness.bytes());
  ZC_REQUIRE(decodedProvenance != zc::none);
  ZC_EXPECT(*ZC_REQUIRE_NONNULL(decodedProvenance) == itemProvenance);
  ZC_EXPECT(query::CapabilityCandidateContract<NamedItemProvenanceQuery>::decode(
                withTrailingByte(witness.bytes()).asPtr()) == zc::none);
}

ZC_TEST("NamedItemQueryTest.KeyFailureCodecPreservesDefinitionOwner") {
  zc::Maybe<binder::LocalSyntaxPath> noPath;
  auto key = contextualDefinition();
  auto failure = require(binder::BinderKeyFailure::from(
      binder::BinderKeyFailureKind::InactiveOwner,
      binder::BinderQueryOwner::definitionHeader(key.definition().clone()), zc::mv(noPath)));
  using Contract = query::CapabilityFailureContract<NamedItemProvenanceQuery,
                                                    query::KeyRejection<binder::BinderKeyFailure>>;
  auto encoded = Contract::encode(failure);
  auto decoded = Contract::decode(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded) == failure);
  ZC_EXPECT(Contract::decode(withTrailingByte(encoded.asPtr()).asPtr()) == zc::none);
}

}  // namespace zomlang::compiler::driver::incremental_binding_query
