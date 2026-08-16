// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/query/binding/active-identity-membership-query.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/binder/graph/module-skeleton-query.h"
#include "zomlang/compiler/driver/query/binding/active-definition-authority-query.h"
#include "zomlang/compiler/driver/query/binding/incremental-binding-query-adapter.h"
#include "zomlang/compiler/driver/query/module-graph/module-graph-query-input.h"
#include "zomlang/compiler/driver/query/binding/named-identity-inventory-query.h"

namespace zomlang::compiler::driver::incremental_binding_query {
namespace {

constexpr zc::StringPtr kCompilationUnitMembershipDomain =
    "zom.binder.active-compilation-unit-membership"_zc;
constexpr zc::StringPtr kImplementationMembershipDomain =
    "zom.binder.active-implementation-membership"_zc;
constexpr zc::StringPtr kImplementationGenericAuthorityDomain =
    "zom.binder.implementation-generic-authority"_zc;
constexpr zc::StringPtr kGenericMembershipDomain =
    "zom.binder.active-generic-parameter-membership"_zc;
constexpr zc::StringPtr kCallableMembershipDomain =
    "zom.binder.active-callable-parameter-membership"_zc;
constexpr uint64_t kMaximumActiveCrates = 4096;
constexpr uint64_t kMaximumEqualImplementationOccurrences = 1048576;
constexpr uint64_t kMaximumIdentityBytes = 128 * 1024 * 1024;
constexpr uint64_t kByteStringPrefixBytes = sizeof(uint64_t);

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

bool sameUnit(const identity::CompilationUnitIdentity& left,
              const identity::CompilationUnitIdentity& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

template <typename Value>
bool sameIdentity(const Value& left, const Value& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

template <typename Value>
zc::Maybe<Value> decodeIdentity(zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto value = Value::decodeCanonical(decoder);
  if (value == zc::none || !decoder.finished() ||
      ZC_ASSERT_NONNULL(value).encode().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(value));
}

zc::Maybe<identity::CrateKey> decodeStableCrate(const StableCrateQueryKey& stable) {
  return decodeIdentity<identity::CrateKey>(stable.canonicalCrateBytes());
}

zc::Maybe<identity::GenericParameterIdentityRecord> decodeGenericParameterRecord(
    zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto ownerTag = decoder.decodeUint8();
  auto ownerBytes = decoder.decodeBytes(32);
  auto kind = decoder.decodeUint8();
  auto ordinal = decoder.decodeUint32();
  if (ownerTag == zc::none || ownerBytes == zc::none || kind == zc::none || ordinal == zc::none ||
      !decoder.finished() ||
      ZC_ASSERT_NONNULL(kind) != static_cast<uint8_t>(identity::GenericParameterKind::Type)) {
    return zc::none;
  }
  zc::Maybe<identity::StableGenericParameterOwnerKey> owner;
  if (ZC_ASSERT_NONNULL(ownerTag) ==
      static_cast<uint8_t>(identity::StableGenericParameterOwnerKind::Definition)) {
    auto key = identity::DefinitionKey::fromBytes(ZC_ASSERT_NONNULL(ownerBytes).asPtr());
    if (key == zc::none) { return zc::none; }
    owner = identity::StableGenericParameterOwnerKey::definition(zc::mv(ZC_ASSERT_NONNULL(key)));
  } else if (ZC_ASSERT_NONNULL(ownerTag) ==
             static_cast<uint8_t>(identity::StableGenericParameterOwnerKind::Implementation)) {
    auto key = identity::ImplKey::fromBytes(ZC_ASSERT_NONNULL(ownerBytes).asPtr());
    if (key == zc::none) { return zc::none; }
    owner =
        identity::StableGenericParameterOwnerKey::implementation(zc::mv(ZC_ASSERT_NONNULL(key)));
  } else {
    return zc::none;
  }
  auto result = identity::GenericParameterIdentityRecord::type(zc::mv(ZC_ASSERT_NONNULL(owner)),
                                                               ZC_ASSERT_NONNULL(ordinal));
  return result.encode().asPtr() == bytes
             ? zc::Maybe<identity::GenericParameterIdentityRecord>(zc::mv(result))
             : zc::none;
}

bool samePosition(identity::CallableParameterPosition left,
                  identity::CallableParameterPosition right) {
  const auto leftOrdinal = left.ordinal();
  const auto rightOrdinal = right.ordinal();
  return left.kind() == right.kind() && (leftOrdinal == zc::none) == (rightOrdinal == zc::none) &&
         (leftOrdinal == zc::none ||
          ZC_ASSERT_NONNULL(leftOrdinal) == ZC_ASSERT_NONNULL(rightOrdinal));
}

zc::Maybe<identity::CallableParameterPosition> decodeCallableParameterPosition(
    identity::CanonicalDecoder& decoder) {
  auto tag = decoder.decodeUint8();
  if (tag == zc::none) { return zc::none; }
  if (ZC_ASSERT_NONNULL(tag) ==
      static_cast<uint8_t>(identity::CallableParameterPositionKind::Receiver)) {
    return identity::CallableParameterPosition::receiver();
  }
  if (ZC_ASSERT_NONNULL(tag) !=
      static_cast<uint8_t>(identity::CallableParameterPositionKind::Ordinary)) {
    return zc::none;
  }
  auto ordinal = decoder.decodeUint32();
  if (ordinal == zc::none) { return zc::none; }
  return identity::CallableParameterPosition::ordinary(ZC_ASSERT_NONNULL(ordinal));
}

zc::Maybe<identity::CallableParameterIdentityRecord> decodeCallableParameterRecord(
    zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto ownerBytes = decoder.decodeBytes(32);
  if (ownerBytes == zc::none) { return zc::none; }
  auto owner = identity::DefinitionKey::fromBytes(ZC_ASSERT_NONNULL(ownerBytes).asPtr());
  auto position = decodeCallableParameterPosition(decoder);
  if (owner == zc::none || position == zc::none || !decoder.finished()) { return zc::none; }
  auto result = identity::CallableParameterIdentityRecord::from(zc::mv(ZC_ASSERT_NONNULL(owner)),
                                                                ZC_ASSERT_NONNULL(position));
  return result.encode().asPtr() == bytes
             ? zc::Maybe<identity::CallableParameterIdentityRecord>(zc::mv(result))
             : zc::none;
}

template <typename Record>
bool resultMatches(zc::StringPtr domain, const ActiveMembershipResult<Record>& left,
                   const ActiveMembershipResult<Record>& right) {
  return left.encodeCanonical(domain).asPtr() == right.encodeCanonical(domain).asPtr();
}

}  // namespace

ActiveCompilationUnitMembership::ActiveCompilationUnitMembership(
    identity::CompilationUnitIdentity&& unit,
    zc::Vector<identity::CrateKey>&& activeCrates) noexcept
    : unitField(zc::mv(unit)), activeCrateFields(zc::mv(activeCrates)) {}

zc::Maybe<ActiveCompilationUnitMembership> ActiveCompilationUnitMembership::from(
    identity::CompilationUnitIdentity&& unit, zc::Vector<identity::CrateKey>&& activeCrates) {
  if (activeCrates.empty() || activeCrates.size() > kMaximumActiveCrates) { return zc::none; }
  for (const auto& crate : activeCrates) {
    if (!sameUnit(crate.unit(), unit)) { return zc::none; }
  }
  for (size_t index = 1; index < activeCrates.size(); ++index) {
    auto current = zc::mv(activeCrates[index]);
    const auto currentBytes = current.encode();
    size_t insertion = index;
    while (insertion != 0 &&
           compareBytes(currentBytes.asPtr(), activeCrates[insertion - 1].encode().asPtr()) < 0) {
      activeCrates[insertion] = zc::mv(activeCrates[insertion - 1]);
      --insertion;
    }
    activeCrates[insertion] = zc::mv(current);
  }
  for (size_t index = 1; index < activeCrates.size(); ++index) {
    if (sameIdentity(activeCrates[index - 1], activeCrates[index])) { return zc::none; }
  }
  ActiveCompilationUnitMembership result(zc::mv(unit), zc::mv(activeCrates));
  if (result.encodeCanonical().size() > kMaximumIdentityBytes) { return zc::none; }
  return zc::mv(result);
}

zc::Maybe<ActiveCompilationUnitMembership> ActiveCompilationUnitMembership::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() > kMaximumIdentityBytes ||
      bytes.size() <= kCompilationUnitMembershipDomain.size() ||
      bytes.first(kCompilationUnitMembershipDomain.size()) !=
          kCompilationUnitMembershipDomain.asBytes() ||
      bytes[kCompilationUnitMembershipDomain.size()] != 0) {
    return zc::none;
  }
  identity::CanonicalDecoder decoder(bytes.slice(kCompilationUnitMembershipDomain.size() + 1));
  auto unitBytes = decoder.decodeByteString(kMaximumIdentityBytes);
  auto count = decoder.decodeSequenceSize(kMaximumActiveCrates);
  if (unitBytes == zc::none || count == zc::none || ZC_ASSERT_NONNULL(count) == 0 ||
      ZC_ASSERT_NONNULL(count) > decoder.remaining() / kByteStringPrefixBytes) {
    return zc::none;
  }
  identity::CanonicalDecoder unitDecoder(ZC_ASSERT_NONNULL(unitBytes).asPtr());
  auto unit = identity::CompilationUnitIdentity::decodeCanonical(unitDecoder);
  if (unit == zc::none || !unitDecoder.finished()) { return zc::none; }
  zc::Vector<identity::CrateKey> crates(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto crateBytes = decoder.decodeByteString(kMaximumIdentityBytes);
    if (crateBytes == zc::none) { return zc::none; }
    auto crate = decodeIdentity<identity::CrateKey>(ZC_ASSERT_NONNULL(crateBytes).asPtr());
    if (crate == zc::none) { return zc::none; }
    crates.add(zc::mv(ZC_ASSERT_NONNULL(crate)));
  }
  if (!decoder.finished()) { return zc::none; }
  auto result = from(zc::mv(ZC_ASSERT_NONNULL(unit)), zc::mv(crates));
  if (result == zc::none || ZC_ASSERT_NONNULL(result).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(result));
}

ActiveCompilationUnitMembership ActiveCompilationUnitMembership::clone() const {
  zc::Vector<identity::CrateKey> crates(activeCrateFields.size());
  for (const auto& crate : activeCrateFields) { crates.add(crate.clone()); }
  return ActiveCompilationUnitMembership(unitField.clone(), zc::mv(crates));
}

const identity::CompilationUnitIdentity& ActiveCompilationUnitMembership::unit() const noexcept {
  return unitField;
}

zc::ArrayPtr<const identity::CrateKey> ActiveCompilationUnitMembership::activeCrates() const {
  return activeCrateFields.asPtr();
}

zc::Array<uint8_t> ActiveCompilationUnitMembership::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  auto unitBytes = unitField.encode();
  encoder.encodeByteString(unitBytes.asPtr());
  encoder.encodeSequenceSize(activeCrateFields.size());
  for (const auto& crate : activeCrateFields) {
    auto crateBytes = crate.encode();
    encoder.encodeByteString(crateBytes.asPtr());
  }
  auto tail = encoder.finish();
  zc::Vector<uint8_t> result(kCompilationUnitMembershipDomain.size() + 1 + tail.size());
  result.addAll(kCompilationUnitMembershipDomain.asBytes());
  result.add(0);
  result.addAll(tail.asPtr());
  return result.releaseAsArray();
}

bool ActiveCompilationUnitMembership::operator==(
    const ActiveCompilationUnitMembership& other) const {
  return encodeCanonical().asPtr() == other.encodeCanonical().asPtr();
}

ActiveImplementationMembershipRecord::ActiveImplementationMembershipRecord(
    binder::StableImplementationQueryKey&& queryKey, identity::ImplIdentityRecord&& record,
    binder::StableImplementationOccurrenceQueryKey&& authorityOccurrence,
    zc::Vector<binder::StableImplementationOccurrenceQueryKey>&& equalOccurrences) noexcept
    : queryKeyField(zc::mv(queryKey)),
      recordField(zc::mv(record)),
      authorityOccurrenceField(zc::mv(authorityOccurrence)),
      equalOccurrenceFields(zc::mv(equalOccurrences)) {}

zc::Maybe<ActiveImplementationMembershipRecord> ActiveImplementationMembershipRecord::from(
    binder::StableImplementationQueryKey&& queryKey, identity::ImplIdentityRecord&& record,
    binder::StableImplementationOccurrenceQueryKey&& authorityOccurrence,
    zc::Vector<binder::StableImplementationOccurrenceQueryKey>&& equalOccurrences) {
  if (equalOccurrences.empty() ||
      equalOccurrences.size() > kMaximumEqualImplementationOccurrences ||
      !sameIdentity(record.module(), queryKey.module()) ||
      identity::ImplKey::compute(record) != queryKey.implementation() ||
      authorityOccurrence != equalOccurrences[0]) {
    return zc::none;
  }
  for (size_t index = 0; index < equalOccurrences.size(); ++index) {
    const auto& occurrence = equalOccurrences[index];
    if (!sameIdentity(occurrence.module(), queryKey.module()) ||
        occurrence.occurrence().implementation() != queryKey.implementation() ||
        (index != 0 && compareBytes(equalOccurrences[index - 1].encodeCanonical().asPtr(),
                                    occurrence.encodeCanonical().asPtr()) >= 0)) {
      return zc::none;
    }
  }
  ActiveImplementationMembershipRecord result(
      zc::mv(queryKey), zc::mv(record), zc::mv(authorityOccurrence), zc::mv(equalOccurrences));
  if (result.encodeCanonical().size() > kMaximumIdentityBytes) { return zc::none; }
  return zc::mv(result);
}

zc::Maybe<ActiveImplementationMembershipRecord>
ActiveImplementationMembershipRecord::decodeCanonical(zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() > kMaximumIdentityBytes ||
      bytes.size() <= kImplementationMembershipDomain.size() ||
      bytes.first(kImplementationMembershipDomain.size()) !=
          kImplementationMembershipDomain.asBytes() ||
      bytes[kImplementationMembershipDomain.size()] != 0) {
    return zc::none;
  }
  identity::CanonicalDecoder decoder(bytes.slice(kImplementationMembershipDomain.size() + 1));
  auto queryKeyBytes = decoder.decodeByteString(kMaximumIdentityBytes);
  auto recordBytes = decoder.decodeByteString(kMaximumIdentityBytes);
  auto authorityBytes = decoder.decodeByteString(kMaximumIdentityBytes);
  auto count = decoder.decodeSequenceSize(kMaximumEqualImplementationOccurrences);
  if (queryKeyBytes == zc::none || recordBytes == zc::none || authorityBytes == zc::none ||
      count == zc::none || ZC_ASSERT_NONNULL(count) == 0 ||
      ZC_ASSERT_NONNULL(count) > decoder.remaining() / kByteStringPrefixBytes) {
    return zc::none;
  }
  auto queryKey = binder::StableImplementationQueryKey::decodeCanonical(
      ZC_ASSERT_NONNULL(queryKeyBytes).asPtr());
  auto record =
      identity::ImplIdentityRecord::decodeCanonical(ZC_ASSERT_NONNULL(recordBytes).asPtr());
  auto authority = binder::StableImplementationOccurrenceQueryKey::decodeCanonical(
      ZC_ASSERT_NONNULL(authorityBytes).asPtr());
  if (queryKey == zc::none || record == zc::none || authority == zc::none) { return zc::none; }
  zc::Vector<binder::StableImplementationOccurrenceQueryKey> occurrences(
      static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto occurrenceBytes = decoder.decodeByteString(kMaximumIdentityBytes);
    if (occurrenceBytes == zc::none) { return zc::none; }
    auto occurrence = binder::StableImplementationOccurrenceQueryKey::decodeCanonical(
        ZC_ASSERT_NONNULL(occurrenceBytes).asPtr());
    if (occurrence == zc::none) { return zc::none; }
    occurrences.add(zc::mv(ZC_ASSERT_NONNULL(occurrence)));
  }
  if (!decoder.finished()) { return zc::none; }
  auto result = from(zc::mv(ZC_ASSERT_NONNULL(queryKey)), zc::mv(ZC_ASSERT_NONNULL(record)),
                     zc::mv(ZC_ASSERT_NONNULL(authority)), zc::mv(occurrences));
  if (result == zc::none || ZC_ASSERT_NONNULL(result).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(result));
}

ActiveImplementationMembershipRecord ActiveImplementationMembershipRecord::clone() const {
  zc::Vector<binder::StableImplementationOccurrenceQueryKey> occurrences(
      equalOccurrenceFields.size());
  for (const auto& occurrence : equalOccurrenceFields) { occurrences.add(occurrence.clone()); }
  return ActiveImplementationMembershipRecord(queryKeyField.clone(), recordField.clone(),
                                              authorityOccurrenceField.clone(),
                                              zc::mv(occurrences));
}

const binder::StableImplementationQueryKey& ActiveImplementationMembershipRecord::queryKey()
    const noexcept {
  return queryKeyField;
}

const identity::ImplIdentityRecord& ActiveImplementationMembershipRecord::record() const noexcept {
  return recordField;
}

const binder::StableImplementationOccurrenceQueryKey&
ActiveImplementationMembershipRecord::authorityOccurrence() const noexcept {
  return authorityOccurrenceField;
}

zc::ArrayPtr<const binder::StableImplementationOccurrenceQueryKey>
ActiveImplementationMembershipRecord::equalOccurrences() const {
  return equalOccurrenceFields.asPtr();
}

zc::Array<uint8_t> ActiveImplementationMembershipRecord::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  auto queryKeyBytes = queryKeyField.encodeCanonical();
  auto recordBytes = recordField.encode();
  auto authorityBytes = authorityOccurrenceField.encodeCanonical();
  encoder.encodeByteString(queryKeyBytes.asPtr());
  encoder.encodeByteString(recordBytes.asPtr());
  encoder.encodeByteString(authorityBytes.asPtr());
  encoder.encodeSequenceSize(equalOccurrenceFields.size());
  for (const auto& occurrence : equalOccurrenceFields) {
    auto occurrenceBytes = occurrence.encodeCanonical();
    encoder.encodeByteString(occurrenceBytes.asPtr());
  }
  auto tail = encoder.finish();
  zc::Vector<uint8_t> result(kImplementationMembershipDomain.size() + 1 + tail.size());
  result.addAll(kImplementationMembershipDomain.asBytes());
  result.add(0);
  result.addAll(tail.asPtr());
  return result.releaseAsArray();
}

bool ActiveImplementationMembershipRecord::operator==(
    const ActiveImplementationMembershipRecord& other) const {
  return encodeCanonical().asPtr() == other.encodeCanonical().asPtr();
}

zc::Array<uint8_t> ActiveImplementationAuthorityInput::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<ActiveImplementationAuthorityInput::Key> ActiveImplementationAuthorityInput::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return Key::decodeCanonical(bytes);
}

zc::Array<uint8_t> ActiveImplementationAuthorityInput::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<ActiveImplementationAuthorityInput::Value>
ActiveImplementationAuthorityInput::decodeValue(zc::ArrayPtr<const uint8_t> bytes) {
  return Value::decodeCanonical(bytes);
}

bool ActiveImplementationAuthorityInputVerifier::verify(
    const ContextualImplementationKey& key, const ActiveImplementationMembershipRecord& record) {
  return key.implementation() == record.queryKey();
}

ImplementationGenericAuthority::ImplementationGenericAuthority(
    binder::StableImplementationQueryKey&& implementation,
    binder::StableImplementationOccurrenceQueryKey&& authorityOccurrence,
    zc::Vector<binder::StableImplementationOccurrenceQueryKey>&& equalOccurrences) noexcept
    : implementationField(zc::mv(implementation)),
      authorityOccurrenceField(zc::mv(authorityOccurrence)),
      equalOccurrenceFields(zc::mv(equalOccurrences)) {}

zc::Maybe<ImplementationGenericAuthority> ImplementationGenericAuthority::from(
    binder::StableImplementationQueryKey&& implementation,
    binder::StableImplementationOccurrenceQueryKey&& authorityOccurrence,
    zc::Vector<binder::StableImplementationOccurrenceQueryKey>&& equalOccurrences) {
  if (equalOccurrences.empty() ||
      equalOccurrences.size() > kMaximumEqualImplementationOccurrences ||
      authorityOccurrence != equalOccurrences[0]) {
    return zc::none;
  }
  for (size_t index = 0; index < equalOccurrences.size(); ++index) {
    const auto& occurrence = equalOccurrences[index];
    if (!sameIdentity(occurrence.module(), implementation.module()) ||
        occurrence.occurrence().implementation() != implementation.implementation() ||
        (index != 0 && compareBytes(equalOccurrences[index - 1].encodeCanonical().asPtr(),
                                    occurrence.encodeCanonical().asPtr()) >= 0)) {
      return zc::none;
    }
  }
  ImplementationGenericAuthority result(zc::mv(implementation), zc::mv(authorityOccurrence),
                                        zc::mv(equalOccurrences));
  if (result.encodeCanonical().size() > kMaximumIdentityBytes) { return zc::none; }
  return zc::mv(result);
}

zc::Maybe<ImplementationGenericAuthority> ImplementationGenericAuthority::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() > kMaximumIdentityBytes ||
      bytes.size() <= kImplementationGenericAuthorityDomain.size() ||
      bytes.first(kImplementationGenericAuthorityDomain.size()) !=
          kImplementationGenericAuthorityDomain.asBytes() ||
      bytes[kImplementationGenericAuthorityDomain.size()] != 0) {
    return zc::none;
  }
  identity::CanonicalDecoder decoder(bytes.slice(kImplementationGenericAuthorityDomain.size() + 1));
  auto implementationBytes = decoder.decodeByteString(kMaximumIdentityBytes);
  auto authorityBytes = decoder.decodeByteString(kMaximumIdentityBytes);
  auto count = decoder.decodeSequenceSize(kMaximumEqualImplementationOccurrences);
  if (implementationBytes == zc::none || authorityBytes == zc::none || count == zc::none ||
      ZC_ASSERT_NONNULL(count) == 0 ||
      ZC_ASSERT_NONNULL(count) > decoder.remaining() / kByteStringPrefixBytes) {
    return zc::none;
  }
  auto implementation = binder::StableImplementationQueryKey::decodeCanonical(
      ZC_ASSERT_NONNULL(implementationBytes).asPtr());
  auto authority = binder::StableImplementationOccurrenceQueryKey::decodeCanonical(
      ZC_ASSERT_NONNULL(authorityBytes).asPtr());
  if (implementation == zc::none || authority == zc::none) { return zc::none; }
  zc::Vector<binder::StableImplementationOccurrenceQueryKey> occurrences(
      static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto occurrenceBytes = decoder.decodeByteString(kMaximumIdentityBytes);
    if (occurrenceBytes == zc::none) { return zc::none; }
    auto occurrence = binder::StableImplementationOccurrenceQueryKey::decodeCanonical(
        ZC_ASSERT_NONNULL(occurrenceBytes).asPtr());
    if (occurrence == zc::none) { return zc::none; }
    occurrences.add(zc::mv(ZC_ASSERT_NONNULL(occurrence)));
  }
  if (!decoder.finished()) { return zc::none; }
  auto result = from(zc::mv(ZC_ASSERT_NONNULL(implementation)),
                     zc::mv(ZC_ASSERT_NONNULL(authority)), zc::mv(occurrences));
  if (result == zc::none || ZC_ASSERT_NONNULL(result).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(result));
}

ImplementationGenericAuthority ImplementationGenericAuthority::clone() const {
  zc::Vector<binder::StableImplementationOccurrenceQueryKey> occurrences(
      equalOccurrenceFields.size());
  for (const auto& occurrence : equalOccurrenceFields) { occurrences.add(occurrence.clone()); }
  return ImplementationGenericAuthority(implementationField.clone(),
                                        authorityOccurrenceField.clone(), zc::mv(occurrences));
}

const binder::StableImplementationQueryKey& ImplementationGenericAuthority::implementation()
    const noexcept {
  return implementationField;
}

const binder::StableImplementationOccurrenceQueryKey&
ImplementationGenericAuthority::authorityOccurrence() const noexcept {
  return authorityOccurrenceField;
}

zc::ArrayPtr<const binder::StableImplementationOccurrenceQueryKey>
ImplementationGenericAuthority::equalOccurrences() const {
  return equalOccurrenceFields.asPtr();
}

zc::Array<uint8_t> ImplementationGenericAuthority::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  auto implementationBytes = implementationField.encodeCanonical();
  auto authorityBytes = authorityOccurrenceField.encodeCanonical();
  encoder.encodeByteString(implementationBytes.asPtr());
  encoder.encodeByteString(authorityBytes.asPtr());
  encoder.encodeSequenceSize(equalOccurrenceFields.size());
  for (const auto& occurrence : equalOccurrenceFields) {
    auto occurrenceBytes = occurrence.encodeCanonical();
    encoder.encodeByteString(occurrenceBytes.asPtr());
  }
  auto tail = encoder.finish();
  zc::Vector<uint8_t> result(kImplementationGenericAuthorityDomain.size() + 1 + tail.size());
  result.addAll(kImplementationGenericAuthorityDomain.asBytes());
  result.add(0);
  result.addAll(tail.asPtr());
  return result.releaseAsArray();
}

ActiveGenericParameterOwner::ActiveGenericParameterOwner(
    ActiveGenericDefinitionOwner&& owner) noexcept
    : valueField(zc::mv(owner)) {}

ActiveGenericParameterOwner::ActiveGenericParameterOwner(
    ActiveGenericImplementationOwner&& owner) noexcept
    : valueField(zc::mv(owner)) {}

ActiveGenericParameterOwner ActiveGenericParameterOwner::definition(
    binder::StableDefinitionQueryKey&& definition, binder::IdentitySyntaxSiteKey&& headerSite) {
  return ActiveGenericParameterOwner(
      ActiveGenericDefinitionOwner{zc::mv(definition), zc::mv(headerSite)});
}

ActiveGenericParameterOwner ActiveGenericParameterOwner::implementation(
    ImplementationGenericAuthority&& authority) {
  return ActiveGenericParameterOwner(ActiveGenericImplementationOwner{zc::mv(authority)});
}

ActiveGenericParameterOwner ActiveGenericParameterOwner::clone() const {
  if (valueField.is<ActiveGenericDefinitionOwner>()) {
    const auto& owner = valueField.get<ActiveGenericDefinitionOwner>();
    return definition(owner.definition.clone(), owner.headerSite.clone());
  }
  return implementation(valueField.get<ActiveGenericImplementationOwner>().authority.clone());
}

const ActiveGenericParameterOwnerValue& ActiveGenericParameterOwner::value() const noexcept {
  return valueField;
}

ActiveGenericParameterMembership::ActiveGenericParameterMembership(
    binder::StableGenericParameterQueryKey&& queryKey,
    identity::GenericParameterIdentityRecord&& record, ActiveGenericParameterOwner&& owner,
    uint32_t ordinal) noexcept
    : queryKeyField(zc::mv(queryKey)),
      recordField(zc::mv(record)),
      ownerField(zc::mv(owner)),
      ordinalField(ordinal) {}

zc::Maybe<ActiveGenericParameterMembership> ActiveGenericParameterMembership::from(
    binder::StableGenericParameterQueryKey&& queryKey,
    identity::GenericParameterIdentityRecord&& record, ActiveGenericParameterOwner&& owner,
    uint32_t ordinal) {
  if (identity::GenericParameterKey::compute(record) != queryKey.parameter() ||
      record.ordinal() != ordinal) {
    return zc::none;
  }
  if (owner.value().is<ActiveGenericDefinitionOwner>()) {
    const auto& definition = owner.value().get<ActiveGenericDefinitionOwner>();
    if (!sameIdentity(definition.definition.module(), queryKey.module()) ||
        !sameIdentity(definition.headerSite.module(), queryKey.module()) ||
        record.owner().kind() != identity::StableGenericParameterOwnerKind::Definition ||
        ZC_ASSERT_NONNULL(record.owner().definitionKey()) != definition.definition.definition()) {
      return zc::none;
    }
  } else {
    const auto& implementation = owner.value().get<ActiveGenericImplementationOwner>().authority;
    if (!sameIdentity(implementation.implementation().module(), queryKey.module()) ||
        record.owner().kind() != identity::StableGenericParameterOwnerKind::Implementation ||
        ZC_ASSERT_NONNULL(record.owner().implKey()) !=
            implementation.implementation().implementation()) {
      return zc::none;
    }
  }
  ActiveGenericParameterMembership result(zc::mv(queryKey), zc::mv(record), zc::mv(owner), ordinal);
  if (result.encodeCanonical().size() > kMaximumIdentityBytes) { return zc::none; }
  return zc::mv(result);
}

zc::Maybe<ActiveGenericParameterMembership> ActiveGenericParameterMembership::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() > kMaximumIdentityBytes || bytes.size() <= kGenericMembershipDomain.size() ||
      bytes.first(kGenericMembershipDomain.size()) != kGenericMembershipDomain.asBytes() ||
      bytes[kGenericMembershipDomain.size()] != 0) {
    return zc::none;
  }
  identity::CanonicalDecoder decoder(bytes.slice(kGenericMembershipDomain.size() + 1));
  auto queryKeyBytes = decoder.decodeByteString(kMaximumIdentityBytes);
  auto recordBytes = decoder.decodeByteString(kMaximumIdentityBytes);
  auto ownerTag = decoder.decodeUint8();
  if (queryKeyBytes == zc::none || recordBytes == zc::none || ownerTag == zc::none) {
    return zc::none;
  }
  auto queryKey = binder::StableGenericParameterQueryKey::decodeCanonical(
      ZC_ASSERT_NONNULL(queryKeyBytes).asPtr());
  auto record = decodeGenericParameterRecord(ZC_ASSERT_NONNULL(recordBytes).asPtr());
  if (queryKey == zc::none || record == zc::none) { return zc::none; }
  zc::Maybe<ActiveGenericParameterOwner> owner;
  if (ZC_ASSERT_NONNULL(ownerTag) == 0x01) {
    auto definitionBytes = decoder.decodeByteString(kMaximumIdentityBytes);
    auto siteBytes = decoder.decodeByteString(kMaximumIdentityBytes);
    if (definitionBytes == zc::none || siteBytes == zc::none) { return zc::none; }
    auto definition = binder::StableDefinitionQueryKey::decodeCanonical(
        ZC_ASSERT_NONNULL(definitionBytes).asPtr());
    identity::CanonicalDecoder siteDecoder(ZC_ASSERT_NONNULL(siteBytes).asPtr());
    auto site = binder::IdentitySyntaxSiteKey::decodeCanonical(siteDecoder);
    if (definition == zc::none || site == zc::none || !siteDecoder.finished()) { return zc::none; }
    owner = ActiveGenericParameterOwner::definition(zc::mv(ZC_ASSERT_NONNULL(definition)),
                                                    zc::mv(ZC_ASSERT_NONNULL(site)));
  } else if (ZC_ASSERT_NONNULL(ownerTag) == 0x02) {
    auto authorityBytes = decoder.decodeByteString(kMaximumIdentityBytes);
    if (authorityBytes == zc::none) { return zc::none; }
    auto authority =
        ImplementationGenericAuthority::decodeCanonical(ZC_ASSERT_NONNULL(authorityBytes).asPtr());
    if (authority == zc::none) { return zc::none; }
    owner = ActiveGenericParameterOwner::implementation(zc::mv(ZC_ASSERT_NONNULL(authority)));
  } else {
    return zc::none;
  }
  auto ordinal = decoder.decodeUint32();
  if (ordinal == zc::none || !decoder.finished()) { return zc::none; }
  auto result = from(zc::mv(ZC_ASSERT_NONNULL(queryKey)), zc::mv(ZC_ASSERT_NONNULL(record)),
                     zc::mv(ZC_ASSERT_NONNULL(owner)), ZC_ASSERT_NONNULL(ordinal));
  if (result == zc::none || ZC_ASSERT_NONNULL(result).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(result));
}

ActiveGenericParameterMembership ActiveGenericParameterMembership::clone() const {
  return ActiveGenericParameterMembership(queryKeyField.clone(), recordField.clone(),
                                          ownerField.clone(), ordinalField);
}

const binder::StableGenericParameterQueryKey& ActiveGenericParameterMembership::queryKey()
    const noexcept {
  return queryKeyField;
}

const identity::GenericParameterIdentityRecord& ActiveGenericParameterMembership::record()
    const noexcept {
  return recordField;
}

const ActiveGenericParameterOwner& ActiveGenericParameterMembership::owner() const noexcept {
  return ownerField;
}

uint32_t ActiveGenericParameterMembership::ordinal() const noexcept { return ordinalField; }

zc::Array<uint8_t> ActiveGenericParameterMembership::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  auto queryKeyBytes = queryKeyField.encodeCanonical();
  auto recordBytes = recordField.encode();
  encoder.encodeByteString(queryKeyBytes.asPtr());
  encoder.encodeByteString(recordBytes.asPtr());
  if (ownerField.value().is<ActiveGenericDefinitionOwner>()) {
    encoder.encodeUint8(0x01);
    const auto& owner = ownerField.value().get<ActiveGenericDefinitionOwner>();
    auto definitionBytes = owner.definition.encodeCanonical();
    auto siteBytes = owner.headerSite.encode();
    encoder.encodeByteString(definitionBytes.asPtr());
    encoder.encodeByteString(siteBytes.asPtr());
  } else {
    encoder.encodeUint8(0x02);
    auto authorityBytes =
        ownerField.value().get<ActiveGenericImplementationOwner>().authority.encodeCanonical();
    encoder.encodeByteString(authorityBytes.asPtr());
  }
  encoder.encodeUint32(ordinalField);
  auto tail = encoder.finish();
  zc::Vector<uint8_t> result(kGenericMembershipDomain.size() + 1 + tail.size());
  result.addAll(kGenericMembershipDomain.asBytes());
  result.add(0);
  result.addAll(tail.asPtr());
  return result.releaseAsArray();
}

zc::Array<uint8_t> ActiveGenericParameterAuthorityInput::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<ActiveGenericParameterAuthorityInput::Key>
ActiveGenericParameterAuthorityInput::decodeKey(zc::ArrayPtr<const uint8_t> bytes) {
  return Key::decodeCanonical(bytes);
}

zc::Array<uint8_t> ActiveGenericParameterAuthorityInput::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<ActiveGenericParameterAuthorityInput::Value>
ActiveGenericParameterAuthorityInput::decodeValue(zc::ArrayPtr<const uint8_t> bytes) {
  return Value::decodeCanonical(bytes);
}

bool ActiveGenericParameterAuthorityInputVerifier::verify(
    const ContextualGenericParameterKey& key, const ActiveGenericParameterMembership& record) {
  return key.parameter() == record.queryKey();
}

ActiveCallableParameterMembershipRecord::ActiveCallableParameterMembershipRecord(
    binder::StableCallableParameterQueryKey&& queryKey,
    identity::CallableParameterIdentityRecord&& record, binder::StableDefinitionQueryKey&& owner,
    binder::IdentitySyntaxSiteKey&& headerSite, identity::CallableParameterPosition position,
    zc::Maybe<identity::DeclaredDefinitionName>&& name, bool receiverLegal) noexcept
    : queryKeyField(zc::mv(queryKey)),
      recordField(zc::mv(record)),
      ownerField(zc::mv(owner)),
      headerSiteField(zc::mv(headerSite)),
      positionField(position),
      nameField(zc::mv(name)),
      receiverLegalField(receiverLegal) {}

zc::Maybe<ActiveCallableParameterMembershipRecord> ActiveCallableParameterMembershipRecord::from(
    binder::StableCallableParameterQueryKey&& queryKey,
    identity::CallableParameterIdentityRecord&& record, binder::StableDefinitionQueryKey&& owner,
    binder::IdentitySyntaxSiteKey&& headerSite, identity::CallableParameterPosition position,
    zc::Maybe<identity::DeclaredDefinitionName>&& name, bool receiverLegal) {
  const bool receiver = position.kind() == identity::CallableParameterPositionKind::Receiver;
  if (identity::CallableParameterKey::compute(record) != queryKey.parameter() ||
      record.owner() != owner.definition() || !sameIdentity(owner.module(), queryKey.module()) ||
      !sameIdentity(headerSite.module(), queryKey.module()) ||
      !samePosition(record.position(), position) || (receiver && name != zc::none) ||
      (!receiver && name == zc::none) || !receiverLegal) {
    return zc::none;
  }
  ActiveCallableParameterMembershipRecord result(zc::mv(queryKey), zc::mv(record), zc::mv(owner),
                                                 zc::mv(headerSite), position, zc::mv(name),
                                                 receiverLegal);
  if (result.encodeCanonical().size() > kMaximumIdentityBytes) { return zc::none; }
  return zc::mv(result);
}

zc::Maybe<ActiveCallableParameterMembershipRecord>
ActiveCallableParameterMembershipRecord::decodeCanonical(zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() > kMaximumIdentityBytes || bytes.size() <= kCallableMembershipDomain.size() ||
      bytes.first(kCallableMembershipDomain.size()) != kCallableMembershipDomain.asBytes() ||
      bytes[kCallableMembershipDomain.size()] != 0) {
    return zc::none;
  }
  identity::CanonicalDecoder decoder(bytes.slice(kCallableMembershipDomain.size() + 1));
  auto queryKeyBytes = decoder.decodeByteString(kMaximumIdentityBytes);
  auto recordBytes = decoder.decodeByteString(kMaximumIdentityBytes);
  auto ownerBytes = decoder.decodeByteString(kMaximumIdentityBytes);
  auto siteBytes = decoder.decodeByteString(kMaximumIdentityBytes);
  if (queryKeyBytes == zc::none || recordBytes == zc::none || ownerBytes == zc::none ||
      siteBytes == zc::none) {
    return zc::none;
  }
  auto queryKey = binder::StableCallableParameterQueryKey::decodeCanonical(
      ZC_ASSERT_NONNULL(queryKeyBytes).asPtr());
  auto record = decodeCallableParameterRecord(ZC_ASSERT_NONNULL(recordBytes).asPtr());
  auto owner =
      binder::StableDefinitionQueryKey::decodeCanonical(ZC_ASSERT_NONNULL(ownerBytes).asPtr());
  identity::CanonicalDecoder siteDecoder(ZC_ASSERT_NONNULL(siteBytes).asPtr());
  auto site = binder::IdentitySyntaxSiteKey::decodeCanonical(siteDecoder);
  auto position = decodeCallableParameterPosition(decoder);
  auto nameTag = decoder.decodeUint8();
  if (queryKey == zc::none || record == zc::none || owner == zc::none || site == zc::none ||
      !siteDecoder.finished() || position == zc::none || nameTag == zc::none) {
    return zc::none;
  }
  zc::Maybe<identity::DeclaredDefinitionName> name;
  if (ZC_ASSERT_NONNULL(nameTag) == 0x01) {
    name = identity::DeclaredDefinitionName::decodeCanonical(decoder);
    if (name == zc::none) { return zc::none; }
  } else if (ZC_ASSERT_NONNULL(nameTag) != 0x00) {
    return zc::none;
  }
  auto receiverLegal = decoder.decodeUint8();
  if (receiverLegal == zc::none || ZC_ASSERT_NONNULL(receiverLegal) > 1 || !decoder.finished()) {
    return zc::none;
  }
  auto result =
      from(zc::mv(ZC_ASSERT_NONNULL(queryKey)), zc::mv(ZC_ASSERT_NONNULL(record)),
           zc::mv(ZC_ASSERT_NONNULL(owner)), zc::mv(ZC_ASSERT_NONNULL(site)),
           ZC_ASSERT_NONNULL(position), zc::mv(name), ZC_ASSERT_NONNULL(receiverLegal) != 0);
  if (result == zc::none || ZC_ASSERT_NONNULL(result).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(result));
}

ActiveCallableParameterMembershipRecord ActiveCallableParameterMembershipRecord::clone() const {
  zc::Maybe<identity::DeclaredDefinitionName> name;
  ZC_IF_SOME(value, nameField) { name = value.clone(); }
  return ActiveCallableParameterMembershipRecord(queryKeyField.clone(), recordField.clone(),
                                                 ownerField.clone(), headerSiteField.clone(),
                                                 positionField, zc::mv(name), receiverLegalField);
}

const binder::StableCallableParameterQueryKey& ActiveCallableParameterMembershipRecord::queryKey()
    const noexcept {
  return queryKeyField;
}

const identity::CallableParameterIdentityRecord& ActiveCallableParameterMembershipRecord::record()
    const noexcept {
  return recordField;
}

const binder::StableDefinitionQueryKey& ActiveCallableParameterMembershipRecord::owner()
    const noexcept {
  return ownerField;
}

const binder::IdentitySyntaxSiteKey& ActiveCallableParameterMembershipRecord::headerSite()
    const noexcept {
  return headerSiteField;
}

identity::CallableParameterPosition ActiveCallableParameterMembershipRecord::position()
    const noexcept {
  return positionField;
}

const zc::Maybe<identity::DeclaredDefinitionName>& ActiveCallableParameterMembershipRecord::name()
    const noexcept {
  return nameField;
}

bool ActiveCallableParameterMembershipRecord::receiverLegal() const noexcept {
  return receiverLegalField;
}

zc::Array<uint8_t> ActiveCallableParameterMembershipRecord::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  auto queryKeyBytes = queryKeyField.encodeCanonical();
  auto recordBytes = recordField.encode();
  auto ownerBytes = ownerField.encodeCanonical();
  auto siteBytes = headerSiteField.encode();
  encoder.encodeByteString(queryKeyBytes.asPtr());
  encoder.encodeByteString(recordBytes.asPtr());
  encoder.encodeByteString(ownerBytes.asPtr());
  encoder.encodeByteString(siteBytes.asPtr());
  positionField.encode(encoder);
  ZC_IF_SOME(name, nameField) {
    encoder.encodeUint8(0x01);
    name.encode(encoder);
  } else {
    encoder.encodeUint8(0x00);
  }
  encoder.encodeUint8(receiverLegalField ? 1 : 0);
  auto tail = encoder.finish();
  zc::Vector<uint8_t> result(kCallableMembershipDomain.size() + 1 + tail.size());
  result.addAll(kCallableMembershipDomain.asBytes());
  result.add(0);
  result.addAll(tail.asPtr());
  return result.releaseAsArray();
}

zc::Array<uint8_t> ActiveCallableParameterAuthorityInput::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<ActiveCallableParameterAuthorityInput::Key>
ActiveCallableParameterAuthorityInput::decodeKey(zc::ArrayPtr<const uint8_t> bytes) {
  return Key::decodeCanonical(bytes);
}

zc::Array<uint8_t> ActiveCallableParameterAuthorityInput::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<ActiveCallableParameterAuthorityInput::Value>
ActiveCallableParameterAuthorityInput::decodeValue(zc::ArrayPtr<const uint8_t> bytes) {
  return Value::decodeCanonical(bytes);
}

bool ActiveCallableParameterAuthorityInputVerifier::verify(
    const ContextualCallableParameterKey& key,
    const ActiveCallableParameterMembershipRecord& record) {
  return key.parameter() == record.queryKey();
}

zc::Array<uint8_t> ActiveMembershipRecordCodec<ActiveCompilationUnitMembership>::encode(
    const ActiveCompilationUnitMembership& record) {
  return record.encodeCanonical();
}

zc::Maybe<ActiveCompilationUnitMembership> ActiveMembershipRecordCodec<
    ActiveCompilationUnitMembership>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  return ActiveCompilationUnitMembership::decodeCanonical(bytes);
}

zc::Array<uint8_t> ActiveMembershipRecordCodec<identity::CrateKey>::encode(
    const identity::CrateKey& record) {
  return record.encode();
}

zc::Maybe<identity::CrateKey> ActiveMembershipRecordCodec<identity::CrateKey>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  return decodeIdentity<identity::CrateKey>(bytes);
}

zc::Array<uint8_t> ActiveMembershipRecordCodec<identity::SourceFileKey>::encode(
    const identity::SourceFileKey& record) {
  return record.encode();
}

zc::Maybe<identity::SourceFileKey> ActiveMembershipRecordCodec<identity::SourceFileKey>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  return decodeIdentity<identity::SourceFileKey>(bytes);
}

zc::Array<uint8_t> ActiveMembershipRecordCodec<identity::ModuleKey>::encode(
    const identity::ModuleKey& record) {
  return record.encode();
}

zc::Maybe<identity::ModuleKey> ActiveMembershipRecordCodec<identity::ModuleKey>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  return decodeIdentity<identity::ModuleKey>(bytes);
}

zc::Array<uint8_t> ActiveMembershipRecordCodec<identity::DefinitionIdentityRecord>::encode(
    const identity::DefinitionIdentityRecord& record) {
  return record.encode();
}

zc::Maybe<identity::DefinitionIdentityRecord> ActiveMembershipRecordCodec<
    identity::DefinitionIdentityRecord>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  return identity::DefinitionIdentityRecord::decodeCanonical(bytes);
}

zc::Array<uint8_t> ActiveMembershipRecordCodec<ActiveImplementationMembershipRecord>::encode(
    const ActiveImplementationMembershipRecord& record) {
  return record.encodeCanonical();
}

zc::Maybe<ActiveImplementationMembershipRecord> ActiveMembershipRecordCodec<
    ActiveImplementationMembershipRecord>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  return ActiveImplementationMembershipRecord::decodeCanonical(bytes);
}

zc::Array<uint8_t> ActiveMembershipRecordCodec<ActiveGenericParameterMembership>::encode(
    const ActiveGenericParameterMembership& record) {
  return record.encodeCanonical();
}

zc::Maybe<ActiveGenericParameterMembership> ActiveMembershipRecordCodec<
    ActiveGenericParameterMembership>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  return ActiveGenericParameterMembership::decodeCanonical(bytes);
}

zc::Array<uint8_t> ActiveMembershipRecordCodec<ActiveCallableParameterMembershipRecord>::encode(
    const ActiveCallableParameterMembershipRecord& record) {
  return record.encodeCanonical();
}

zc::Maybe<ActiveCallableParameterMembershipRecord> ActiveMembershipRecordCodec<
    ActiveCallableParameterMembershipRecord>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  return ActiveCallableParameterMembershipRecord::decodeCanonical(bytes);
}

#define ZOM_DEFINE_ACTIVE_MEMBERSHIP_CODEC(Name)                                       \
  zc::Array<uint8_t> Name::encodeKey(const Key& key) { return key.encodeCanonical(); } \
  zc::Maybe<Name::Key> Name::decodeKey(zc::ArrayPtr<const uint8_t> bytes) {            \
    return Key::decodeCanonical(bytes);                                                \
  }                                                                                    \
  zc::Array<uint8_t> Name::encodeValue(const Value& value) {                           \
    return value.encodeCanonical(valueDomain);                                         \
  }                                                                                    \
  zc::Maybe<Name::Value> Name::decodeValue(zc::ArrayPtr<const uint8_t> bytes) {        \
    return Value::decodeCanonical(valueDomain, bytes);                                 \
  }

ZOM_DEFINE_ACTIVE_MEMBERSHIP_CODEC(ActiveCompilationUnitMembershipQuery)
ZOM_DEFINE_ACTIVE_MEMBERSHIP_CODEC(ActiveCrateMembershipQuery)
ZOM_DEFINE_ACTIVE_MEMBERSHIP_CODEC(ActiveSourceMembershipQuery)
ZOM_DEFINE_ACTIVE_MEMBERSHIP_CODEC(ActiveModuleMembershipQuery)
ZOM_DEFINE_ACTIVE_MEMBERSHIP_CODEC(ActiveDefinitionMembershipQuery)
ZOM_DEFINE_ACTIVE_MEMBERSHIP_CODEC(ActiveImplementationMembershipQuery)
ZOM_DEFINE_ACTIVE_MEMBERSHIP_CODEC(ActiveGenericParameterMembershipQuery)
ZOM_DEFINE_ACTIVE_MEMBERSHIP_CODEC(ActiveCallableParameterMembershipQuery)

#undef ZOM_DEFINE_ACTIVE_MEMBERSHIP_CODEC

zc::Maybe<ActiveCompilationUnitMembershipQuery::GlobalKey>
ActiveCompilationUnitMembershipQuery::projectGlobalKey(const Key& key) {
  return key.unit().clone();
}

bool ActiveCompilationUnitMembershipQuery::sameAuthority(const Record& left, const Record& right) {
  return left.encodeCanonical().asPtr() == right.encodeCanonical().asPtr();
}

bool ActiveCompilationUnitMembershipQuery::validateAuthority(const Key& key,
                                                             const GlobalKey& globalKey,
                                                             const Record& record) {
  return sameUnit(key.unit(), globalKey) && sameUnit(record.unit(), globalKey);
}

zc::Maybe<ActiveCrateMembershipQuery::GlobalKey> ActiveCrateMembershipQuery::projectGlobalKey(
    const Key& key) {
  return key.crate().clone();
}

bool ActiveCrateMembershipQuery::sameAuthority(const Record& left, const Record& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

bool ActiveCrateMembershipQuery::validateAuthority(const Key& key, const GlobalKey& globalKey,
                                                   const Record& record) {
  return sameIdentity(key.crate(), globalKey) && sameIdentity(record, globalKey);
}

zc::Maybe<ActiveSourceMembershipQuery::GlobalKey> ActiveSourceMembershipQuery::projectGlobalKey(
    const Key& key) {
  return key.source().clone();
}

bool ActiveSourceMembershipQuery::sameAuthority(const Record& left, const Record& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

bool ActiveSourceMembershipQuery::validateAuthority(const Key& key, const GlobalKey& globalKey,
                                                    const Record& record) {
  return sameIdentity(key.source(), globalKey) && sameIdentity(record, globalKey);
}

zc::Maybe<ActiveModuleMembershipQuery::GlobalKey> ActiveModuleMembershipQuery::projectGlobalKey(
    const Key& key) {
  return key.module().clone();
}

bool ActiveModuleMembershipQuery::sameAuthority(const Record& left, const Record& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

bool ActiveModuleMembershipQuery::validateAuthority(const Key& key, const GlobalKey& globalKey,
                                                    const Record& record) {
  return sameIdentity(key.module(), globalKey) && sameIdentity(record, globalKey);
}

zc::Maybe<ActiveDefinitionMembershipQuery::GlobalKey>
ActiveDefinitionMembershipQuery::projectGlobalKey(const Key& key) {
  return key.definition().definition().clone();
}

bool ActiveDefinitionMembershipQuery::sameAuthority(const Record& left, const Record& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

bool ActiveDefinitionMembershipQuery::validateAuthority(const Key& key, const GlobalKey& globalKey,
                                                        const Record& record) {
  return key.definition().definition() == globalKey &&
         ActiveDefinitionAuthorityInputVerifier::verify(key, record) &&
         identity::DefinitionKey::compute(record) == globalKey;
}

zc::Maybe<ActiveImplementationMembershipQuery::GlobalKey>
ActiveImplementationMembershipQuery::projectGlobalKey(const Key& key) {
  return key.implementation().implementation().clone();
}

bool ActiveImplementationMembershipQuery::sameAuthority(const Record& left, const Record& right) {
  return left.encodeCanonical().asPtr() == right.encodeCanonical().asPtr();
}

bool ActiveImplementationMembershipQuery::validateAuthority(const Key& key,
                                                            const GlobalKey& globalKey,
                                                            const Record& record) {
  return key.implementation().implementation() == globalKey &&
         ActiveImplementationAuthorityInputVerifier::verify(key, record) &&
         identity::ImplKey::compute(record.record()) == globalKey;
}

zc::Maybe<ActiveGenericParameterMembershipQuery::GlobalKey>
ActiveGenericParameterMembershipQuery::projectGlobalKey(const Key& key) {
  return key.parameter().parameter().clone();
}

bool ActiveGenericParameterMembershipQuery::sameAuthority(const Record& left, const Record& right) {
  return left.encodeCanonical().asPtr() == right.encodeCanonical().asPtr();
}

bool ActiveGenericParameterMembershipQuery::validateAuthority(const Key& key,
                                                              const GlobalKey& globalKey,
                                                              const Record& record) {
  return key.parameter().parameter() == globalKey &&
         ActiveGenericParameterAuthorityInputVerifier::verify(key, record) &&
         identity::GenericParameterKey::compute(record.record()) == globalKey;
}

zc::Maybe<ActiveCallableParameterMembershipQuery::GlobalKey>
ActiveCallableParameterMembershipQuery::projectGlobalKey(const Key& key) {
  return key.parameter().parameter().clone();
}

bool ActiveCallableParameterMembershipQuery::sameAuthority(const Record& left,
                                                           const Record& right) {
  return left.encodeCanonical().asPtr() == right.encodeCanonical().asPtr();
}

bool ActiveCallableParameterMembershipQuery::validateAuthority(const Key& key,
                                                               const GlobalKey& globalKey,
                                                               const Record& record) {
  return key.parameter().parameter() == globalKey &&
         ActiveCallableParameterAuthorityInputVerifier::verify(key, record) &&
         identity::CallableParameterKey::compute(record.record()) == globalKey;
}

query::TypedQueryResult<ActiveCompilationUnitMembershipQuery::Value>
ActiveCompilationUnitMembershipQuery::provide(query::QueryContext& context, const Key& key) {
  auto authority =
      context.get<module_graph_query::CompleteCompilationContextAuthorityInput>(key.contextRoots());
  if (authority.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(authority.runtimeFailure());
  }
  if (authority.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  if (authority.value().contextRoots() != key.contextRoots()) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto crates = context.get<ActiveCratesQuery>(key.contextRoots());
  if (crates.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(crates.runtimeFailure());
  }
  if (crates.kind() != query::QueryValueKind::Value ||
      authority.value().completeCrates().size() != crates.value().crates().size()) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  zc::Vector<identity::CrateKey> matching;
  for (size_t index = 0; index < crates.value().crates().size(); ++index) {
    const auto& stable = crates.value().crates()[index];
    auto crate = decodeStableCrate(stable);
    if (crate == zc::none) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    if (!sameIdentity(authority.value().completeCrates()[index], ZC_ASSERT_NONNULL(crate)) ||
        (index != 0 && (sameIdentity(authority.value().completeCrates()[index - 1],
                                     authority.value().completeCrates()[index]) ||
                        crates.value().crates()[index - 1].canonicalCrateBytes() ==
                            stable.canonicalCrateBytes()))) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    if (sameUnit(ZC_ASSERT_NONNULL(crate).unit(), key.unit())) {
      matching.add(zc::mv(ZC_ASSERT_NONNULL(crate)));
    }
  }
  if (matching.empty()) { return query::TypedQueryResult<Value>::value(Value::inactive()); }
  auto record = ActiveCompilationUnitMembership::from(key.unit().clone(), zc::mv(matching));
  if (record == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto result = Value::active(valueDomain, zc::mv(ZC_ASSERT_NONNULL(record)));
  if (result == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(result)));
}

bool ActiveCompilationUnitMembershipQuery::verify(query::QueryContext& context, const Key& key,
                                                  const query::TypedQueryResult<Value>& result) {
  if (result.isRuntimeFailure() || result.kind() != query::QueryValueKind::Value) { return false; }
  auto authority =
      context.get<module_graph_query::CompleteCompilationContextAuthorityInput>(key.contextRoots());
  if (authority.isRuntimeFailure() || authority.kind() != query::QueryValueKind::Value ||
      authority.value().contextRoots() != key.contextRoots()) {
    return false;
  }
  auto crates = context.get<ActiveCratesQuery>(key.contextRoots());
  if (crates.isRuntimeFailure() || crates.kind() != query::QueryValueKind::Value ||
      authority.value().completeCrates().size() != crates.value().crates().size()) {
    return false;
  }
  zc::Vector<identity::CrateKey> expectedCrates;
  for (size_t index = 0; index < crates.value().crates().size(); ++index) {
    const auto& stable = crates.value().crates()[index];
    auto crate = decodeStableCrate(stable);
    if (crate == zc::none ||
        !sameIdentity(authority.value().completeCrates()[index], ZC_ASSERT_NONNULL(crate)) ||
        (index != 0 && (sameIdentity(authority.value().completeCrates()[index - 1],
                                     authority.value().completeCrates()[index]) ||
                        crates.value().crates()[index - 1].canonicalCrateBytes() ==
                            stable.canonicalCrateBytes()))) {
      return false;
    }
    if (sameUnit(ZC_ASSERT_NONNULL(crate).unit(), key.unit())) {
      expectedCrates.add(zc::mv(ZC_ASSERT_NONNULL(crate)));
    }
  }
  if (expectedCrates.empty()) {
    return resultMatches(valueDomain, result.value(), Value::inactive());
  }
  auto record = ActiveCompilationUnitMembership::from(key.unit().clone(), zc::mv(expectedCrates));
  if (record == zc::none) { return false; }
  auto expected = Value::active(valueDomain, zc::mv(ZC_ASSERT_NONNULL(record)));
  return expected != zc::none &&
         resultMatches(valueDomain, result.value(), ZC_ASSERT_NONNULL(expected));
}

query::TypedQueryResult<ActiveCrateMembershipQuery::Value> ActiveCrateMembershipQuery::provide(
    query::QueryContext& context, const Key& key) {
  auto unitKey =
      ContextualCompilationUnitKey::from(key.contextRoots().clone(), key.crate().unit().clone());
  auto unit = context.get<ActiveCompilationUnitMembershipQuery>(unitKey);
  if (unit.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(unit.runtimeFailure());
  }
  if (unit.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  bool found = false;
  if (unit.value().isActive()) {
    for (const auto& active : unit.value().record().activeCrates()) {
      if (sameIdentity(active, key.crate())) {
        if (found) {
          return query::TypedQueryResult<Value>::runtimeFailure(
              query::QueryRuntimeFailure::InvariantViolation);
        }
        found = true;
      }
    }
  }
  if (!found) { return query::TypedQueryResult<Value>::value(Value::inactive()); }
  auto result = Value::active(valueDomain, key.crate().clone());
  if (result == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(result)));
}

bool ActiveCrateMembershipQuery::verify(query::QueryContext& context, const Key& key,
                                        const query::TypedQueryResult<Value>& result) {
  if (result.isRuntimeFailure() || result.kind() != query::QueryValueKind::Value) { return false; }
  auto unitKey =
      ContextualCompilationUnitKey::from(key.contextRoots().clone(), key.crate().unit().clone());
  auto unit = context.get<ActiveCompilationUnitMembershipQuery>(unitKey);
  if (unit.isRuntimeFailure() || unit.kind() != query::QueryValueKind::Value) { return false; }
  size_t occurrences = 0;
  if (unit.value().isActive()) {
    for (const auto& active : unit.value().record().activeCrates()) {
      if (sameIdentity(active, key.crate())) { ++occurrences; }
    }
  }
  if (occurrences > 1) { return false; }
  if (occurrences == 0) { return resultMatches(valueDomain, result.value(), Value::inactive()); }
  auto expected = Value::active(valueDomain, key.crate().clone());
  return expected != zc::none &&
         resultMatches(valueDomain, result.value(), ZC_ASSERT_NONNULL(expected));
}

query::TypedQueryResult<ActiveSourceMembershipQuery::Value> ActiveSourceMembershipQuery::provide(
    query::QueryContext& context, const Key& key) {
  auto crateKey =
      ContextualCrateKey::from(key.contextRoots().clone(), key.source().crate().clone());
  auto crate = context.get<ActiveCrateMembershipQuery>(crateKey);
  if (crate.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(crate.runtimeFailure());
  }
  if (crate.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  if (!crate.value().isActive()) {
    return query::TypedQueryResult<Value>::value(Value::inactive());
  }
  if (!sameIdentity(crate.value().record(), key.source().crate())) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto stableCrate = StableCrateQueryKey::fromVerified(key.source().crate());
  auto stableSource = identity::source_query::StableSourceQueryKey::fromVerified(key.source());
  if (stableCrate == zc::none || stableSource == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto sources = context.get<ActiveSourcesQuery>(ZC_ASSERT_NONNULL(stableCrate));
  if (sources.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(sources.runtimeFailure());
  }
  if (sources.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  size_t occurrences = 0;
  for (const auto& source : sources.value().sources()) {
    if (source.canonicalSourceBytes() == ZC_ASSERT_NONNULL(stableSource).canonicalSourceBytes()) {
      ++occurrences;
    }
  }
  if (occurrences > 1) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  if (occurrences == 0) { return query::TypedQueryResult<Value>::value(Value::inactive()); }
  auto result = Value::active(valueDomain, key.source().clone());
  if (result == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(result)));
}

bool ActiveSourceMembershipQuery::verify(query::QueryContext& context, const Key& key,
                                         const query::TypedQueryResult<Value>& result) {
  if (result.isRuntimeFailure() || result.kind() != query::QueryValueKind::Value) { return false; }
  auto crateKey =
      ContextualCrateKey::from(key.contextRoots().clone(), key.source().crate().clone());
  auto crate = context.get<ActiveCrateMembershipQuery>(crateKey);
  if (crate.isRuntimeFailure() || crate.kind() != query::QueryValueKind::Value) { return false; }
  if (!crate.value().isActive()) {
    return resultMatches(valueDomain, result.value(), Value::inactive());
  }
  if (!sameIdentity(crate.value().record(), key.source().crate())) { return false; }
  auto stableCrate = StableCrateQueryKey::fromVerified(key.source().crate());
  auto stableSource = identity::source_query::StableSourceQueryKey::fromVerified(key.source());
  if (stableCrate == zc::none || stableSource == zc::none) { return false; }
  auto sources = context.get<ActiveSourcesQuery>(ZC_ASSERT_NONNULL(stableCrate));
  if (sources.isRuntimeFailure() || sources.kind() != query::QueryValueKind::Value) {
    return false;
  }
  size_t occurrences = 0;
  for (const auto& source : sources.value().sources()) {
    if (source.canonicalSourceBytes() == ZC_ASSERT_NONNULL(stableSource).canonicalSourceBytes()) {
      ++occurrences;
    }
  }
  if (occurrences > 1) { return false; }
  if (occurrences == 0) { return resultMatches(valueDomain, result.value(), Value::inactive()); }
  auto expected = Value::active(valueDomain, key.source().clone());
  return expected != zc::none &&
         resultMatches(valueDomain, result.value(), ZC_ASSERT_NONNULL(expected));
}

query::TypedQueryResult<ActiveModuleMembershipQuery::Value> ActiveModuleMembershipQuery::provide(
    query::QueryContext& context, const Key& key) {
  auto crateKey =
      ContextualCrateKey::from(key.contextRoots().clone(), key.module().crate().clone());
  auto crate = context.get<ActiveCrateMembershipQuery>(crateKey);
  if (crate.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(crate.runtimeFailure());
  }
  if (crate.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  if (!crate.value().isActive()) {
    return query::TypedQueryResult<Value>::value(Value::inactive());
  }
  if (!sameIdentity(crate.value().record(), key.module().crate())) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto modules = context.get<module_graph_query::ActiveModulesQuery>(key.module().crate());
  if (modules.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(modules.runtimeFailure());
  }
  if (modules.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  size_t occurrences = 0;
  for (const auto& module : modules.value().modules()) {
    if (sameIdentity(module, key.module())) { ++occurrences; }
  }
  if (occurrences > 1) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  if (occurrences == 0) { return query::TypedQueryResult<Value>::value(Value::inactive()); }
  auto result = Value::active(valueDomain, key.module().clone());
  if (result == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(result)));
}

bool ActiveModuleMembershipQuery::verify(query::QueryContext& context, const Key& key,
                                         const query::TypedQueryResult<Value>& result) {
  if (result.isRuntimeFailure() || result.kind() != query::QueryValueKind::Value) { return false; }
  auto crateKey =
      ContextualCrateKey::from(key.contextRoots().clone(), key.module().crate().clone());
  auto crate = context.get<ActiveCrateMembershipQuery>(crateKey);
  if (crate.isRuntimeFailure() || crate.kind() != query::QueryValueKind::Value) { return false; }
  if (!crate.value().isActive()) {
    return resultMatches(valueDomain, result.value(), Value::inactive());
  }
  if (!sameIdentity(crate.value().record(), key.module().crate())) { return false; }
  auto modules = context.get<module_graph_query::ActiveModulesQuery>(key.module().crate());
  if (modules.isRuntimeFailure() || modules.kind() != query::QueryValueKind::Value) {
    return false;
  }
  size_t occurrences = 0;
  for (const auto& module : modules.value().modules()) {
    if (sameIdentity(module, key.module())) { ++occurrences; }
  }
  if (occurrences > 1) { return false; }
  if (occurrences == 0) { return resultMatches(valueDomain, result.value(), Value::inactive()); }
  auto expected = Value::active(valueDomain, key.module().clone());
  return expected != zc::none &&
         resultMatches(valueDomain, result.value(), ZC_ASSERT_NONNULL(expected));
}

query::TypedQueryResult<ActiveDefinitionMembershipQuery::Value>
ActiveDefinitionMembershipQuery::provide(query::QueryContext& context, const Key& key) {
  auto authority = context.probeInput<ActiveDefinitionAuthorityInput>(key);
  if (authority.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(authority.runtimeFailure());
  }
  if (authority.kind() != query::QueryValueKind::Value) {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    if (readiness.isRuntimeFailure()) {
      return query::TypedQueryResult<Value>::runtimeFailure(readiness.runtimeFailure());
    }
    if (readiness.kind() != query::QueryValueKind::Value ||
        !CompleteRootIdentityReadinessVerifier::verify(key.contextRoots(), readiness.value())) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    return query::TypedQueryResult<Value>::value(Value::inactive());
  }
  if (!ActiveDefinitionAuthorityInputVerifier::verify(key, authority.value())) {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    if (readiness.isRuntimeFailure()) {
      return query::TypedQueryResult<Value>::runtimeFailure(readiness.runtimeFailure());
    }
    if (readiness.kind() != query::QueryValueKind::Value ||
        !CompleteRootIdentityReadinessVerifier::verify(key.contextRoots(), readiness.value())) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto stableModule = StableModuleQueryKey::fromVerified(key.definition().module());
  if (stableModule == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto inventory = context.get<NamedDefinitionInventoryQuery>(ZC_ASSERT_NONNULL(stableModule));
  auto header = context.get<binder::DefinitionHeaderSyntax>(key.definition());
  if (inventory.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(inventory.runtimeFailure());
  }
  if (header.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(header.runtimeFailure());
  }
  size_t occurrences = 0;
  bool exactInventoryRecord = false;
  if (inventory.kind() == query::QueryValueKind::Value) {
    for (const auto& entry : inventory.value().entries()) {
      if (entry.key() != key.definition().definition()) { continue; }
      ++occurrences;
      exactInventoryRecord = entry.record().encode().asPtr() == authority.value().encode().asPtr();
    }
  }
  bool exactHeader = false;
  if (header.kind() == query::QueryValueKind::Value &&
      header.value().storage().is<binder::BinderQueryValue<binder::StableDefinitionHeader>>()) {
    const auto& candidate =
        header.value().storage().get<binder::BinderQueryValue<binder::StableDefinitionHeader>>();
    exactHeader = candidate.diagnostics.values().size() == 0 &&
                  candidate.value.queryKey() == key.definition() &&
                  candidate.value.record().encode().asPtr() == authority.value().encode().asPtr();
  }
  if (occurrences != 1 || !exactInventoryRecord || !exactHeader) {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    if (readiness.isRuntimeFailure()) {
      return query::TypedQueryResult<Value>::runtimeFailure(readiness.runtimeFailure());
    }
    if (readiness.kind() != query::QueryValueKind::Value ||
        !CompleteRootIdentityReadinessVerifier::verify(key.contextRoots(), readiness.value())) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto result = Value::active(valueDomain, authority.value().clone());
  if (result == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(result)));
}

bool ActiveDefinitionMembershipQuery::verify(query::QueryContext& context, const Key& key,
                                             const query::TypedQueryResult<Value>& result) {
  if (result.isRuntimeFailure() || result.kind() != query::QueryValueKind::Value) { return false; }
  auto authority = context.probeInput<ActiveDefinitionAuthorityInput>(key);
  if (authority.isRuntimeFailure()) { return false; }
  if (authority.kind() != query::QueryValueKind::Value) {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    return readiness.kind() == query::QueryValueKind::Value &&
           CompleteRootIdentityReadinessVerifier::verify(key.contextRoots(), readiness.value()) &&
           resultMatches(valueDomain, result.value(), Value::inactive());
  }
  if (!ActiveDefinitionAuthorityInputVerifier::verify(key, authority.value())) {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    static_cast<void>(readiness);
    return false;
  }
  auto stableModule = StableModuleQueryKey::fromVerified(key.definition().module());
  if (stableModule == zc::none) { return false; }
  auto inventory = context.get<NamedDefinitionInventoryQuery>(ZC_ASSERT_NONNULL(stableModule));
  auto header = context.get<binder::DefinitionHeaderSyntax>(key.definition());
  if (inventory.isRuntimeFailure() || header.isRuntimeFailure() ||
      inventory.kind() != query::QueryValueKind::Value ||
      header.kind() != query::QueryValueKind::Value ||
      !header.value().storage().is<binder::BinderQueryValue<binder::StableDefinitionHeader>>()) {
    return false;
  }
  size_t occurrences = 0;
  bool exactInventoryRecord = false;
  for (const auto& entry : inventory.value().entries()) {
    if (entry.key() != key.definition().definition()) { continue; }
    ++occurrences;
    exactInventoryRecord = entry.record().encode().asPtr() == authority.value().encode().asPtr();
  }
  const auto& candidate =
      header.value().storage().get<binder::BinderQueryValue<binder::StableDefinitionHeader>>();
  const bool exactHeader =
      candidate.diagnostics.values().size() == 0 &&
      candidate.value.queryKey() == key.definition() &&
      candidate.value.record().encode().asPtr() == authority.value().encode().asPtr();
  if (occurrences != 1 || !exactInventoryRecord || !exactHeader) {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    static_cast<void>(readiness);
    return false;
  }
  auto expected = Value::active(valueDomain, authority.value().clone());
  return expected != zc::none &&
         resultMatches(valueDomain, result.value(), ZC_ASSERT_NONNULL(expected));
}

query::TypedQueryResult<ActiveImplementationMembershipQuery::Value>
ActiveImplementationMembershipQuery::provide(query::QueryContext& context, const Key& key) {
  auto authority = context.probeInput<ActiveImplementationAuthorityInput>(key);
  if (authority.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(authority.runtimeFailure());
  }
  if (authority.kind() != query::QueryValueKind::Value) {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    if (readiness.isRuntimeFailure()) {
      return query::TypedQueryResult<Value>::runtimeFailure(readiness.runtimeFailure());
    }
    if (readiness.kind() != query::QueryValueKind::Value ||
        !CompleteRootIdentityReadinessVerifier::verify(key.contextRoots(), readiness.value())) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    return query::TypedQueryResult<Value>::value(Value::inactive());
  }
  if (!ActiveImplementationAuthorityInputVerifier::verify(key, authority.value())) {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    if (readiness.isRuntimeFailure()) {
      return query::TypedQueryResult<Value>::runtimeFailure(readiness.runtimeFailure());
    }
    if (readiness.kind() != query::QueryValueKind::Value ||
        !CompleteRootIdentityReadinessVerifier::verify(key.contextRoots(), readiness.value())) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto moduleKey =
      ContextualModuleKey::from(key.contextRoots().clone(), key.implementation().module().clone());
  auto activeModule = context.get<ActiveModuleMembershipQuery>(moduleKey);
  if (activeModule.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(activeModule.runtimeFailure());
  }
  if (activeModule.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  if (!activeModule.value().isActive()) {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    static_cast<void>(readiness);
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  if (!sameIdentity(activeModule.value().record(), key.implementation().module())) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto stableModule = StableModuleQueryKey::fromVerified(key.implementation().module());
  if (stableModule == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto inventory = context.get<NamedImplementationInventoryQuery>(ZC_ASSERT_NONNULL(stableModule));
  if (inventory.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(inventory.runtimeFailure());
  }
  if (inventory.kind() != query::QueryValueKind::Value) {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    if (readiness.isRuntimeFailure()) {
      return query::TypedQueryResult<Value>::runtimeFailure(readiness.runtimeFailure());
    }
    if (readiness.kind() != query::QueryValueKind::Value ||
        !CompleteRootIdentityReadinessVerifier::verify(key.contextRoots(), readiness.value())) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  zc::Maybe<const binder::NamedImplementationInventoryEntry&> selected;
  for (const auto& entry : inventory.value().entries()) {
    if (entry.key() != key.implementation().implementation()) { continue; }
    if (selected != zc::none) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    selected = entry;
  }
  if (selected == zc::none) {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    if (readiness.isRuntimeFailure()) {
      return query::TypedQueryResult<Value>::runtimeFailure(readiness.runtimeFailure());
    }
    if (readiness.kind() != query::QueryValueKind::Value ||
        !CompleteRootIdentityReadinessVerifier::verify(key.contextRoots(), readiness.value())) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto sites =
      context.getCapability<RevisionLocalImplementationSitesQuery>(ZC_ASSERT_NONNULL(stableModule));
  if (sites.isRuntimeRejected()) {
    return query::TypedQueryResult<Value>::runtimeFailure(sites.runtimeFailure());
  }
  if (!sites.isPublished()) {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    if (readiness.isRuntimeFailure()) {
      return query::TypedQueryResult<Value>::runtimeFailure(readiness.runtimeFailure());
    }
    if (readiness.kind() != query::QueryValueKind::Value ||
        !CompleteRootIdentityReadinessVerifier::verify(key.contextRoots(), readiness.value())) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  zc::Vector<binder::StableImplementationOccurrenceQueryKey> occurrences;
  for (const auto& site : sites.lease().capability().entries()) {
    if (site.occurrence().implementation() != key.implementation().implementation()) { continue; }
    auto occurrence = binder::StableImplementationOccurrenceQueryKey::from(
        key.implementation().module().clone(), site.occurrence().clone());
    if (occurrence == zc::none) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    occurrences.add(zc::mv(ZC_ASSERT_NONNULL(occurrence)));
  }
  bool headersExact = !occurrences.empty();
  for (const auto& occurrence : occurrences) {
    auto header = context.get<binder::ImplementationOccurrenceHeaderSyntax>(occurrence);
    if (header.isRuntimeFailure()) {
      return query::TypedQueryResult<Value>::runtimeFailure(header.runtimeFailure());
    }
    if (header.kind() != query::QueryValueKind::Value ||
        !header.value()
             .storage()
             .is<binder::BinderQueryValue<binder::StableImplementationOccurrenceHeader>>()) {
      headersExact = false;
      continue;
    }
    const auto& value =
        header.value()
            .storage()
            .get<binder::BinderQueryValue<binder::StableImplementationOccurrenceHeader>>();
    if (value.diagnostics.values().size() != 0 || value.value.queryKey() != occurrence ||
        value.value.authority() != key.implementation() ||
        value.value.record().encode().asPtr() !=
            ZC_ASSERT_NONNULL(selected).record().encode().asPtr()) {
      headersExact = false;
    }
  }
  zc::Maybe<binder::StableImplementationOccurrenceQueryKey> authorityOccurrence;
  if (!occurrences.empty()) { authorityOccurrence = occurrences[0].clone(); }
  auto membership =
      authorityOccurrence == zc::none
          ? zc::Maybe<ActiveImplementationMembershipRecord>(zc::none)
          : ActiveImplementationMembershipRecord::from(
                key.implementation().clone(), ZC_ASSERT_NONNULL(selected).record().clone(),
                zc::mv(ZC_ASSERT_NONNULL(authorityOccurrence)), zc::mv(occurrences));
  if (!headersExact || membership == zc::none ||
      !ActiveImplementationAuthorityInputVerifier::verify(key, ZC_ASSERT_NONNULL(membership)) ||
      ZC_ASSERT_NONNULL(membership) != authority.value()) {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    if (readiness.isRuntimeFailure()) {
      return query::TypedQueryResult<Value>::runtimeFailure(readiness.runtimeFailure());
    }
    if (readiness.kind() != query::QueryValueKind::Value ||
        !CompleteRootIdentityReadinessVerifier::verify(key.contextRoots(), readiness.value())) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto result = Value::active(valueDomain, authority.value().clone());
  if (result == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(result)));
}

bool ActiveImplementationMembershipQuery::verify(query::QueryContext& context, const Key& key,
                                                 const query::TypedQueryResult<Value>& result) {
  if (result.isRuntimeFailure() || result.kind() != query::QueryValueKind::Value) { return false; }
  auto authority = context.probeInput<ActiveImplementationAuthorityInput>(key);
  if (authority.isRuntimeFailure()) { return false; }
  if (authority.kind() != query::QueryValueKind::Value) {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    return readiness.kind() == query::QueryValueKind::Value &&
           CompleteRootIdentityReadinessVerifier::verify(key.contextRoots(), readiness.value()) &&
           resultMatches(valueDomain, result.value(), Value::inactive());
  }
  if (!ActiveImplementationAuthorityInputVerifier::verify(key, authority.value())) {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    static_cast<void>(readiness);
    return false;
  }
  auto moduleKey =
      ContextualModuleKey::from(key.contextRoots().clone(), key.implementation().module().clone());
  auto activeModule = context.get<ActiveModuleMembershipQuery>(moduleKey);
  if (activeModule.isRuntimeFailure() || activeModule.kind() != query::QueryValueKind::Value) {
    return false;
  }
  if (!activeModule.value().isActive()) {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    static_cast<void>(readiness);
    return false;
  }
  if (!sameIdentity(activeModule.value().record(), key.implementation().module())) { return false; }
  auto stableModule = StableModuleQueryKey::fromVerified(key.implementation().module());
  if (stableModule == zc::none) { return false; }
  auto inventory = context.get<NamedImplementationInventoryQuery>(ZC_ASSERT_NONNULL(stableModule));
  if (inventory.isRuntimeFailure() || inventory.kind() != query::QueryValueKind::Value) {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    static_cast<void>(readiness);
    return false;
  }
  zc::Maybe<const binder::NamedImplementationInventoryEntry&> selected;
  for (const auto& entry : inventory.value().entries()) {
    if (entry.key() != key.implementation().implementation()) { continue; }
    if (selected != zc::none) { return false; }
    selected = entry;
  }
  if (selected == zc::none) {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    static_cast<void>(readiness);
    return false;
  }
  auto sites =
      context.getCapability<RevisionLocalImplementationSitesQuery>(ZC_ASSERT_NONNULL(stableModule));
  if (!sites.isPublished()) {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    static_cast<void>(readiness);
    return false;
  }
  zc::Vector<binder::StableImplementationOccurrenceQueryKey> occurrences;
  for (const auto& site : sites.lease().capability().entries()) {
    if (site.occurrence().implementation() != key.implementation().implementation()) { continue; }
    auto occurrence = binder::StableImplementationOccurrenceQueryKey::from(
        key.implementation().module().clone(), site.occurrence().clone());
    if (occurrence == zc::none) { return false; }
    occurrences.add(zc::mv(ZC_ASSERT_NONNULL(occurrence)));
  }
  bool headersExact = !occurrences.empty();
  for (const auto& occurrence : occurrences) {
    auto header = context.get<binder::ImplementationOccurrenceHeaderSyntax>(occurrence);
    if (header.isRuntimeFailure() || header.kind() != query::QueryValueKind::Value ||
        !header.value()
             .storage()
             .is<binder::BinderQueryValue<binder::StableImplementationOccurrenceHeader>>()) {
      headersExact = false;
      continue;
    }
    const auto& value =
        header.value()
            .storage()
            .get<binder::BinderQueryValue<binder::StableImplementationOccurrenceHeader>>();
    if (value.diagnostics.values().size() != 0 || value.value.queryKey() != occurrence ||
        value.value.authority() != key.implementation() ||
        value.value.record().encode().asPtr() !=
            ZC_ASSERT_NONNULL(selected).record().encode().asPtr()) {
      headersExact = false;
    }
  }
  zc::Maybe<binder::StableImplementationOccurrenceQueryKey> authorityOccurrence;
  if (!occurrences.empty()) { authorityOccurrence = occurrences[0].clone(); }
  auto membership =
      authorityOccurrence == zc::none
          ? zc::Maybe<ActiveImplementationMembershipRecord>(zc::none)
          : ActiveImplementationMembershipRecord::from(
                key.implementation().clone(), ZC_ASSERT_NONNULL(selected).record().clone(),
                zc::mv(ZC_ASSERT_NONNULL(authorityOccurrence)), zc::mv(occurrences));
  if (!headersExact || membership == zc::none ||
      !ActiveImplementationAuthorityInputVerifier::verify(key, ZC_ASSERT_NONNULL(membership)) ||
      ZC_ASSERT_NONNULL(membership) != authority.value()) {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    static_cast<void>(readiness);
    return false;
  }
  auto expected = Value::active(valueDomain, authority.value().clone());
  return expected != zc::none &&
         resultMatches(valueDomain, result.value(), ZC_ASSERT_NONNULL(expected));
}

query::TypedQueryResult<ActiveGenericParameterMembershipQuery::Value>
ActiveGenericParameterMembershipQuery::provide(query::QueryContext& context, const Key& key) {
  auto authority = context.probeInput<ActiveGenericParameterAuthorityInput>(key);
  if (authority.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(authority.runtimeFailure());
  }
  if (authority.kind() != query::QueryValueKind::Value) {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    if (readiness.isRuntimeFailure()) {
      return query::TypedQueryResult<Value>::runtimeFailure(readiness.runtimeFailure());
    }
    if (readiness.kind() != query::QueryValueKind::Value ||
        !CompleteRootIdentityReadinessVerifier::verify(key.contextRoots(), readiness.value())) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    return query::TypedQueryResult<Value>::value(Value::inactive());
  }

  auto contradiction = [&]() -> query::TypedQueryResult<Value> {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    if (readiness.isRuntimeFailure()) {
      return query::TypedQueryResult<Value>::runtimeFailure(readiness.runtimeFailure());
    }
    if (readiness.kind() != query::QueryValueKind::Value ||
        !CompleteRootIdentityReadinessVerifier::verify(key.contextRoots(), readiness.value())) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  };

  if (!ActiveGenericParameterAuthorityInputVerifier::verify(key, authority.value())) {
    return contradiction();
  }

  const auto& owner = authority.value().owner().value();
  if (owner.is<ActiveGenericDefinitionOwner>()) {
    const auto& definitionOwner = owner.get<ActiveGenericDefinitionOwner>();
    auto contextualDefinition = ContextualDefinitionKey::from(key.contextRoots().clone(),
                                                              definitionOwner.definition.clone());
    auto definition = context.get<ActiveDefinitionMembershipQuery>(contextualDefinition);
    if (definition.isRuntimeFailure()) {
      return query::TypedQueryResult<Value>::runtimeFailure(definition.runtimeFailure());
    }
    if (definition.kind() != query::QueryValueKind::Value || !definition.value().isActive()) {
      return contradiction();
    }

    auto header = context.get<binder::DefinitionHeaderSyntax>(definitionOwner.definition);
    if (header.isRuntimeFailure()) {
      return query::TypedQueryResult<Value>::runtimeFailure(header.runtimeFailure());
    }
    if (header.kind() != query::QueryValueKind::Value ||
        !header.value().storage().is<binder::BinderQueryValue<binder::StableDefinitionHeader>>()) {
      return contradiction();
    }
    const auto& candidate =
        header.value().storage().get<binder::BinderQueryValue<binder::StableDefinitionHeader>>();
    if (candidate.diagnostics.values().size() != 0 ||
        candidate.value.queryKey() != definitionOwner.definition ||
        !candidate.value.authoritySite().sameAs(definitionOwner.headerSite) ||
        candidate.value.record().encode().asPtr() != definition.value().record().encode().asPtr()) {
      return contradiction();
    }

    size_t matches = 0;
    for (const auto& parameter : candidate.value.genericParameters().values()) {
      auto parameterKey = binder::StableGenericParameterQueryKey::from(
          key.parameter().module().clone(), parameter.key().clone());
      if (parameterKey != key.parameter()) { continue; }
      ++matches;
      if (parameter.ordinal() != authority.value().ordinal() ||
          parameter.record().encode().asPtr() != authority.value().record().encode().asPtr() ||
          !parameter.site().value().is<binder::DefinitionAuthoritySite>() ||
          !parameter.site().value().get<binder::DefinitionAuthoritySite>().site.sameAs(
              definitionOwner.headerSite)) {
        return contradiction();
      }
    }
    if (matches != 1) { return contradiction(); }
  } else {
    const auto& implementationOwner = owner.get<ActiveGenericImplementationOwner>().authority;
    auto contextualImplementation = ContextualImplementationKey::from(
        key.contextRoots().clone(), implementationOwner.implementation().clone());
    auto implementation =
        context.get<ActiveImplementationMembershipQuery>(contextualImplementation);
    if (implementation.isRuntimeFailure()) {
      return query::TypedQueryResult<Value>::runtimeFailure(implementation.runtimeFailure());
    }
    if (implementation.kind() != query::QueryValueKind::Value ||
        !implementation.value().isActive()) {
      return contradiction();
    }
    const auto& membership = implementation.value().record();
    if (membership.queryKey() != implementationOwner.implementation() ||
        membership.authorityOccurrence() != implementationOwner.authorityOccurrence() ||
        membership.equalOccurrences().size() != implementationOwner.equalOccurrences().size()) {
      return contradiction();
    }
    for (size_t index = 0; index < membership.equalOccurrences().size(); ++index) {
      if (membership.equalOccurrences()[index] != implementationOwner.equalOccurrences()[index]) {
        return contradiction();
      }
    }

    zc::Maybe<identity::DeclaredDefinitionName> parameterName;
    for (const auto& occurrence : implementationOwner.equalOccurrences()) {
      auto header = context.get<binder::ImplementationOccurrenceHeaderSyntax>(occurrence);
      if (header.isRuntimeFailure()) {
        return query::TypedQueryResult<Value>::runtimeFailure(header.runtimeFailure());
      }
      if (header.kind() != query::QueryValueKind::Value ||
          !header.value()
               .storage()
               .is<binder::BinderQueryValue<binder::StableImplementationOccurrenceHeader>>()) {
        return contradiction();
      }
      const auto& candidate =
          header.value()
              .storage()
              .get<binder::BinderQueryValue<binder::StableImplementationOccurrenceHeader>>();
      if (candidate.diagnostics.values().size() != 0 || candidate.value.queryKey() != occurrence ||
          candidate.value.authority() != implementationOwner.implementation() ||
          candidate.value.record().encode().asPtr() != membership.record().encode().asPtr()) {
        return contradiction();
      }

      size_t matches = 0;
      for (const auto& parameter : candidate.value.genericParameters().values()) {
        auto parameterKey = binder::StableGenericParameterQueryKey::from(
            key.parameter().module().clone(), parameter.key().clone());
        if (parameterKey != key.parameter()) { continue; }
        ++matches;
        if (parameter.ordinal() != authority.value().ordinal() ||
            parameter.record().encode().asPtr() != authority.value().record().encode().asPtr() ||
            !parameter.site().value().is<binder::ImplementationOccurrenceSite>() ||
            !parameter.site().value().get<binder::ImplementationOccurrenceSite>().site.sameAs(
                occurrence.occurrence())) {
          return contradiction();
        }
        if (parameterName == zc::none) {
          parameterName = parameter.name().clone();
        } else if (ZC_ASSERT_NONNULL(parameterName) != parameter.name()) {
          return contradiction();
        }
      }
      if (matches != 1) { return contradiction(); }
    }
  }

  auto result = Value::active(valueDomain, authority.value().clone());
  if (result == zc::none) { return contradiction(); }
  return query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(result)));
}

bool ActiveGenericParameterMembershipQuery::verify(query::QueryContext& context, const Key& key,
                                                   const query::TypedQueryResult<Value>& result) {
  if (result.isRuntimeFailure() || result.kind() != query::QueryValueKind::Value) { return false; }
  auto authority = context.probeInput<ActiveGenericParameterAuthorityInput>(key);
  if (authority.isRuntimeFailure()) { return false; }
  if (authority.kind() != query::QueryValueKind::Value) {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    return readiness.kind() == query::QueryValueKind::Value &&
           CompleteRootIdentityReadinessVerifier::verify(key.contextRoots(), readiness.value()) &&
           resultMatches(valueDomain, result.value(), Value::inactive());
  }
  auto contradiction = [&]() {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    static_cast<void>(readiness);
    return false;
  };
  if (!ActiveGenericParameterAuthorityInputVerifier::verify(key, authority.value())) {
    return contradiction();
  }

  const auto& owner = authority.value().owner().value();
  if (owner.is<ActiveGenericDefinitionOwner>()) {
    const auto& definitionOwner = owner.get<ActiveGenericDefinitionOwner>();
    auto contextualDefinition = ContextualDefinitionKey::from(key.contextRoots().clone(),
                                                              definitionOwner.definition.clone());
    auto definition = context.get<ActiveDefinitionMembershipQuery>(contextualDefinition);
    if (definition.isRuntimeFailure()) { return false; }
    if (definition.kind() != query::QueryValueKind::Value || !definition.value().isActive()) {
      return contradiction();
    }
    auto header = context.get<binder::DefinitionHeaderSyntax>(definitionOwner.definition);
    if (header.isRuntimeFailure()) { return false; }
    if (header.kind() != query::QueryValueKind::Value ||
        !header.value().storage().is<binder::BinderQueryValue<binder::StableDefinitionHeader>>()) {
      return contradiction();
    }
    const auto& candidate =
        header.value().storage().get<binder::BinderQueryValue<binder::StableDefinitionHeader>>();
    if (candidate.diagnostics.values().size() != 0 ||
        candidate.value.queryKey() != definitionOwner.definition ||
        !candidate.value.authoritySite().sameAs(definitionOwner.headerSite) ||
        candidate.value.record().encode().asPtr() != definition.value().record().encode().asPtr()) {
      return contradiction();
    }
    size_t matches = 0;
    for (const auto& parameter : candidate.value.genericParameters().values()) {
      auto parameterKey = binder::StableGenericParameterQueryKey::from(
          key.parameter().module().clone(), parameter.key().clone());
      if (parameterKey != key.parameter()) { continue; }
      ++matches;
      if (parameter.ordinal() != authority.value().ordinal() ||
          parameter.record().encode().asPtr() != authority.value().record().encode().asPtr() ||
          !parameter.site().value().is<binder::DefinitionAuthoritySite>() ||
          !parameter.site().value().get<binder::DefinitionAuthoritySite>().site.sameAs(
              definitionOwner.headerSite)) {
        return contradiction();
      }
    }
    if (matches != 1) { return contradiction(); }
  } else {
    const auto& implementationOwner = owner.get<ActiveGenericImplementationOwner>().authority;
    auto contextualImplementation = ContextualImplementationKey::from(
        key.contextRoots().clone(), implementationOwner.implementation().clone());
    auto implementation =
        context.get<ActiveImplementationMembershipQuery>(contextualImplementation);
    if (implementation.isRuntimeFailure()) { return false; }
    if (implementation.kind() != query::QueryValueKind::Value ||
        !implementation.value().isActive()) {
      return contradiction();
    }
    const auto& membership = implementation.value().record();
    if (membership.queryKey() != implementationOwner.implementation() ||
        membership.authorityOccurrence() != implementationOwner.authorityOccurrence() ||
        membership.equalOccurrences().size() != implementationOwner.equalOccurrences().size()) {
      return contradiction();
    }
    for (size_t index = 0; index < membership.equalOccurrences().size(); ++index) {
      if (membership.equalOccurrences()[index] != implementationOwner.equalOccurrences()[index]) {
        return contradiction();
      }
    }
    zc::Maybe<identity::DeclaredDefinitionName> parameterName;
    for (const auto& occurrence : implementationOwner.equalOccurrences()) {
      auto header = context.get<binder::ImplementationOccurrenceHeaderSyntax>(occurrence);
      if (header.isRuntimeFailure()) { return false; }
      if (header.kind() != query::QueryValueKind::Value ||
          !header.value()
               .storage()
               .is<binder::BinderQueryValue<binder::StableImplementationOccurrenceHeader>>()) {
        return contradiction();
      }
      const auto& candidate =
          header.value()
              .storage()
              .get<binder::BinderQueryValue<binder::StableImplementationOccurrenceHeader>>();
      if (candidate.diagnostics.values().size() != 0 || candidate.value.queryKey() != occurrence ||
          candidate.value.authority() != implementationOwner.implementation() ||
          candidate.value.record().encode().asPtr() != membership.record().encode().asPtr()) {
        return contradiction();
      }
      size_t matches = 0;
      for (const auto& parameter : candidate.value.genericParameters().values()) {
        auto parameterKey = binder::StableGenericParameterQueryKey::from(
            key.parameter().module().clone(), parameter.key().clone());
        if (parameterKey != key.parameter()) { continue; }
        ++matches;
        if (parameter.ordinal() != authority.value().ordinal() ||
            parameter.record().encode().asPtr() != authority.value().record().encode().asPtr() ||
            !parameter.site().value().is<binder::ImplementationOccurrenceSite>() ||
            !parameter.site().value().get<binder::ImplementationOccurrenceSite>().site.sameAs(
                occurrence.occurrence())) {
          return contradiction();
        }
        if (parameterName == zc::none) {
          parameterName = parameter.name().clone();
        } else if (ZC_ASSERT_NONNULL(parameterName) != parameter.name()) {
          return contradiction();
        }
      }
      if (matches != 1) { return contradiction(); }
    }
  }

  auto expected = Value::active(valueDomain, authority.value().clone());
  return expected != zc::none &&
         resultMatches(valueDomain, result.value(), ZC_ASSERT_NONNULL(expected));
}

query::TypedQueryResult<ActiveCallableParameterMembershipQuery::Value>
ActiveCallableParameterMembershipQuery::provide(query::QueryContext& context, const Key& key) {
  auto authority = context.probeInput<ActiveCallableParameterAuthorityInput>(key);
  if (authority.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(authority.runtimeFailure());
  }
  if (authority.kind() != query::QueryValueKind::Value) {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    if (readiness.isRuntimeFailure()) {
      return query::TypedQueryResult<Value>::runtimeFailure(readiness.runtimeFailure());
    }
    if (readiness.kind() != query::QueryValueKind::Value ||
        !CompleteRootIdentityReadinessVerifier::verify(key.contextRoots(), readiness.value())) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    return query::TypedQueryResult<Value>::value(Value::inactive());
  }

  auto contradiction = [&]() -> query::TypedQueryResult<Value> {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    if (readiness.isRuntimeFailure()) {
      return query::TypedQueryResult<Value>::runtimeFailure(readiness.runtimeFailure());
    }
    if (readiness.kind() != query::QueryValueKind::Value ||
        !CompleteRootIdentityReadinessVerifier::verify(key.contextRoots(), readiness.value())) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  };

  if (!ActiveCallableParameterAuthorityInputVerifier::verify(key, authority.value())) {
    return contradiction();
  }
  auto contextualDefinition =
      ContextualDefinitionKey::from(key.contextRoots().clone(), authority.value().owner().clone());
  auto definition = context.get<ActiveDefinitionMembershipQuery>(contextualDefinition);
  if (definition.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(definition.runtimeFailure());
  }
  if (definition.kind() != query::QueryValueKind::Value || !definition.value().isActive()) {
    return contradiction();
  }

  auto header = context.get<binder::DefinitionHeaderSyntax>(authority.value().owner());
  if (header.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(header.runtimeFailure());
  }
  if (header.kind() != query::QueryValueKind::Value ||
      !header.value().storage().is<binder::BinderQueryValue<binder::StableDefinitionHeader>>()) {
    return contradiction();
  }
  const auto& candidate =
      header.value().storage().get<binder::BinderQueryValue<binder::StableDefinitionHeader>>();
  if (candidate.diagnostics.values().size() != 0 ||
      candidate.value.queryKey() != authority.value().owner() ||
      !candidate.value.authoritySite().sameAs(authority.value().headerSite()) ||
      candidate.value.record().encode().asPtr() != definition.value().record().encode().asPtr()) {
    return contradiction();
  }

  size_t ordinaryCount = 0;
  for (const auto& parameter : candidate.value.callableParameters().values()) {
    if (parameter.position().kind() == identity::CallableParameterPositionKind::Ordinary) {
      ++ordinaryCount;
    }
  }
  size_t matches = 0;
  for (const auto& parameter : candidate.value.callableParameters().values()) {
    auto parameterKey = binder::StableCallableParameterQueryKey::from(
        key.parameter().module().clone(), parameter.key().clone());
    if (parameterKey != key.parameter()) { continue; }
    ++matches;
    bool sameName = (parameter.name() == zc::none) == (authority.value().name() == zc::none);
    if (sameName && parameter.name() != zc::none) {
      sameName = ZC_ASSERT_NONNULL(parameter.name()) == ZC_ASSERT_NONNULL(authority.value().name());
    }
    const bool receiver =
        parameter.position().kind() == identity::CallableParameterPositionKind::Receiver;
    const auto ordinal = parameter.position().ordinal();
    const bool receiverLegal = (receiver && parameter.name() == zc::none && ordinal == zc::none) ||
                               (!receiver && parameter.name() != zc::none && ordinal != zc::none &&
                                ZC_ASSERT_NONNULL(ordinal) < ordinaryCount);
    if (parameter.record().encode().asPtr() != authority.value().record().encode().asPtr() ||
        !parameter.site().value().is<binder::DefinitionAuthoritySite>() ||
        !parameter.site().value().get<binder::DefinitionAuthoritySite>().site.sameAs(
            authority.value().headerSite()) ||
        !samePosition(parameter.position(), authority.value().position()) || !sameName ||
        receiverLegal != authority.value().receiverLegal()) {
      return contradiction();
    }
  }
  if (matches != 1) { return contradiction(); }

  auto result = Value::active(valueDomain, authority.value().clone());
  if (result == zc::none) { return contradiction(); }
  return query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(result)));
}

bool ActiveCallableParameterMembershipQuery::verify(query::QueryContext& context, const Key& key,
                                                    const query::TypedQueryResult<Value>& result) {
  if (result.isRuntimeFailure() || result.kind() != query::QueryValueKind::Value) { return false; }
  auto authority = context.probeInput<ActiveCallableParameterAuthorityInput>(key);
  if (authority.isRuntimeFailure()) { return false; }
  if (authority.kind() != query::QueryValueKind::Value) {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    return readiness.kind() == query::QueryValueKind::Value &&
           CompleteRootIdentityReadinessVerifier::verify(key.contextRoots(), readiness.value()) &&
           resultMatches(valueDomain, result.value(), Value::inactive());
  }
  auto contradiction = [&]() {
    auto readiness = context.probeInput<CompleteRootIdentityReadinessInput>(key.contextRoots());
    static_cast<void>(readiness);
    return false;
  };
  if (!ActiveCallableParameterAuthorityInputVerifier::verify(key, authority.value())) {
    return contradiction();
  }

  auto contextualDefinition =
      ContextualDefinitionKey::from(key.contextRoots().clone(), authority.value().owner().clone());
  auto definition = context.get<ActiveDefinitionMembershipQuery>(contextualDefinition);
  if (definition.isRuntimeFailure()) { return false; }
  if (definition.kind() != query::QueryValueKind::Value || !definition.value().isActive()) {
    return contradiction();
  }
  auto header = context.get<binder::DefinitionHeaderSyntax>(authority.value().owner());
  if (header.isRuntimeFailure()) { return false; }
  if (header.kind() != query::QueryValueKind::Value ||
      !header.value().storage().is<binder::BinderQueryValue<binder::StableDefinitionHeader>>()) {
    return contradiction();
  }
  const auto& candidate =
      header.value().storage().get<binder::BinderQueryValue<binder::StableDefinitionHeader>>();
  if (candidate.diagnostics.values().size() != 0 ||
      candidate.value.queryKey() != authority.value().owner() ||
      !candidate.value.authoritySite().sameAs(authority.value().headerSite()) ||
      candidate.value.record().encode().asPtr() != definition.value().record().encode().asPtr()) {
    return contradiction();
  }

  size_t ordinaryCount = 0;
  for (const auto& parameter : candidate.value.callableParameters().values()) {
    if (parameter.position().kind() == identity::CallableParameterPositionKind::Ordinary) {
      ++ordinaryCount;
    }
  }
  size_t matches = 0;
  for (const auto& parameter : candidate.value.callableParameters().values()) {
    auto parameterKey = binder::StableCallableParameterQueryKey::from(
        key.parameter().module().clone(), parameter.key().clone());
    if (parameterKey != key.parameter()) { continue; }
    ++matches;
    bool sameName = (parameter.name() == zc::none) == (authority.value().name() == zc::none);
    if (sameName && parameter.name() != zc::none) {
      sameName = ZC_ASSERT_NONNULL(parameter.name()) == ZC_ASSERT_NONNULL(authority.value().name());
    }
    const bool receiver =
        parameter.position().kind() == identity::CallableParameterPositionKind::Receiver;
    const auto ordinal = parameter.position().ordinal();
    const bool receiverLegal = (receiver && parameter.name() == zc::none && ordinal == zc::none) ||
                               (!receiver && parameter.name() != zc::none && ordinal != zc::none &&
                                ZC_ASSERT_NONNULL(ordinal) < ordinaryCount);
    if (parameter.record().encode().asPtr() != authority.value().record().encode().asPtr() ||
        !parameter.site().value().is<binder::DefinitionAuthoritySite>() ||
        !parameter.site().value().get<binder::DefinitionAuthoritySite>().site.sameAs(
            authority.value().headerSite()) ||
        !samePosition(parameter.position(), authority.value().position()) || !sameName ||
        receiverLegal != authority.value().receiverLegal()) {
      return contradiction();
    }
  }
  if (matches != 1) { return contradiction(); }

  auto expected = Value::active(valueDomain, authority.value().clone());
  return expected != zc::none &&
         resultMatches(valueDomain, result.value(), ZC_ASSERT_NONNULL(expected));
}

bool registerActiveIdentityMembershipQueries(query::QueryDatabase& database) {
  if (!database.registerDescriptor<ActiveDefinitionAuthorityInput>().isRegistered()) {
    return false;
  }
  if (!database.registerDescriptor<ActiveDefinitionAuthorityReadyInput>().isRegistered()) {
    return false;
  }
  if (!database.registerDescriptor<ActiveImplementationAuthorityInput>().isRegistered()) {
    return false;
  }
  if (!database.registerDescriptor<ActiveGenericParameterAuthorityInput>().isRegistered()) {
    return false;
  }
  if (!database.registerDescriptor<ActiveCallableParameterAuthorityInput>().isRegistered()) {
    return false;
  }
  if (!database.registerDescriptor<CompleteRootIdentityReadinessInput>().isRegistered()) {
    return false;
  }
  if (!database.registerDescriptor<ActiveCompilationUnitMembershipQuery>().isRegistered()) {
    return false;
  }
  if (!database.registerDescriptor<ActiveCrateMembershipQuery>().isRegistered()) { return false; }
  if (!database.registerDescriptor<ActiveSourceMembershipQuery>().isRegistered()) { return false; }
  if (!database.registerDescriptor<ActiveModuleMembershipQuery>().isRegistered()) { return false; }
  if (!database.registerDescriptor<ActiveDefinitionMembershipQuery>().isRegistered()) {
    return false;
  }
  if (!database.registerDescriptor<ActiveImplementationMembershipQuery>().isRegistered()) {
    return false;
  }
  if (!database.registerDescriptor<ActiveGenericParameterMembershipQuery>().isRegistered()) {
    return false;
  }
  return database.registerDescriptor<ActiveCallableParameterMembershipQuery>().isRegistered();
}

}  // namespace zomlang::compiler::driver::incremental_binding_query
