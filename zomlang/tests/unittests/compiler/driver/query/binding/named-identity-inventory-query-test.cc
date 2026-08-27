// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/query/binding/named-identity-inventory-query.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"
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

int compareBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] < right[index]) { return -1; }
    if (left[index] > right[index]) { return 1; }
  }
  if (left.size() < right.size()) { return -1; }
  if (left.size() > right.size()) { return 1; }
  return 0;
}

identity::ModuleKey semanticModule(zc::StringPtr name) {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(tests::test_identity_detail::scalar<identity::ModulePathSegment>(name));
  auto result = identity::ModuleKey::from(tests::test_identity_detail::crate(), zc::mv(path));
  return require(zc::mv(result));
}

identity::DefinitionIdentityAuthority definitionAuthority(identity::ModuleKey&& module,
                                                          zc::StringPtr name) {
  zc::Vector<identity::EnclosingStableOwnerKey> owners;
  zc::Maybe<identity::OverloadHeaderDigest> noOverload;
  auto record = identity::DefinitionIdentityRecord::from(
      zc::mv(module), zc::mv(owners), identity::DefinitionKind::Class,
      identity::DefinitionNamespace::Type,
      require(identity::DeclaredDefinitionName::fromCanonical(name)), zc::mv(noOverload));
  zc::Maybe<identity::OverloadHeaderAuthority> noOverloadAuthority;
  return require(identity::DefinitionIdentityAuthority::from(require(zc::mv(record)),
                                                             zc::mv(noOverloadAuthority)));
}

void encodeDefinitionEntry(identity::CanonicalEncoder& encoder, const identity::DefinitionKey& key,
                           const identity::DefinitionIdentityRecord& record,
                           binder::DefinitionBodyDisposition disposition) {
  key.encode(encoder);
  auto recordBytes = record.encode();
  encoder.encodeByteString(recordBytes.asPtr());
  encoder.encodeUint8(static_cast<uint8_t>(disposition));
}

zc::Array<uint8_t> singleDefinitionEntryBytes(const identity::DefinitionKey& key,
                                              const identity::DefinitionIdentityRecord& record,
                                              binder::DefinitionBodyDisposition disposition) {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString("zom.named-definition-inventory"_zcb);
  encoder.encodeSequenceSize(1);
  encodeDefinitionEntry(encoder, key, record, disposition);
  return encoder.finish();
}

zc::Array<uint8_t> twoDefinitionEntryBytes(const identity::DefinitionIdentityAuthority& first,
                                           binder::DefinitionBodyDisposition firstDisposition,
                                           const identity::DefinitionIdentityAuthority& second,
                                           binder::DefinitionBodyDisposition secondDisposition,
                                           bool sortEntries) {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString("zom.named-definition-inventory"_zcb);
  encoder.encodeSequenceSize(2);
  if (sortEntries && compareBytes(second.key().bytes(), first.key().bytes()) < 0) {
    encodeDefinitionEntry(encoder, second.key(), second.record(), secondDisposition);
    encodeDefinitionEntry(encoder, first.key(), first.record(), firstDisposition);
  } else {
    encodeDefinitionEntry(encoder, first.key(), first.record(), firstDisposition);
    encodeDefinitionEntry(encoder, second.key(), second.record(), secondDisposition);
  }
  return encoder.finish();
}

identity::CanonicalNameReference implementationName(zc::StringPtr name) {
  zc::Vector<identity::SemanticIdentifier> suffix;
  suffix.add(tests::test_identity_detail::scalar<identity::SemanticIdentifier>(name));
  auto result = identity::CanonicalNameReference::from(identity::CanonicalNameRoot::relative(),
                                                       zc::mv(suffix));
  return require(zc::mv(result));
}

identity::ImplHeader implementationHeader(zc::StringPtr traitName) {
  zc::Vector<identity::CanonicalHeaderTypeSyntax> arguments;
  auto trait =
      identity::CanonicalTraitReference::from(implementationName(traitName), zc::mv(arguments));
  auto selfType =
      identity::CanonicalHeaderTypeSyntax::predefined(identity::PredefinedTypeKind::I32);
  zc::Vector<identity::CanonicalGenericParameter> generics;
  zc::Vector<identity::CanonicalBoundObligation> obligations;
  auto result = identity::ImplHeader::from(
      zc::mv(generics), identity::ImplPolarity::Positive, identity::ImplSafety::Safe,
      require(zc::mv(trait)), require(zc::mv(selfType)), zc::mv(obligations));
  return require(zc::mv(result));
}

identity::ImplIdentityAuthority implementationAuthority(identity::ModuleKey&& module,
                                                        zc::StringPtr traitName) {
  zc::Vector<identity::EnclosingStableOwnerKey> owners;
  return identity::ImplIdentityAuthority::from(identity::ImplIdentityRecord::from(
      zc::mv(module), zc::mv(owners), implementationHeader(traitName)));
}

void encodeImplementationEntry(identity::CanonicalEncoder& encoder, const identity::ImplKey& key,
                               const identity::ImplIdentityRecord& record) {
  key.encode(encoder);
  auto recordBytes = record.encode();
  encoder.encodeByteString(recordBytes.asPtr());
}

zc::Array<uint8_t> singleImplementationEntryBytes(const identity::ImplKey& key,
                                                  const identity::ImplIdentityRecord& record) {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString("zom.named-implementation-inventory"_zcb);
  encoder.encodeSequenceSize(1);
  encodeImplementationEntry(encoder, key, record);
  return encoder.finish();
}

zc::Array<uint8_t> twoImplementationEntryBytes(const identity::ImplIdentityAuthority& first,
                                               const identity::ImplIdentityAuthority& second,
                                               bool sortEntries) {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString("zom.named-implementation-inventory"_zcb);
  encoder.encodeSequenceSize(2);
  if (sortEntries && compareBytes(second.key().bytes(), first.key().bytes()) < 0) {
    encodeImplementationEntry(encoder, second.key(), second.record());
    encodeImplementationEntry(encoder, first.key(), first.record());
  } else {
    encodeImplementationEntry(encoder, first.key(), first.record());
    encodeImplementationEntry(encoder, second.key(), second.record());
  }
  return encoder.finish();
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
  zc::Vector<binder::NamedDefinitionInventoryInput> definitionInputs;
  auto definitions = require(binder::NamedDefinitionInventory::fromVerified(
      tests::test_identity_detail::module(), definitionInputs.asPtr()));
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

ZC_TEST("NamedIdentityInventoryQueryTest.DefinitionInventoryRetainsCanonicalRecordsAndBodies") {
  auto first = definitionAuthority(semanticModule("test"_zc), "First"_zc);
  auto second = definitionAuthority(semanticModule("test"_zc), "Second"_zc);
  zc::Vector<binder::NamedDefinitionInventoryInput> inputs;
  if (compareBytes(first.key().bytes(), second.key().bytes()) < 0) {
    inputs.add(binder::NamedDefinitionInventoryInput{
        second.clone(), binder::DefinitionBodyDisposition::NoExecutableBody});
    inputs.add(binder::NamedDefinitionInventoryInput{
        first.clone(), binder::DefinitionBodyDisposition::ExecutableBody});
  } else {
    inputs.add(binder::NamedDefinitionInventoryInput{
        first.clone(), binder::DefinitionBodyDisposition::ExecutableBody});
    inputs.add(binder::NamedDefinitionInventoryInput{
        second.clone(), binder::DefinitionBodyDisposition::NoExecutableBody});
  }

  auto inventory = require(
      binder::NamedDefinitionInventory::fromVerified(semanticModule("test"_zc), inputs.asPtr()));
  ZC_REQUIRE(inventory.entries().size() == 2);
  ZC_EXPECT(
      compareBytes(inventory.entries()[0].key().bytes(), inventory.entries()[1].key().bytes()) < 0);
  for (const auto& entry : inventory.entries()) {
    ZC_EXPECT(identity::DefinitionKey::compute(entry.record()) == entry.key());
    if (entry.key() == first.key()) {
      ZC_EXPECT(entry.bodyDisposition() == binder::DefinitionBodyDisposition::ExecutableBody);
    } else {
      ZC_EXPECT(entry.key() == second.key());
      ZC_EXPECT(entry.bodyDisposition() == binder::DefinitionBodyDisposition::NoExecutableBody);
    }
  }

  zc::Vector<binder::NamedDefinitionInventoryInput> duplicates;
  duplicates.add(binder::NamedDefinitionInventoryInput{
      first.clone(), binder::DefinitionBodyDisposition::ExecutableBody});
  duplicates.add(binder::NamedDefinitionInventoryInput{
      first.clone(), binder::DefinitionBodyDisposition::ExecutableBody});
  auto deduplicated = require(binder::NamedDefinitionInventory::fromVerified(
      semanticModule("test"_zc), duplicates.asPtr()));
  ZC_EXPECT(deduplicated.entries().size() == 1);

  zc::Vector<binder::NamedDefinitionInventoryInput> contradictory;
  contradictory.add(binder::NamedDefinitionInventoryInput{
      first.clone(), binder::DefinitionBodyDisposition::ExecutableBody});
  contradictory.add(binder::NamedDefinitionInventoryInput{
      first.clone(), binder::DefinitionBodyDisposition::NoExecutableBody});
  ZC_EXPECT(binder::NamedDefinitionInventory::fromVerified(semanticModule("test"_zc),
                                                           contradictory.asPtr()) == zc::none);

  zc::Vector<binder::NamedDefinitionInventoryInput> wrongModule;
  wrongModule.add(binder::NamedDefinitionInventoryInput{
      definitionAuthority(semanticModule("other"_zc), "Other"_zc),
      binder::DefinitionBodyDisposition::NoExecutableBody});
  ZC_EXPECT(binder::NamedDefinitionInventory::fromVerified(semanticModule("test"_zc),
                                                           wrongModule.asPtr()) == zc::none);

  zc::Vector<binder::NamedDefinitionInventoryInput> unknownDisposition;
  unknownDisposition.add(binder::NamedDefinitionInventoryInput{
      first.clone(), static_cast<binder::DefinitionBodyDisposition>(0xff)});
  ZC_EXPECT(binder::NamedDefinitionInventory::fromVerified(semanticModule("test"_zc),
                                                           unknownDisposition.asPtr()) == zc::none);
}

ZC_TEST("NamedIdentityInventoryQueryTest.DefinitionInventoryRejectsInvalidWireRelations") {
  auto first = definitionAuthority(semanticModule("test"_zc), "First"_zc);
  auto second = definitionAuthority(semanticModule("test"_zc), "Second"_zc);
  constexpr auto executable = binder::DefinitionBodyDisposition::ExecutableBody;
  constexpr auto noExecutable = binder::DefinitionBodyDisposition::NoExecutableBody;

  auto canonical = twoDefinitionEntryBytes(first, executable, second, noExecutable, true);
  auto decoded = binder::NamedDefinitionInventory::decodeCanonical(canonical.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded).entries().size() == 2);

  auto nonIncreasing = twoDefinitionEntryBytes(first, executable, second, noExecutable, false);
  if (compareBytes(first.key().bytes(), second.key().bytes()) < 0) {
    nonIncreasing = twoDefinitionEntryBytes(second, noExecutable, first, executable, false);
  }
  ZC_EXPECT(binder::NamedDefinitionInventory::decodeCanonical(nonIncreasing.asPtr()) == zc::none);

  auto duplicate = twoDefinitionEntryBytes(first, executable, first, executable, false);
  ZC_EXPECT(binder::NamedDefinitionInventory::decodeCanonical(duplicate.asPtr()) == zc::none);

  auto mismatched = singleDefinitionEntryBytes(first.key(), second.record(), executable);
  ZC_EXPECT(binder::NamedDefinitionInventory::decodeCanonical(mismatched.asPtr()) == zc::none);

  auto unknownDisposition = singleDefinitionEntryBytes(
      first.key(), first.record(), static_cast<binder::DefinitionBodyDisposition>(0xff));
  ZC_EXPECT(binder::NamedDefinitionInventory::decodeCanonical(unknownDisposition.asPtr()) ==
            zc::none);

  auto otherModule = definitionAuthority(semanticModule("other"_zc), "Other"_zc);
  auto mixedModules = twoDefinitionEntryBytes(first, executable, otherModule, noExecutable, true);
  ZC_EXPECT(binder::NamedDefinitionInventory::decodeCanonical(mixedModules.asPtr()) == zc::none);
}

ZC_TEST("NamedIdentityInventoryQueryTest.ImplementationInventoryRetainsCanonicalRecords") {
  auto first = implementationAuthority(semanticModule("test"_zc), "First"_zc);
  auto second = implementationAuthority(semanticModule("test"_zc), "Second"_zc);
  zc::Vector<identity::ImplIdentityAuthority> authorities;
  if (compareBytes(first.key().bytes(), second.key().bytes()) < 0) {
    authorities.add(second.clone());
    authorities.add(first.clone());
  } else {
    authorities.add(first.clone());
    authorities.add(second.clone());
  }

  auto inventory = require(binder::NamedImplementationInventory::fromVerified(
      semanticModule("test"_zc), authorities.asPtr()));
  ZC_REQUIRE(inventory.entries().size() == 2);
  ZC_EXPECT(
      compareBytes(inventory.entries()[0].key().bytes(), inventory.entries()[1].key().bytes()) < 0);
  for (const auto& entry : inventory.entries()) {
    ZC_EXPECT(identity::ImplKey::compute(entry.record()) == entry.key());
  }

  zc::Vector<identity::ImplIdentityAuthority> duplicates;
  duplicates.add(first.clone());
  duplicates.add(first.clone());
  auto deduplicated = require(binder::NamedImplementationInventory::fromVerified(
      semanticModule("test"_zc), duplicates.asPtr()));
  ZC_EXPECT(deduplicated.entries().size() == 1);

  zc::Vector<identity::ImplIdentityAuthority> wrongModule;
  wrongModule.add(implementationAuthority(semanticModule("other"_zc), "Other"_zc));
  ZC_EXPECT(binder::NamedImplementationInventory::fromVerified(semanticModule("test"_zc),
                                                               wrongModule.asPtr()) == zc::none);
}

ZC_TEST("NamedIdentityInventoryQueryTest.ImplementationInventoryRejectsInvalidWireRelations") {
  auto first = implementationAuthority(semanticModule("test"_zc), "First"_zc);
  auto second = implementationAuthority(semanticModule("test"_zc), "Second"_zc);

  auto canonical = twoImplementationEntryBytes(first, second, true);
  auto decoded = binder::NamedImplementationInventory::decodeCanonical(canonical.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded).entries().size() == 2);

  auto nonIncreasing = twoImplementationEntryBytes(first, second, false);
  if (compareBytes(first.key().bytes(), second.key().bytes()) < 0) {
    nonIncreasing = twoImplementationEntryBytes(second, first, false);
  }
  ZC_EXPECT(binder::NamedImplementationInventory::decodeCanonical(nonIncreasing.asPtr()) ==
            zc::none);

  auto duplicate = twoImplementationEntryBytes(first, first, false);
  ZC_EXPECT(binder::NamedImplementationInventory::decodeCanonical(duplicate.asPtr()) == zc::none);

  auto mismatched = singleImplementationEntryBytes(first.key(), second.record());
  ZC_EXPECT(binder::NamedImplementationInventory::decodeCanonical(mismatched.asPtr()) == zc::none);

  auto otherModule = implementationAuthority(semanticModule("other"_zc), "Other"_zc);
  auto mixedModules = twoImplementationEntryBytes(first, otherModule, true);
  ZC_EXPECT(binder::NamedImplementationInventory::decodeCanonical(mixedModules.asPtr()) ==
            zc::none);
}

}  // namespace zomlang::compiler::driver::incremental_binding_query
