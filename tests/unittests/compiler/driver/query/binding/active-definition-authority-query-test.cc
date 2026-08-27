// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/driver/query/binding/active-definition-authority-query.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "compiler/basic/thread-pool.h"
#include "compiler/driver/query/binding/contextual-binding-key.h"
#include "compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::driver::incremental_binding_query {
namespace {

template <typename T>
constexpr bool isMoveOnly() {
  return __is_constructible(T, T&&) && !__is_constructible(T, const T&);
}

static_assert(isMoveOnly<ContextualBodyOwnerKey>());
static_assert(isMoveOnly<ContextualCompilationUnitKey>());
static_assert(isMoveOnly<ContextualCrateKey>());
static_assert(isMoveOnly<ContextualSourceKey>());
static_assert(isMoveOnly<ContextualModuleKey>());
static_assert(isMoveOnly<ContextualDefinitionKey>());
static_assert(isMoveOnly<ContextualImplementationKey>());
static_assert(isMoveOnly<ContextualGenericParameterKey>());
static_assert(isMoveOnly<ContextualCallableParameterKey>());

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto value = Scalar::fromCanonical(text);
  return zc::mv(ZC_REQUIRE_NONNULL(value));
}

template <typename T>
T require(zc::Maybe<T>&& value) {
  return zc::mv(ZC_REQUIRE_NONNULL(value));
}

template <typename T>
T digestKey(uint8_t byte) {
  uint8_t bytes[32];
  for (auto& value : bytes) { value = byte; }
  return require(T::fromBytes(zc::arrayPtr(bytes)));
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

identity::PackageKey package(zc::StringPtr name = "authority"_zc) {
  zc::Vector<identity::CanonicalPathSegment> path;
  return identity::PackageKey::from(
      identity::CanonicalPackageSource::localPath(
          identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(path))),
      scalar<identity::PackageName>(name), version(), packageFeatures());
}

identity::CanonicalTargetSpecificationKey target() {
  auto result = identity::CanonicalTargetSpecificationKey::from(
      scalar<identity::TargetComponentName>("x86_64"_zc),
      scalar<identity::TargetComponentName>("zom"_zc),
      scalar<identity::TargetComponentName>("none"_zc),
      scalar<identity::TargetComponentName>("unknown"_zc),
      scalar<identity::TargetComponentName>("zom"_zc), 64, identity::Endianness::Little,
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

identity::CrateKey crate(zc::StringPtr packageName = "authority"_zc) {
  auto result =
      identity::CrateKey::from(identity::CompilationUnitIdentity::userPackage(package(packageName)),
                               identity::CrateTargetKind::Library,
                               scalar<identity::TargetName>("authority"_zc), compilation());
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::ModuleKey module(zc::StringPtr packageName = "authority"_zc) {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(scalar<identity::ModulePathSegment>("root"_zc));
  auto result = identity::ModuleKey::from(crate(packageName), zc::mv(path));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::SourceFileKey source() {
  zc::Vector<identity::CanonicalPathSegment> path;
  path.add(scalar<identity::CanonicalPathSegment>("root.zom"_zc));
  return identity::SourceFileKey::from(
      crate(), identity::SourceOriginKey::localFile(
                   identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(path))));
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

CompilationRootSetQueryKey contextRoots(zc::StringPtr packageName = "authority"_zc) {
  zc::Vector<CompilationRootKey> roots;
  auto root = CompilationRootKey::userPackage(package(packageName));
  roots.add(zc::mv(ZC_REQUIRE_NONNULL(root)));
  auto result = CompilationRootSetQueryKey::from(zc::mv(roots));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

ContextualDefinitionKey contextualDefinition(const identity::DefinitionKey& definition) {
  return ContextualDefinitionKey::from(
      contextRoots(), binder::StableDefinitionQueryKey::from(module(), definition.clone()));
}

zc::Array<uint8_t> expectedContextualWire(zc::StringPtr domain,
                                          const CompilationRootSetQueryKey& roots,
                                          zc::ArrayPtr<const uint8_t> payload) {
  identity::CanonicalEncoder record;
  const auto rootBytes = roots.encodeCanonical();
  record.encodeByteString(rootBytes.asPtr());
  record.encodeByteString(payload);
  const auto fields = record.finish();
  zc::Vector<uint8_t> expected(domain.size() + 1 + fields.size());
  expected.addAll(domain.asBytes());
  expected.add(0x00);
  expected.addAll(fields.asPtr());
  return expected.releaseAsArray();
}

zc::Array<uint8_t> reorderedContextualWire(zc::StringPtr domain,
                                           const CompilationRootSetQueryKey& roots,
                                           zc::ArrayPtr<const uint8_t> payload) {
  identity::CanonicalEncoder record;
  const auto rootBytes = roots.encodeCanonical();
  record.encodeByteString(payload);
  record.encodeByteString(rootBytes.asPtr());
  const auto fields = record.finish();
  zc::Vector<uint8_t> reordered(domain.size() + 1 + fields.size());
  reordered.addAll(domain.asBytes());
  reordered.add(0x00);
  reordered.addAll(fields.asPtr());
  return reordered.releaseAsArray();
}

zc::Array<uint8_t> oversizedContextualComponent(zc::StringPtr domain) {
  identity::CanonicalEncoder record;
  record.encodeUint64((64u * 1024u * 1024u) + 1u);
  const auto fields = record.finish();
  zc::Vector<uint8_t> oversized(domain.size() + 1 + fields.size());
  oversized.addAll(domain.asBytes());
  oversized.add(0x00);
  oversized.addAll(fields.asPtr());
  return oversized.releaseAsArray();
}

template <typename T>
void expectContextualWire(const T& value, zc::StringPtr domain,
                          zc::ArrayPtr<const uint8_t> payload) {
  const auto encoded = value.encodeCanonical();
  ZC_EXPECT(encoded.asPtr() ==
            expectedContextualWire(domain, value.contextRoots(), payload).asPtr());
  auto decoded = T::decodeCanonical(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded) == value);
  ZC_EXPECT(
      T::decodeCanonical(reorderedContextualWire(domain, value.contextRoots(), payload).asPtr()) ==
      zc::none);
  ZC_EXPECT(T::decodeCanonical(oversizedContextualComponent(domain).asPtr()) == zc::none);
  ZC_EXPECT(T::decodeCanonical(encoded.asPtr().first(encoded.size() - 1)) == zc::none);
  auto wrongDomain = zc::heapArray<uint8_t>(encoded.asPtr());
  wrongDomain[0] ^= 0x01;
  ZC_EXPECT(T::decodeCanonical(wrongDomain.asPtr()) == zc::none);
  auto trailing = zc::heapArray<uint8_t>(encoded.size() + 1);
  trailing.first(encoded.size()).copyFrom(encoded.asPtr());
  trailing.back() = 0;
  ZC_EXPECT(T::decodeCanonical(trailing.asPtr()) == zc::none);
}

}  // namespace

ZC_TEST("StableBindingQueryTest.ContextualKeysRejectRootAndOwnerDrift") {
  auto body = ContextualBodyOwnerKey::from(
      contextRoots(), require(binder::StableOwnerBodyQueryKey::from(
                          module(), binder::StableBodyOwnerKey::module(module()))));
  auto unit = ContextualCompilationUnitKey::from(
      contextRoots(), identity::CompilationUnitIdentity::userPackage(package()));
  auto crateKey = ContextualCrateKey::from(contextRoots(), crate());
  auto sourceKey = ContextualSourceKey::from(contextRoots(), source());
  auto moduleKey = ContextualModuleKey::from(contextRoots(), module());
  auto definition = ContextualDefinitionKey::from(
      contextRoots(),
      binder::StableDefinitionQueryKey::from(module(), digestKey<identity::DefinitionKey>(0x11)));
  auto implementation = ContextualImplementationKey::from(
      contextRoots(),
      binder::StableImplementationQueryKey::from(module(), digestKey<identity::ImplKey>(0x22)));
  auto generic = ContextualGenericParameterKey::from(
      contextRoots(), binder::StableGenericParameterQueryKey::from(
                          module(), digestKey<identity::GenericParameterKey>(0x33)));
  auto callable = ContextualCallableParameterKey::from(
      contextRoots(), binder::StableCallableParameterQueryKey::from(
                          module(), digestKey<identity::CallableParameterKey>(0x44)));

  expectContextualWire(body, "zom.binder.contextual-body-owner-key"_zc,
                       body.body().encodeCanonical().asPtr());
  expectContextualWire(unit, "zom.binder.contextual-compilation-unit-key"_zc,
                       unit.unit().encode().asPtr());
  expectContextualWire(crateKey, "zom.binder.contextual-crate-key"_zc,
                       crateKey.crate().encode().asPtr());
  expectContextualWire(sourceKey, "zom.binder.contextual-source-key"_zc,
                       sourceKey.source().encode().asPtr());
  expectContextualWire(moduleKey, "zom.binder.contextual-module-key"_zc,
                       moduleKey.module().encode().asPtr());
  expectContextualWire(definition, "zom.binder.contextual-definition-key"_zc,
                       definition.definition().encodeCanonical().asPtr());
  expectContextualWire(implementation, "zom.binder.contextual-implementation-key"_zc,
                       implementation.implementation().encodeCanonical().asPtr());
  expectContextualWire(generic, "zom.binder.contextual-generic-parameter-key"_zc,
                       generic.parameter().encodeCanonical().asPtr());
  expectContextualWire(callable, "zom.binder.contextual-callable-parameter-key"_zc,
                       callable.parameter().encodeCanonical().asPtr());

  auto contextDrift =
      ContextualModuleKey::from(contextRoots("alternate"_zc), module("authority"_zc));
  ZC_EXPECT(contextDrift != moduleKey);
  auto ownerDrift = ContextualDefinitionKey::from(
      contextRoots(), binder::StableDefinitionQueryKey::from(
                          module("alternate"_zc), digestKey<identity::DefinitionKey>(0x11)));
  ZC_EXPECT(ownerDrift != definition);
}

ZC_TEST("Active definition authority inputs use strict low durability codecs") {
  auto record = definitionRecord("Alpha"_zc);
  auto key = identity::DefinitionKey::compute(record);
  auto contextualKey = contextualDefinition(key);
  auto encodedKey = ActiveDefinitionAuthorityInput::encodeKey(contextualKey);
  auto decodedKey = ActiveDefinitionAuthorityInput::decodeKey(encodedKey.asPtr());
  ZC_REQUIRE(decodedKey != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedKey) == contextualKey);
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

  ZC_EXPECT(ActiveDefinitionAuthorityInput::descriptor.durability == query::Durability::Low);
  ZC_EXPECT(ActiveDefinitionAuthorityReadyInput::descriptor.durability == query::Durability::Low);
  ZC_EXPECT(ActiveDefinitionAuthorityInput::descriptor.domain ==
            "zom.query.active-definition-authority"_zc);
  ZC_EXPECT(ActiveDefinitionAuthorityReadyInput::descriptor.domain ==
            "zom.query.active-definition-authority-ready"_zc);
}

ZC_TEST("Active definition authority projection is canonical and fingerprinted") {
  zc::Vector<ActiveDefinitionAuthorityRecord> forward;
  forward.add(authorityRecord("Alpha"_zc));
  forward.add(authorityRecord("Beta"_zc));
  auto roots = contextRoots();
  auto first = ActiveDefinitionAuthorityProjection::from(roots, zc::mv(forward));
  ZC_REQUIRE(first != zc::none);

  zc::Vector<ActiveDefinitionAuthorityRecord> reverse;
  reverse.add(authorityRecord("Beta"_zc));
  reverse.add(authorityRecord("Alpha"_zc));
  reverse.add(authorityRecord("Alpha"_zc));
  auto second = ActiveDefinitionAuthorityProjection::from(roots, zc::mv(reverse));
  ZC_REQUIRE(second != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(first).records().size() == 2);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(second).records().size() == 2);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(first).fingerprint() == ZC_REQUIRE_NONNULL(second).fingerprint());
  ZC_EXPECT(zc::encodeHex(ZC_REQUIRE_NONNULL(first).fingerprint().bytes()) ==
            "13b5ff2fd2c8712b1102a1a08c88d8c179c5a7099475ecbe6dfaa2e3c1ad7456"_zc);

  zc::Vector<ActiveDefinitionAuthorityRecord> changedRecords;
  changedRecords.add(authorityRecord("Alpha"_zc));
  changedRecords.add(authorityRecord("Gamma"_zc));
  auto changed = ActiveDefinitionAuthorityProjection::from(roots, zc::mv(changedRecords));
  ZC_REQUIRE(changed != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(first).fingerprint() != ZC_REQUIRE_NONNULL(changed).fingerprint());

  auto readinessKey = contextRoots();
  auto encodedReadinessKey = ActiveDefinitionAuthorityReadyInput::encodeKey(readinessKey);
  ZC_EXPECT(ActiveDefinitionAuthorityReadyInput::decodeKey(encodedReadinessKey.asPtr()) !=
            zc::none);
  ZC_EXPECT(ActiveDefinitionAuthorityReadyInput::decodeKey(encodedReadinessKey.asPtr().first(0)) ==
            zc::none);
  auto malformedReadinessKey = zc::heapArray<uint8_t>(encodedReadinessKey.asPtr());
  malformedReadinessKey[0] ^= 0x01;
  ZC_EXPECT(ActiveDefinitionAuthorityReadyInput::decodeKey(malformedReadinessKey.asPtr()) ==
            zc::none);
  auto oversizedReadinessKey = zc::heapArray<uint8_t>(encodedReadinessKey.size() + 1);
  oversizedReadinessKey.first(encodedReadinessKey.size()).copyFrom(encodedReadinessKey.asPtr());
  oversizedReadinessKey.back() = 0;
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
  query::QueryDatabase database(scheduler(), query::productionQueryDescriptorInventory());
  ZC_REQUIRE(registerActiveDefinitionAuthorityInputs(database));
  auto projectionRecords = zc::Vector<ActiveDefinitionAuthorityRecord>();
  projectionRecords.add(authorityRecord("Alpha"_zc));
  auto roots = contextRoots();
  auto projection = ActiveDefinitionAuthorityProjection::from(roots, zc::mv(projectionRecords));
  ZC_REQUIRE(projection != zc::none);
  const auto& authority = ZC_REQUIRE_NONNULL(projection).records()[0];

  auto pending = database.beginInputTransaction(database.snapshot().revision());
  ZC_REQUIRE(pending.isOpened());
  auto transaction = zc::mv(pending).takeTransaction();
  auto authorityKey = contextualDefinition(authority.key());
  ZC_REQUIRE(transaction.set<ActiveDefinitionAuthorityInput>(authorityKey, authority.record())
                 .isApplied());
  ZC_REQUIRE(transaction
                 .set<ActiveDefinitionAuthorityReadyInput>(
                     roots, ZC_REQUIRE_NONNULL(projection).fingerprint())
                 .isApplied());
  ZC_REQUIRE(transaction.commit().isCommitted());

  auto snapshot = database.snapshot();
  auto retained = snapshot.get<ActiveDefinitionAuthorityInput>(authorityKey);
  ZC_REQUIRE(!retained.isRuntimeFailure());
  ZC_EXPECT(retained.value().encode().asPtr() == authority.record().encode().asPtr());
  auto readiness = snapshot.get<ActiveDefinitionAuthorityReadyInput>(roots);
  ZC_REQUIRE(!readiness.isRuntimeFailure());
  ZC_EXPECT(readiness.value() == ZC_REQUIRE_NONNULL(projection).fingerprint());
  auto metadata = snapshot.metadata<ActiveDefinitionAuthorityInput>(authorityKey);
  ZC_REQUIRE(metadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(metadata).minimumDurability() == query::Durability::Low);
}

}  // namespace zomlang::compiler::driver::incremental_binding_query
