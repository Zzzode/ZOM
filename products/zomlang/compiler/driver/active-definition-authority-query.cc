// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/active-definition-authority-query.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/sha256.h"

namespace zomlang::compiler::driver::incremental_binding_query {
namespace {

constexpr zc::StringPtr kAuthoritySetDomain = "zom.active-definition-authority-set"_zc;
constexpr uint64_t kMaximumContextRootBytes = 64 * 1024 * 1024;
constexpr uint64_t kMaximumModuleKeyBytes = 16 * 1024;

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

query::QueryKindContract inputContract(zc::StringPtr domain) {
  auto contract = query::QueryKindContract::input(domain, query::Durability::Low);
  return zc::mv(ZC_REQUIRE_NONNULL(contract));
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

ContextualDefinitionKey::ContextualDefinitionKey(CompilationRootSetQueryKey&& contextRoots,
                                                 identity::DefinitionKey&& definition) noexcept
    : contextRootsField(zc::mv(contextRoots)), definitionField(zc::mv(definition)) {}

ContextualDefinitionKey ContextualDefinitionKey::from(CompilationRootSetQueryKey&& contextRoots,
                                                      identity::DefinitionKey&& definition) {
  return ContextualDefinitionKey(zc::mv(contextRoots), zc::mv(definition));
}

zc::Maybe<ContextualDefinitionKey> ContextualDefinitionKey::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto rootsBytes = decoder.decodeByteString(kMaximumContextRootBytes);
  auto definitionBytes = decoder.decodeByteString(32);
  if (rootsBytes == zc::none || definitionBytes == zc::none || !decoder.finished() ||
      ZC_ASSERT_NONNULL(definitionBytes).size() != 32) {
    return zc::none;
  }
  auto roots = CompilationRootSetQueryKey::decodeCanonical(ZC_ASSERT_NONNULL(rootsBytes).asPtr());
  auto definition = identity::DefinitionKey::fromBytes(ZC_ASSERT_NONNULL(definitionBytes).asPtr());
  if (roots == zc::none || definition == zc::none) { return zc::none; }
  return ContextualDefinitionKey(zc::mv(ZC_ASSERT_NONNULL(roots)),
                                 zc::mv(ZC_ASSERT_NONNULL(definition)));
}

ContextualDefinitionKey ContextualDefinitionKey::clone() const {
  return ContextualDefinitionKey(contextRootsField.clone(), definitionField.clone());
}

const CompilationRootSetQueryKey& ContextualDefinitionKey::contextRoots() const noexcept {
  return contextRootsField;
}

const identity::DefinitionKey& ContextualDefinitionKey::definition() const noexcept {
  return definitionField;
}

zc::Array<uint8_t> ContextualDefinitionKey::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  const auto roots = contextRootsField.encodeCanonical();
  encoder.encodeByteString(roots.asPtr());
  encoder.encodeByteString(definitionField.bytes());
  return encoder.finish();
}

bool ContextualDefinitionKey::operator==(const ContextualDefinitionKey& other) const noexcept {
  return contextRootsField == other.contextRootsField && definitionField == other.definitionField;
}

ContextualModuleKey::ContextualModuleKey(CompilationRootSetQueryKey&& contextRoots,
                                         identity::ModuleKey&& module) noexcept
    : contextRootsField(zc::mv(contextRoots)), moduleField(zc::mv(module)) {}

ContextualModuleKey ContextualModuleKey::from(CompilationRootSetQueryKey&& contextRoots,
                                              identity::ModuleKey&& module) {
  return ContextualModuleKey(zc::mv(contextRoots), zc::mv(module));
}

zc::Maybe<ContextualModuleKey> ContextualModuleKey::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto rootsBytes = decoder.decodeByteString(kMaximumContextRootBytes);
  auto moduleBytes = decoder.decodeByteString(kMaximumModuleKeyBytes);
  if (rootsBytes == zc::none || moduleBytes == zc::none || !decoder.finished()) { return zc::none; }
  auto roots = CompilationRootSetQueryKey::decodeCanonical(ZC_ASSERT_NONNULL(rootsBytes).asPtr());
  identity::CanonicalDecoder moduleDecoder(ZC_ASSERT_NONNULL(moduleBytes).asPtr());
  auto module = identity::ModuleKey::decodeCanonical(moduleDecoder);
  if (roots == zc::none || module == zc::none || !moduleDecoder.finished() ||
      ZC_ASSERT_NONNULL(module).encode().asPtr() != ZC_ASSERT_NONNULL(moduleBytes).asPtr()) {
    return zc::none;
  }
  return ContextualModuleKey(zc::mv(ZC_ASSERT_NONNULL(roots)), zc::mv(ZC_ASSERT_NONNULL(module)));
}

ContextualModuleKey ContextualModuleKey::clone() const {
  return ContextualModuleKey(contextRootsField.clone(), moduleField.clone());
}

const CompilationRootSetQueryKey& ContextualModuleKey::contextRoots() const noexcept {
  return contextRootsField;
}

const identity::ModuleKey& ContextualModuleKey::module() const noexcept { return moduleField; }

zc::Array<uint8_t> ContextualModuleKey::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  const auto roots = contextRootsField.encodeCanonical();
  const auto module = moduleField.encode();
  encoder.encodeByteString(roots.asPtr());
  encoder.encodeByteString(module.asPtr());
  return encoder.finish();
}

bool ContextualModuleKey::operator==(const ContextualModuleKey& other) const noexcept {
  return contextRootsField == other.contextRootsField &&
         moduleField.encode().asPtr() == other.moduleField.encode().asPtr();
}

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

zc::StringPtr ActiveDefinitionAuthorityInput::domain() {
  return "zom.query.active-definition-authority"_zc;
}

query::QueryKindContract ActiveDefinitionAuthorityInput::contract() {
  return inputContract(domain());
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

zc::StringPtr ActiveDefinitionAuthorityReadyInput::domain() {
  return "zom.query.active-definition-authority-ready"_zc;
}

query::QueryKindContract ActiveDefinitionAuthorityReadyInput::contract() {
  return inputContract(domain());
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
  return database.registerInputKind<ActiveDefinitionAuthorityInput>() != zc::none &&
         database.registerInputKind<ActiveDefinitionAuthorityReadyInput>() != zc::none;
}

}  // namespace zomlang::compiler::driver::incremental_binding_query
