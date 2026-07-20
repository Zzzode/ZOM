// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/active-definition-authority-query.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/basic/thread-pool.h"

namespace zomlang::compiler::driver::incremental_binding_query {
namespace {

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto value = Scalar::fromCanonical(text);
  return zc::mv(ZC_REQUIRE_NONNULL(value));
}

identity::ResolvedVersion version() { return scalar<identity::ResolvedVersion>("0.0.0"_zc); }

identity::SortedFeatureSet packageFeatures() {
  zc::Vector<identity::FeatureName> values;
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::SortedTargetFeatureSet targetFeatures() {
  zc::Vector<identity::TargetFeatureName> values;
  auto result = identity::SortedTargetFeatureSet::from(zc::mv(values));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::PackageKey package() {
  zc::Vector<identity::CanonicalPathSegment> path;
  return identity::PackageKey::from(
      identity::CanonicalPackageSource::localPath(
          identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(path))),
      scalar<identity::PackageName>("authority"_zc), version(), packageFeatures());
}

identity::CanonicalTargetSpecificationKey target() {
  auto result = identity::CanonicalTargetSpecificationKey::from(
      scalar<identity::TargetComponentName>("x86_64"_zc),
      scalar<identity::TargetComponentName>("zom"_zc),
      scalar<identity::TargetComponentName>("none"_zc),
      scalar<identity::TargetComponentName>("unknown"_zc),
      scalar<identity::TargetComponentName>("zom-v1"_zc), 64, identity::Endianness::Little,
      targetFeatures());
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::CompilationConfigKey compilation() {
  zc::Maybe<identity::BuildScriptProducerKey> noBuildScript;
  auto result = identity::CompilationConfigKey::from(
      identity::CompilationDomain::Target, target(),
      identity::SemanticCompilerOptionsKey::from(2026, true, false, false), zc::mv(noBuildScript));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::CrateKey crate() {
  auto result =
      identity::CrateKey::from(package(), identity::CrateTargetKind::Library,
                               scalar<identity::TargetName>("authority"_zc), compilation());
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::ModuleKey module() {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(scalar<identity::ModulePathSegment>("root"_zc));
  auto result = identity::ModuleKey::from(crate(), zc::mv(path));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::DefinitionIdentityRecord definitionRecord(zc::StringPtr name) {
  zc::Vector<identity::EnclosingStableOwnerKey> owners;
  zc::Maybe<identity::OverloadHeaderDigest> noOverload;
  auto result = identity::DefinitionIdentityRecord::from(
      module(), zc::mv(owners), identity::DefinitionKind::Class,
      identity::DefinitionNamespace::Type, scalar<identity::DeclaredDefinitionName>(name),
      zc::mv(noOverload));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

ActiveDefinitionAuthorityRecord authorityRecord(zc::StringPtr name) {
  auto record = definitionRecord(name);
  auto key = identity::DefinitionKey::compute(record);
  auto result = ActiveDefinitionAuthorityRecord::from(zc::mv(key), zc::mv(record));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

basic::ThreadPool& scheduler() {
  static basic::ThreadPool value(4);
  return value;
}

}  // namespace

ZC_TEST("Active definition authority inputs use strict low durability codecs") {
  auto record = definitionRecord("Alpha"_zc);
  auto key = identity::DefinitionKey::compute(record);
  auto encodedKey = ActiveDefinitionAuthorityInput::encodeKey(key);
  auto decodedKey = ActiveDefinitionAuthorityInput::decodeKey(encodedKey.asPtr());
  ZC_REQUIRE(decodedKey != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedKey) == key);
  ZC_EXPECT(ActiveDefinitionAuthorityInput::decodeKey(encodedKey.asPtr().first(31)) == zc::none);
  auto oversizedKey = zc::heapArray<uint8_t>(encodedKey.size() + 1);
  oversizedKey.first(encodedKey.size()).copyFrom(encodedKey.asPtr());
  oversizedKey.back() = 0;
  ZC_EXPECT(ActiveDefinitionAuthorityInput::decodeKey(oversizedKey.asPtr()) == zc::none);

  auto encodedRecord = ActiveDefinitionAuthorityInput::encodeValue(record);
  auto decodedRecord = ActiveDefinitionAuthorityInput::decodeValue(encodedRecord.asPtr());
  ZC_REQUIRE(decodedRecord != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedRecord).encode().asPtr() == encodedRecord.asPtr());
  auto trailing = zc::heapArray<uint8_t>(encodedRecord.size() + 1);
  trailing.first(encodedRecord.size()).copyFrom(encodedRecord.asPtr());
  trailing.back() = 0;
  ZC_EXPECT(ActiveDefinitionAuthorityInput::decodeValue(trailing.asPtr()) == zc::none);
  auto oversizedRecord = zc::heapArray<uint8_t>((4u * 1024u * 1024u) + 1u);
  oversizedRecord.asPtr().fill(0);
  ZC_EXPECT(ActiveDefinitionAuthorityInput::decodeValue(oversizedRecord.asPtr()) == zc::none);

  auto mismatchedRecord = definitionRecord("Beta"_zc);
  auto mismatchedKey = identity::DefinitionKey::compute(record);
  ZC_EXPECT(ActiveDefinitionAuthorityRecord::from(zc::mv(mismatchedKey),
                                                  zc::mv(mismatchedRecord)) == zc::none);

  auto authorityContract = ActiveDefinitionAuthorityInput::contract();
  auto readinessContract = ActiveDefinitionAuthorityReadyInput::contract();
  ZC_EXPECT(authorityContract.isInput());
  ZC_EXPECT(readinessContract.isInput());
  ZC_EXPECT(authorityContract.inputDurability() == query::Durability::Low);
  ZC_EXPECT(readinessContract.inputDurability() == query::Durability::Low);
  ZC_EXPECT(ActiveDefinitionAuthorityInput::domain() ==
            "zom.query.active-definition-authority.v1"_zc);
  ZC_EXPECT(ActiveDefinitionAuthorityReadyInput::domain() ==
            "zom.query.active-definition-authority-ready.v1"_zc);
}

ZC_TEST("Active definition authority projection is canonical and fingerprinted") {
  zc::Vector<ActiveDefinitionAuthorityRecord> forward;
  forward.add(authorityRecord("Alpha"_zc));
  forward.add(authorityRecord("Beta"_zc));
  auto first = ActiveDefinitionAuthorityProjection::from(zc::mv(forward));
  ZC_REQUIRE(first != zc::none);

  zc::Vector<ActiveDefinitionAuthorityRecord> reverse;
  reverse.add(authorityRecord("Beta"_zc));
  reverse.add(authorityRecord("Alpha"_zc));
  reverse.add(authorityRecord("Alpha"_zc));
  auto second = ActiveDefinitionAuthorityProjection::from(zc::mv(reverse));
  ZC_REQUIRE(second != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(first).records().size() == 2);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(second).records().size() == 2);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(first).fingerprint() == ZC_REQUIRE_NONNULL(second).fingerprint());
  ZC_EXPECT(zc::encodeHex(ZC_REQUIRE_NONNULL(first).fingerprint().bytes()) ==
            "947428334e1596fcc6203d5c703f5aa51a95de691384548bc812e648120bb115"_zc);

  zc::Vector<ActiveDefinitionAuthorityRecord> changedRecords;
  changedRecords.add(authorityRecord("Alpha"_zc));
  changedRecords.add(authorityRecord("Gamma"_zc));
  auto changed = ActiveDefinitionAuthorityProjection::from(zc::mv(changedRecords));
  ZC_REQUIRE(changed != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(first).fingerprint() != ZC_REQUIRE_NONNULL(changed).fingerprint());

  auto readinessKey = identity::source_query::CompilationUnitQueryKey::fixed();
  auto encodedReadinessKey = ActiveDefinitionAuthorityReadyInput::encodeKey(readinessKey);
  ZC_EXPECT(ActiveDefinitionAuthorityReadyInput::decodeKey(encodedReadinessKey.asPtr()) !=
            zc::none);
  ZC_EXPECT(ActiveDefinitionAuthorityReadyInput::decodeKey(encodedReadinessKey.asPtr().first(0)) ==
            zc::none);
  auto malformedReadinessKey = zc::heapArray<uint8_t>(1);
  malformedReadinessKey[0] = 0x02;
  ZC_EXPECT(ActiveDefinitionAuthorityReadyInput::decodeKey(malformedReadinessKey.asPtr()) ==
            zc::none);
  auto oversizedReadinessKey = zc::heapArray<uint8_t>(2);
  oversizedReadinessKey.asPtr().fill(0x01);
  ZC_EXPECT(ActiveDefinitionAuthorityReadyInput::decodeKey(oversizedReadinessKey.asPtr()) ==
            zc::none);
  auto encodedFingerprint =
      ActiveDefinitionAuthorityReadyInput::encodeValue(ZC_REQUIRE_NONNULL(first).fingerprint());
  auto decodedFingerprint =
      ActiveDefinitionAuthorityReadyInput::decodeValue(encodedFingerprint.asPtr());
  ZC_REQUIRE(decodedFingerprint != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedFingerprint) == ZC_REQUIRE_NONNULL(first).fingerprint());
  ZC_EXPECT(ActiveDefinitionAuthorityReadyInput::decodeValue(
                encodedFingerprint.asPtr().first(31)) == zc::none);
  auto oversizedFingerprint = zc::heapArray<uint8_t>(encodedFingerprint.size() + 1);
  oversizedFingerprint.first(encodedFingerprint.size()).copyFrom(encodedFingerprint.asPtr());
  oversizedFingerprint.back() = 0;
  ZC_EXPECT(ActiveDefinitionAuthorityReadyInput::decodeValue(oversizedFingerprint.asPtr()) ==
            zc::none);
}

ZC_TEST("Active definition authority inputs round trip through QueryDatabase") {
  query::QueryDatabase database(scheduler());
  ZC_REQUIRE(registerActiveDefinitionAuthorityInputs(database));
  auto projectionRecords = zc::Vector<ActiveDefinitionAuthorityRecord>();
  projectionRecords.add(authorityRecord("Alpha"_zc));
  auto projection = ActiveDefinitionAuthorityProjection::from(zc::mv(projectionRecords));
  ZC_REQUIRE(projection != zc::none);
  const auto& authority = ZC_REQUIRE_NONNULL(projection).records()[0];

  auto transaction = database.beginInputTransaction();
  ZC_REQUIRE(transaction != zc::none);
  ZC_REQUIRE(ZC_REQUIRE_NONNULL(transaction)
                 .set<ActiveDefinitionAuthorityInput>(authority.key(), authority.record()));
  ZC_REQUIRE(ZC_REQUIRE_NONNULL(transaction)
                 .set<ActiveDefinitionAuthorityReadyInput>(
                     identity::source_query::CompilationUnitQueryKey::fixed(),
                     ZC_REQUIRE_NONNULL(projection).fingerprint()));
  ZC_REQUIRE(ZC_REQUIRE_NONNULL(transaction).commit() != zc::none);

  auto snapshot = database.snapshot();
  auto retained = snapshot.get<ActiveDefinitionAuthorityInput>(authority.key());
  ZC_REQUIRE(!retained.isRuntimeFailure());
  ZC_EXPECT(retained.value().encode().asPtr() == authority.record().encode().asPtr());
  auto readiness = snapshot.get<ActiveDefinitionAuthorityReadyInput>(
      identity::source_query::CompilationUnitQueryKey::fixed());
  ZC_REQUIRE(!readiness.isRuntimeFailure());
  ZC_EXPECT(readiness.value() == ZC_REQUIRE_NONNULL(projection).fingerprint());
  auto metadata = snapshot.metadata<ActiveDefinitionAuthorityInput>(authority.key());
  ZC_REQUIRE(metadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(metadata).minimumDurability() == query::Durability::Low);
}

}  // namespace zomlang::compiler::driver::incremental_binding_query
