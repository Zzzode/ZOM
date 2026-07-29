// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/named-identity-inventory-query.h"

#include "zc/ztest/test.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::driver::incremental_binding_query {
namespace {

template <typename T>
T require(zc::Maybe<T>&& value) {
  return zc::mv(ZC_REQUIRE_NONNULL(value));
}

StableModuleQueryKey moduleKey() {
  return require(StableModuleQueryKey::fromVerified(tests::test_identity_detail::module()));
}

zc::Array<uint8_t> withTrailingByte(zc::ArrayPtr<const uint8_t> bytes) {
  auto result = zc::heapArray<uint8_t>(bytes.size() + 1);
  result.first(bytes.size()).copyFrom(bytes);
  result.back() = 0;
  return result;
}

template <typename Descriptor>
void expectModuleKeyCodec() {
  auto key = moduleKey();
  auto encoded = Descriptor::encodeKey(key);
  auto decoded = Descriptor::decodeKey(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded) == key);
  ZC_EXPECT(Descriptor::decodeKey(withTrailingByte(encoded.asPtr()).asPtr()) == zc::none);
}

}  // namespace

ZC_TEST("NamedIdentityInventoryQueryTest.DescriptorsUseExactAdmissionAndKeyCodecs") {
  expectModuleKeyCodec<IdentitySyntaxSiteInventoryQuery>();
  expectModuleKeyCodec<StableIdentityAdmissionQuery>();
  expectModuleKeyCodec<NamedDefinitionInventoryQuery>();
  expectModuleKeyCodec<NamedImplementationInventoryQuery>();
  expectModuleKeyCodec<RevisionLocalDefinitionSitesQuery>();
  expectModuleKeyCodec<RevisionLocalImplementationSitesQuery>();
  expectModuleKeyCodec<ModuleBodySyntaxQuery>();
  expectModuleKeyCodec<ModuleBodyProvenanceQuery>();

  ZC_EXPECT(IdentitySyntaxSiteInventoryQuery::descriptor.admission ==
            query::CapabilityAdmission::AnySnapshot);
  ZC_EXPECT(StableIdentityAdmissionQuery::descriptor.admission ==
            query::CapabilityAdmission::AnySnapshot);
  ZC_EXPECT(RevisionLocalDefinitionSitesQuery::descriptor.admission ==
            query::CapabilityAdmission::FinalSealedSnapshot);
  ZC_EXPECT(RevisionLocalImplementationSitesQuery::descriptor.admission ==
            query::CapabilityAdmission::FinalSealedSnapshot);
  ZC_EXPECT(ModuleBodyProvenanceQuery::descriptor.admission ==
            query::CapabilityAdmission::FinalSealedSnapshot);
}

ZC_TEST("NamedIdentityInventoryQueryTest.PrivateAdmissionWitnessesAreStableAndOpaque") {
  zc::Vector<binder::IdentitySyntaxSiteInventoryEntry> siteEntries;
  auto inventory = require(binder::IdentitySyntaxSiteInventory::fromVerified(
      tests::test_identity_detail::module(), tests::test_identity_detail::source(),
      tests::test_identity_detail::digest(0x31), 0, zc::mv(siteEntries)));
  auto inventoryClone = inventory.clone();
  auto firstInventoryWitness =
      query::CapabilityCandidateContract<IdentitySyntaxSiteInventoryQuery>::encode(inventory);
  auto secondInventoryWitness =
      query::CapabilityCandidateContract<IdentitySyntaxSiteInventoryQuery>::encode(inventoryClone);
  ZC_EXPECT(firstInventoryWitness.bytes() == secondInventoryWitness.bytes());
  ZC_EXPECT(query::CapabilityCandidateContract<IdentitySyntaxSiteInventoryQuery>::decode(
                firstInventoryWitness.bytes()) == zc::none);

  zc::Vector<binder::StableIdentityAdmissionDefinition> definitions;
  zc::Vector<binder::StableIdentityAdmissionImplementation> implementations;
  auto admission = require(binder::StableIdentityAdmission::fromVerified(
      tests::test_identity_detail::module(), tests::test_identity_detail::source(),
      tests::test_identity_detail::digest(0x32), zc::mv(definitions), zc::mv(implementations)));
  auto admissionClone = admission.clone();
  auto firstAdmissionWitness =
      query::CapabilityCandidateContract<StableIdentityAdmissionQuery>::encode(admission);
  auto secondAdmissionWitness =
      query::CapabilityCandidateContract<StableIdentityAdmissionQuery>::encode(admissionClone);
  ZC_EXPECT(firstAdmissionWitness.bytes() == secondAdmissionWitness.bytes());
  ZC_EXPECT(query::CapabilityCandidateContract<StableIdentityAdmissionQuery>::decode(
                firstAdmissionWitness.bytes()) == zc::none);
}

ZC_TEST("NamedIdentityInventoryQueryTest.SemanticAndRevisionLocalCodecsRejectTrailingBytes") {
  zc::Vector<identity::DefinitionIdentityAuthority> definitionAuthorities;
  auto definitions = require(binder::NamedDefinitionInventory::fromVerified(
      tests::test_identity_detail::module(), definitionAuthorities.asPtr()));
  auto definitionBytes = NamedDefinitionInventoryQuery::encodeValue(definitions);
  auto decodedDefinitions = NamedDefinitionInventoryQuery::decodeValue(definitionBytes.asPtr());
  ZC_REQUIRE(decodedDefinitions != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedDefinitions).sameAs(definitions));
  ZC_EXPECT(NamedDefinitionInventoryQuery::decodeValue(
                withTrailingByte(definitionBytes.asPtr()).asPtr()) == zc::none);

  zc::Vector<identity::ImplIdentityAuthority> implementationAuthorities;
  auto implementations = require(binder::NamedImplementationInventory::fromVerified(
      tests::test_identity_detail::module(), implementationAuthorities.asPtr()));
  auto implementationBytes = NamedImplementationInventoryQuery::encodeValue(implementations);
  auto decodedImplementations =
      NamedImplementationInventoryQuery::decodeValue(implementationBytes.asPtr());
  ZC_REQUIRE(decodedImplementations != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedImplementations).sameAs(implementations));
  ZC_EXPECT(NamedImplementationInventoryQuery::decodeValue(
                withTrailingByte(implementationBytes.asPtr()).asPtr()) == zc::none);

  zc::Vector<binder::RevisionLocalDefinitionSite> definitionSites;
  auto revisionDefinitions = require(binder::RevisionLocalDefinitionSites::fromVerified(
      tests::test_identity_detail::module(), tests::test_identity_detail::source(), definitions,
      zc::mv(definitionSites)));
  auto definitionWitness =
      query::CapabilityCandidateContract<RevisionLocalDefinitionSitesQuery>::encode(
          revisionDefinitions);
  auto decodedRevisionDefinitions =
      query::CapabilityCandidateContract<RevisionLocalDefinitionSitesQuery>::decode(
          definitionWitness.bytes());
  ZC_REQUIRE(decodedRevisionDefinitions != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedRevisionDefinitions)->sameAs(revisionDefinitions));
  ZC_EXPECT(query::CapabilityCandidateContract<RevisionLocalDefinitionSitesQuery>::decode(
                withTrailingByte(definitionWitness.bytes()).asPtr()) == zc::none);

  zc::Vector<binder::RevisionLocalImplementationSite> implementationSites;
  auto revisionImplementations = require(binder::RevisionLocalImplementationSites::fromVerified(
      tests::test_identity_detail::module(), tests::test_identity_detail::source(), implementations,
      zc::mv(implementationSites)));
  auto implementationWitness =
      query::CapabilityCandidateContract<RevisionLocalImplementationSitesQuery>::encode(
          revisionImplementations);
  auto decodedRevisionImplementations =
      query::CapabilityCandidateContract<RevisionLocalImplementationSitesQuery>::decode(
          implementationWitness.bytes());
  ZC_REQUIRE(decodedRevisionImplementations != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedRevisionImplementations)->sameAs(revisionImplementations));
  ZC_EXPECT(query::CapabilityCandidateContract<RevisionLocalImplementationSitesQuery>::decode(
                withTrailingByte(implementationWitness.bytes()).asPtr()) == zc::none);
}

}  // namespace zomlang::compiler::driver::incremental_binding_query
