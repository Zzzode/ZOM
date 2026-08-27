// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/driver/query/binding/active-definition-authority-query.h"

#include "zc/core/debug.h"
#include "compiler/identity/canonical/canonical-decoder.h"
#include "compiler/identity/canonical/canonical-encoder.h"
#include "compiler/identity/crypto/sha256.h"

namespace zomlang::compiler::driver::incremental_binding_query {
namespace {

constexpr zc::StringPtr kAuthoritySetDomain = "zom.active-definition-authority-set"_zc;
constexpr zc::StringPtr kCompleteRootIdentityReadinessDomain =
    "zom.binder.complete-root-identity-readiness"_zc;
constexpr uint64_t kMaximumReadinessBytes = 128 * 1024 * 1024;

int compareBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] < right[index]) return -1;
    if (left[index] > right[index]) return 1;
  }
  if (left.size() < right.size()) return -1;
  if (left.size() > right.size()) return 1;
  return 0;
}

zc::Maybe<ActiveDefinitionAuthoritySetFingerprint> computeFingerprint(
    const CompilationRootSetQueryKey& contextRoots,
    zc::ArrayPtr<const ActiveDefinitionAuthorityRecord> records) {
  identity::Sha256Hasher hasher;
  const uint8_t separator = 0;
  identity::CanonicalEncoder sequenceHeader;
  const auto contextBytes = contextRoots.encodeCanonical();
  sequenceHeader.encodeByteString(contextBytes.asPtr());
  sequenceHeader.encodeSequenceSize(records.size());
  auto headerBytes = sequenceHeader.finish();
  if (!hasher.update(kAuthoritySetDomain.asBytes()) || !hasher.update(zc::arrayPtr(separator)) ||
      !hasher.update(headerBytes.asPtr())) {
    return zc::none;
  }
  for (const auto& record : records) {
    identity::CanonicalEncoder pair;
    record.key().encode(pair);
    auto recordBytes = record.record().encode();
    pair.encodeByteString(recordBytes.asPtr());
    auto pairBytes = pair.finish();
    if (!hasher.update(pairBytes.asPtr())) { return zc::none; }
  }
  auto digest = hasher.finish();
  if (digest == zc::none) { return zc::none; }
  return ActiveDefinitionAuthoritySetFingerprint::fromBytes(ZC_ASSERT_NONNULL(digest).bytes());
}

}  // namespace

ActiveDefinitionAuthorityRecord::ActiveDefinitionAuthorityRecord(
    identity::DefinitionKey&& key, identity::DefinitionIdentityRecord&& record) noexcept
    : keyField(zc::mv(key)), recordField(zc::mv(record)) {}

zc::Maybe<ActiveDefinitionAuthorityRecord> ActiveDefinitionAuthorityRecord::from(
    identity::DefinitionKey&& key, identity::DefinitionIdentityRecord&& record) {
  if (identity::DefinitionKey::compute(record) != key) { return zc::none; }
  return ActiveDefinitionAuthorityRecord(zc::mv(key), zc::mv(record));
}

ActiveDefinitionAuthorityRecord ActiveDefinitionAuthorityRecord::clone() const {
  return ActiveDefinitionAuthorityRecord(keyField.clone(), recordField.clone());
}

const identity::DefinitionKey& ActiveDefinitionAuthorityRecord::key() const noexcept {
  return keyField;
}

const identity::DefinitionIdentityRecord& ActiveDefinitionAuthorityRecord::record() const noexcept {
  return recordField;
}

bool ActiveDefinitionAuthorityRecord::sameAs(const ActiveDefinitionAuthorityRecord& other) const {
  return keyField == other.keyField &&
         recordField.encode().asPtr() == other.recordField.encode().asPtr();
}

ActiveDefinitionAuthoritySetFingerprint::ActiveDefinitionAuthoritySetFingerprint(
    const identity::Sha256Digest& digest) noexcept
    : digestField(digest) {}

zc::Maybe<ActiveDefinitionAuthoritySetFingerprint>
ActiveDefinitionAuthoritySetFingerprint::fromBytes(zc::ArrayPtr<const uint8_t> bytes) {
  auto digest = identity::Sha256Digest::fromBytes(bytes);
  if (digest == zc::none) { return zc::none; }
  return ActiveDefinitionAuthoritySetFingerprint(ZC_ASSERT_NONNULL(digest));
}

ActiveDefinitionAuthoritySetFingerprint ActiveDefinitionAuthoritySetFingerprint::clone()
    const noexcept {
  return ActiveDefinitionAuthoritySetFingerprint(digestField);
}

zc::ArrayPtr<const uint8_t> ActiveDefinitionAuthoritySetFingerprint::bytes() const {
  return digestField.bytes();
}

bool ActiveDefinitionAuthoritySetFingerprint::operator==(
    const ActiveDefinitionAuthoritySetFingerprint& other) const noexcept {
  return digestField == other.digestField;
}

ActiveDefinitionAuthorityProjection::ActiveDefinitionAuthorityProjection(
    zc::Vector<ActiveDefinitionAuthorityRecord>&& records,
    ActiveDefinitionAuthoritySetFingerprint&& fingerprint) noexcept
    : recordFields(zc::mv(records)), fingerprintField(zc::mv(fingerprint)) {}

zc::Maybe<ActiveDefinitionAuthorityProjection> ActiveDefinitionAuthorityProjection::from(
    const CompilationRootSetQueryKey& contextRoots,
    zc::Vector<ActiveDefinitionAuthorityRecord>&& records) {
  for (size_t index = 1; index < records.size(); ++index) {
    auto current = zc::mv(records[index]);
    size_t insertion = index;
    while (insertion != 0 &&
           compareBytes(current.key().bytes(), records[insertion - 1].key().bytes()) < 0) {
      records[insertion] = zc::mv(records[insertion - 1]);
      --insertion;
    }
    records[insertion] = zc::mv(current);
  }
  zc::Vector<ActiveDefinitionAuthorityRecord> unique(records.size());
  for (auto& record : records) {
    if (unique.size() != 0 && unique.back().key() == record.key()) {
      if (!unique.back().sameAs(record)) { return zc::none; }
      continue;
    }
    unique.add(zc::mv(record));
  }
  auto fingerprint = computeFingerprint(contextRoots, unique.asPtr());
  if (fingerprint == zc::none) { return zc::none; }
  return ActiveDefinitionAuthorityProjection(zc::mv(unique),
                                             zc::mv(ZC_ASSERT_NONNULL(fingerprint)));
}

ActiveDefinitionAuthorityProjection ActiveDefinitionAuthorityProjection::clone() const {
  zc::Vector<ActiveDefinitionAuthorityRecord> records(recordFields.size());
  for (const auto& record : recordFields) { records.add(record.clone()); }
  return ActiveDefinitionAuthorityProjection(zc::mv(records), fingerprintField.clone());
}

zc::ArrayPtr<const ActiveDefinitionAuthorityRecord> ActiveDefinitionAuthorityProjection::records()
    const {
  return recordFields.asPtr();
}

const ActiveDefinitionAuthoritySetFingerprint& ActiveDefinitionAuthorityProjection::fingerprint()
    const noexcept {
  return fingerprintField;
}

zc::Array<uint8_t> ActiveDefinitionAuthorityInput::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<ActiveDefinitionAuthorityInput::Key> ActiveDefinitionAuthorityInput::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ContextualDefinitionKey::decodeCanonical(bytes);
}

zc::Array<uint8_t> ActiveDefinitionAuthorityInput::encodeValue(const Value& value) {
  return value.encode();
}

zc::Maybe<ActiveDefinitionAuthorityInput::Value> ActiveDefinitionAuthorityInput::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return identity::DefinitionIdentityRecord::decodeCanonical(bytes);
}

bool ActiveDefinitionAuthorityInputVerifier::verify(
    const ContextualDefinitionKey& key, const identity::DefinitionIdentityRecord& record) {
  return record.module().encode().asPtr() == key.definition().module().encode().asPtr() &&
         identity::DefinitionKey::compute(record) == key.definition().definition();
}

CompleteRootIdentityReadiness::CompleteRootIdentityReadiness(
    CompilationRootSetQueryKey&& contextRoots,
    const identity::Sha256Digest& definitionAuthorityDigest,
    const identity::Sha256Digest& implementationAuthorityDigest,
    const identity::Sha256Digest& genericParameterAuthorityDigest,
    const identity::Sha256Digest& callableParameterAuthorityDigest) noexcept
    : contextRootsField(zc::mv(contextRoots)),
      definitionAuthorityDigestField(definitionAuthorityDigest),
      implementationAuthorityDigestField(implementationAuthorityDigest),
      genericParameterAuthorityDigestField(genericParameterAuthorityDigest),
      callableParameterAuthorityDigestField(callableParameterAuthorityDigest) {}

CompleteRootIdentityReadiness CompleteRootIdentityReadiness::from(
    CompilationRootSetQueryKey&& contextRoots,
    const identity::Sha256Digest& definitionAuthorityDigest,
    const identity::Sha256Digest& implementationAuthorityDigest,
    const identity::Sha256Digest& genericParameterAuthorityDigest,
    const identity::Sha256Digest& callableParameterAuthorityDigest) {
  return CompleteRootIdentityReadiness(
      zc::mv(contextRoots), definitionAuthorityDigest, implementationAuthorityDigest,
      genericParameterAuthorityDigest, callableParameterAuthorityDigest);
}

zc::Maybe<CompleteRootIdentityReadiness> CompleteRootIdentityReadiness::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() > kMaximumReadinessBytes ||
      bytes.size() <= kCompleteRootIdentityReadinessDomain.size() ||
      bytes.first(kCompleteRootIdentityReadinessDomain.size()) !=
          kCompleteRootIdentityReadinessDomain.asBytes() ||
      bytes[kCompleteRootIdentityReadinessDomain.size()] != 0) {
    return zc::none;
  }
  identity::CanonicalDecoder decoder(bytes.slice(kCompleteRootIdentityReadinessDomain.size() + 1));
  auto rootsBytes = decoder.decodeByteString(kMaximumReadinessBytes);
  auto definitionBytes = decoder.decodeByteString(sizeof(identity::Sha256Digest));
  auto implementationBytes = decoder.decodeByteString(sizeof(identity::Sha256Digest));
  auto genericBytes = decoder.decodeByteString(sizeof(identity::Sha256Digest));
  auto callableBytes = decoder.decodeByteString(sizeof(identity::Sha256Digest));
  if (rootsBytes == zc::none || definitionBytes == zc::none || implementationBytes == zc::none ||
      genericBytes == zc::none || callableBytes == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto roots = CompilationRootSetQueryKey::decodeCanonical(ZC_ASSERT_NONNULL(rootsBytes).asPtr());
  auto definition = identity::Sha256Digest::fromBytes(ZC_ASSERT_NONNULL(definitionBytes).asPtr());
  auto implementation =
      identity::Sha256Digest::fromBytes(ZC_ASSERT_NONNULL(implementationBytes).asPtr());
  auto generic = identity::Sha256Digest::fromBytes(ZC_ASSERT_NONNULL(genericBytes).asPtr());
  auto callable = identity::Sha256Digest::fromBytes(ZC_ASSERT_NONNULL(callableBytes).asPtr());
  if (roots == zc::none || definition == zc::none || implementation == zc::none ||
      generic == zc::none || callable == zc::none) {
    return zc::none;
  }
  auto result = from(zc::mv(ZC_ASSERT_NONNULL(roots)), ZC_ASSERT_NONNULL(definition),
                     ZC_ASSERT_NONNULL(implementation), ZC_ASSERT_NONNULL(generic),
                     ZC_ASSERT_NONNULL(callable));
  if (result.encodeCanonical().asPtr() != bytes) { return zc::none; }
  return result;
}

CompleteRootIdentityReadiness CompleteRootIdentityReadiness::clone() const {
  return from(contextRootsField.clone(), definitionAuthorityDigestField,
              implementationAuthorityDigestField, genericParameterAuthorityDigestField,
              callableParameterAuthorityDigestField);
}

const CompilationRootSetQueryKey& CompleteRootIdentityReadiness::contextRoots() const noexcept {
  return contextRootsField;
}

const identity::Sha256Digest& CompleteRootIdentityReadiness::definitionAuthorityDigest()
    const noexcept {
  return definitionAuthorityDigestField;
}

const identity::Sha256Digest& CompleteRootIdentityReadiness::implementationAuthorityDigest()
    const noexcept {
  return implementationAuthorityDigestField;
}

const identity::Sha256Digest& CompleteRootIdentityReadiness::genericParameterAuthorityDigest()
    const noexcept {
  return genericParameterAuthorityDigestField;
}

const identity::Sha256Digest& CompleteRootIdentityReadiness::callableParameterAuthorityDigest()
    const noexcept {
  return callableParameterAuthorityDigestField;
}

zc::Array<uint8_t> CompleteRootIdentityReadiness::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  auto rootsBytes = contextRootsField.encodeCanonical();
  encoder.encodeByteString(rootsBytes.asPtr());
  encoder.encodeByteString(definitionAuthorityDigestField.bytes());
  encoder.encodeByteString(implementationAuthorityDigestField.bytes());
  encoder.encodeByteString(genericParameterAuthorityDigestField.bytes());
  encoder.encodeByteString(callableParameterAuthorityDigestField.bytes());
  auto tail = encoder.finish();
  zc::Vector<uint8_t> result(kCompleteRootIdentityReadinessDomain.size() + 1 + tail.size());
  result.addAll(kCompleteRootIdentityReadinessDomain.asBytes());
  result.add(0);
  result.addAll(tail.asPtr());
  return result.releaseAsArray();
}

zc::Array<uint8_t> CompleteRootIdentityReadinessInput::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<CompleteRootIdentityReadinessInput::Key> CompleteRootIdentityReadinessInput::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return CompilationRootSetQueryKey::decodeCanonical(bytes);
}

zc::Array<uint8_t> CompleteRootIdentityReadinessInput::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<CompleteRootIdentityReadinessInput::Value>
CompleteRootIdentityReadinessInput::decodeValue(zc::ArrayPtr<const uint8_t> bytes) {
  return CompleteRootIdentityReadiness::decodeCanonical(bytes);
}

bool CompleteRootIdentityReadinessVerifier::verify(const CompilationRootSetQueryKey& key,
                                                   const CompleteRootIdentityReadiness& readiness) {
  return readiness.contextRoots() == key;
}

zc::Array<uint8_t> ActiveDefinitionAuthorityReadyInput::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<ActiveDefinitionAuthorityReadyInput::Key> ActiveDefinitionAuthorityReadyInput::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return CompilationRootSetQueryKey::decodeCanonical(bytes);
}

zc::Array<uint8_t> ActiveDefinitionAuthorityReadyInput::encodeValue(const Value& value) {
  return zc::heapArray<uint8_t>(value.bytes());
}

zc::Maybe<ActiveDefinitionAuthorityReadyInput::Value>
ActiveDefinitionAuthorityReadyInput::decodeValue(zc::ArrayPtr<const uint8_t> bytes) {
  return ActiveDefinitionAuthoritySetFingerprint::fromBytes(bytes);
}

bool registerActiveDefinitionAuthorityInputs(query::QueryDatabase& database) {
  auto authority = database.registerDescriptor<ActiveDefinitionAuthorityInput>();
  if (!authority.isRegistered()) { return false; }
  return database.registerDescriptor<ActiveDefinitionAuthorityReadyInput>().isRegistered();
}

}  // namespace zomlang::compiler::driver::incremental_binding_query
