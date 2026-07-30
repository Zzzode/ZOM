// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/debug.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/driver/contextual-binding-key.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/source-key.h"
#include "zomlang/compiler/query/query-database.h"

namespace zomlang::compiler::driver::incremental_binding_query {

/// \brief Complete active crate subset for one compilation-unit identity.
class ActiveCompilationUnitMembership final {
public:
  ActiveCompilationUnitMembership(ActiveCompilationUnitMembership&&) noexcept = default;
  ActiveCompilationUnitMembership& operator=(ActiveCompilationUnitMembership&&) noexcept = default;
  ZC_DISALLOW_COPY(ActiveCompilationUnitMembership);

  ZC_NODISCARD static zc::Maybe<ActiveCompilationUnitMembership> from(
      identity::CompilationUnitIdentity&& unit, zc::Vector<identity::CrateKey>&& activeCrates);
  ZC_NODISCARD static zc::Maybe<ActiveCompilationUnitMembership> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD ActiveCompilationUnitMembership clone() const;
  ZC_NODISCARD const identity::CompilationUnitIdentity& unit() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::CrateKey> activeCrates() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const ActiveCompilationUnitMembership& other) const;

private:
  ActiveCompilationUnitMembership(identity::CompilationUnitIdentity&& unit,
                                  zc::Vector<identity::CrateKey>&& activeCrates) noexcept;

  identity::CompilationUnitIdentity unitField;
  zc::Vector<identity::CrateKey> activeCrateFields;
};

/// \brief Complete active authority and equal occurrences for one implementation.
class ActiveImplementationMembershipRecord final {
public:
  ActiveImplementationMembershipRecord(ActiveImplementationMembershipRecord&&) noexcept = default;
  ActiveImplementationMembershipRecord& operator=(ActiveImplementationMembershipRecord&&) noexcept =
      default;
  ZC_DISALLOW_COPY(ActiveImplementationMembershipRecord);

  ZC_NODISCARD static zc::Maybe<ActiveImplementationMembershipRecord> from(
      binder::StableImplementationQueryKey&& queryKey, identity::ImplIdentityRecord&& record,
      binder::StableImplementationOccurrenceQueryKey&& authorityOccurrence,
      zc::Vector<binder::StableImplementationOccurrenceQueryKey>&& equalOccurrences);
  ZC_NODISCARD static zc::Maybe<ActiveImplementationMembershipRecord> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD ActiveImplementationMembershipRecord clone() const;
  ZC_NODISCARD const binder::StableImplementationQueryKey& queryKey() const noexcept;
  ZC_NODISCARD const identity::ImplIdentityRecord& record() const noexcept;
  ZC_NODISCARD const binder::StableImplementationOccurrenceQueryKey& authorityOccurrence()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::StableImplementationOccurrenceQueryKey> equalOccurrences()
      const ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const ActiveImplementationMembershipRecord& other) const;

private:
  ActiveImplementationMembershipRecord(
      binder::StableImplementationQueryKey&& queryKey, identity::ImplIdentityRecord&& record,
      binder::StableImplementationOccurrenceQueryKey&& authorityOccurrence,
      zc::Vector<binder::StableImplementationOccurrenceQueryKey>&& equalOccurrences) noexcept;

  binder::StableImplementationQueryKey queryKeyField;
  identity::ImplIdentityRecord recordField;
  binder::StableImplementationOccurrenceQueryKey authorityOccurrenceField;
  zc::Vector<binder::StableImplementationOccurrenceQueryKey> equalOccurrenceFields;
};

/// \brief Complete implementation authority installed by the identity transaction.
struct ActiveImplementationAuthorityInput final {
  using Key = ContextualImplementationKey;
  using Value = ActiveImplementationMembershipRecord;

  static constexpr query::InputDescriptorMetadata descriptor{
      "ActiveImplementationAuthorityInput"_zcc, "zom.query.active-implementation-authority"_zcc,
      query::Durability::Low};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
};

/// \brief Structural verifier for one contextual implementation-authority input.
class ActiveImplementationAuthorityInputVerifier final {
public:
  ZC_NODISCARD static bool verify(const ContextualImplementationKey& key,
                                  const ActiveImplementationMembershipRecord& record);
};

/// \brief Complete implementation owner authority for shared generic parameters.
class ImplementationGenericAuthority final {
public:
  ImplementationGenericAuthority(ImplementationGenericAuthority&&) noexcept = default;
  ImplementationGenericAuthority& operator=(ImplementationGenericAuthority&&) noexcept = default;
  ZC_DISALLOW_COPY(ImplementationGenericAuthority);

  ZC_NODISCARD static zc::Maybe<ImplementationGenericAuthority> from(
      binder::StableImplementationQueryKey&& implementation,
      binder::StableImplementationOccurrenceQueryKey&& authorityOccurrence,
      zc::Vector<binder::StableImplementationOccurrenceQueryKey>&& equalOccurrences);
  ZC_NODISCARD static zc::Maybe<ImplementationGenericAuthority> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD ImplementationGenericAuthority clone() const;
  ZC_NODISCARD const binder::StableImplementationQueryKey& implementation() const noexcept;
  ZC_NODISCARD const binder::StableImplementationOccurrenceQueryKey& authorityOccurrence()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::StableImplementationOccurrenceQueryKey> equalOccurrences()
      const ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  ImplementationGenericAuthority(
      binder::StableImplementationQueryKey&& implementation,
      binder::StableImplementationOccurrenceQueryKey&& authorityOccurrence,
      zc::Vector<binder::StableImplementationOccurrenceQueryKey>&& equalOccurrences) noexcept;

  binder::StableImplementationQueryKey implementationField;
  binder::StableImplementationOccurrenceQueryKey authorityOccurrenceField;
  zc::Vector<binder::StableImplementationOccurrenceQueryKey> equalOccurrenceFields;
};

struct ActiveGenericDefinitionOwner final {
  binder::StableDefinitionQueryKey definition;
  binder::IdentitySyntaxSiteKey headerSite;
};

struct ActiveGenericImplementationOwner final {
  ImplementationGenericAuthority authority;
};

using ActiveGenericParameterOwnerValue =
    zc::OneOf<ActiveGenericDefinitionOwner, ActiveGenericImplementationOwner>;

/// \brief Closed definition-or-implementation generic owner authority.
class ActiveGenericParameterOwner final {
public:
  ActiveGenericParameterOwner(ActiveGenericParameterOwner&&) noexcept = default;
  ActiveGenericParameterOwner& operator=(ActiveGenericParameterOwner&&) noexcept = default;
  ZC_DISALLOW_COPY(ActiveGenericParameterOwner);

  ZC_NODISCARD static ActiveGenericParameterOwner definition(
      binder::StableDefinitionQueryKey&& definition, binder::IdentitySyntaxSiteKey&& headerSite);
  ZC_NODISCARD static ActiveGenericParameterOwner implementation(
      ImplementationGenericAuthority&& authority);
  ZC_NODISCARD ActiveGenericParameterOwner clone() const;
  ZC_NODISCARD const ActiveGenericParameterOwnerValue& value() const noexcept;

private:
  explicit ActiveGenericParameterOwner(ActiveGenericDefinitionOwner&& owner) noexcept;
  explicit ActiveGenericParameterOwner(ActiveGenericImplementationOwner&& owner) noexcept;
  ActiveGenericParameterOwnerValue valueField;
};

/// \brief Complete generic-parameter identity and owner authority.
class ActiveGenericParameterMembership final {
public:
  ActiveGenericParameterMembership(ActiveGenericParameterMembership&&) noexcept = default;
  ActiveGenericParameterMembership& operator=(ActiveGenericParameterMembership&&) noexcept =
      default;
  ZC_DISALLOW_COPY(ActiveGenericParameterMembership);

  ZC_NODISCARD static zc::Maybe<ActiveGenericParameterMembership> from(
      binder::StableGenericParameterQueryKey&& queryKey,
      identity::GenericParameterIdentityRecord&& record, ActiveGenericParameterOwner&& owner,
      uint32_t ordinal);
  ZC_NODISCARD static zc::Maybe<ActiveGenericParameterMembership> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD ActiveGenericParameterMembership clone() const;
  ZC_NODISCARD const binder::StableGenericParameterQueryKey& queryKey() const noexcept;
  ZC_NODISCARD const identity::GenericParameterIdentityRecord& record() const noexcept;
  ZC_NODISCARD const ActiveGenericParameterOwner& owner() const noexcept;
  ZC_NODISCARD uint32_t ordinal() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  ActiveGenericParameterMembership(binder::StableGenericParameterQueryKey&& queryKey,
                                   identity::GenericParameterIdentityRecord&& record,
                                   ActiveGenericParameterOwner&& owner, uint32_t ordinal) noexcept;

  binder::StableGenericParameterQueryKey queryKeyField;
  identity::GenericParameterIdentityRecord recordField;
  ActiveGenericParameterOwner ownerField;
  uint32_t ordinalField;
};

/// \brief Complete generic-parameter authority installed by the identity transaction.
struct ActiveGenericParameterAuthorityInput final {
  using Key = ContextualGenericParameterKey;
  using Value = ActiveGenericParameterMembership;

  static constexpr query::InputDescriptorMetadata descriptor{
      "ActiveGenericParameterAuthorityInput"_zcc,
      "zom.query.active-generic-parameter-authority"_zcc, query::Durability::Low};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
};

/// \brief Structural verifier for one contextual generic-parameter authority.
class ActiveGenericParameterAuthorityInputVerifier final {
public:
  ZC_NODISCARD static bool verify(const ContextualGenericParameterKey& key,
                                  const ActiveGenericParameterMembership& record);
};

/// \brief Complete callable-parameter identity and definition-header authority.
class ActiveCallableParameterMembershipRecord final {
public:
  ActiveCallableParameterMembershipRecord(ActiveCallableParameterMembershipRecord&&) noexcept =
      default;
  ActiveCallableParameterMembershipRecord& operator=(
      ActiveCallableParameterMembershipRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(ActiveCallableParameterMembershipRecord);

  ZC_NODISCARD static zc::Maybe<ActiveCallableParameterMembershipRecord> from(
      binder::StableCallableParameterQueryKey&& queryKey,
      identity::CallableParameterIdentityRecord&& record, binder::StableDefinitionQueryKey&& owner,
      binder::IdentitySyntaxSiteKey&& headerSite, identity::CallableParameterPosition position,
      zc::Maybe<identity::DeclaredDefinitionName>&& name, bool receiverLegal);
  ZC_NODISCARD static zc::Maybe<ActiveCallableParameterMembershipRecord> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD ActiveCallableParameterMembershipRecord clone() const;
  ZC_NODISCARD const binder::StableCallableParameterQueryKey& queryKey() const noexcept;
  ZC_NODISCARD const identity::CallableParameterIdentityRecord& record() const noexcept;
  ZC_NODISCARD const binder::StableDefinitionQueryKey& owner() const noexcept;
  ZC_NODISCARD const binder::IdentitySyntaxSiteKey& headerSite() const noexcept;
  ZC_NODISCARD identity::CallableParameterPosition position() const noexcept;
  ZC_NODISCARD const zc::Maybe<identity::DeclaredDefinitionName>& name() const noexcept;
  ZC_NODISCARD bool receiverLegal() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  ActiveCallableParameterMembershipRecord(binder::StableCallableParameterQueryKey&& queryKey,
                                          identity::CallableParameterIdentityRecord&& record,
                                          binder::StableDefinitionQueryKey&& owner,
                                          binder::IdentitySyntaxSiteKey&& headerSite,
                                          identity::CallableParameterPosition position,
                                          zc::Maybe<identity::DeclaredDefinitionName>&& name,
                                          bool receiverLegal) noexcept;

  binder::StableCallableParameterQueryKey queryKeyField;
  identity::CallableParameterIdentityRecord recordField;
  binder::StableDefinitionQueryKey ownerField;
  binder::IdentitySyntaxSiteKey headerSiteField;
  identity::CallableParameterPosition positionField;
  zc::Maybe<identity::DeclaredDefinitionName> nameField;
  bool receiverLegalField;
};

/// \brief Complete callable-parameter authority installed by the identity transaction.
struct ActiveCallableParameterAuthorityInput final {
  using Key = ContextualCallableParameterKey;
  using Value = ActiveCallableParameterMembershipRecord;

  static constexpr query::InputDescriptorMetadata descriptor{
      "ActiveCallableParameterAuthorityInput"_zcc,
      "zom.query.active-callable-parameter-authority"_zcc, query::Durability::Low};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
};

/// \brief Structural verifier for one contextual callable-parameter authority.
class ActiveCallableParameterAuthorityInputVerifier final {
public:
  ZC_NODISCARD static bool verify(const ContextualCallableParameterKey& key,
                                  const ActiveCallableParameterMembershipRecord& record);
};

template <typename Record>
struct ActiveMembershipRecordCodec;

template <>
struct ActiveMembershipRecordCodec<ActiveCompilationUnitMembership> final {
  ZC_NODISCARD static zc::Array<uint8_t> encode(const ActiveCompilationUnitMembership& record);
  ZC_NODISCARD static zc::Maybe<ActiveCompilationUnitMembership> decode(
      zc::ArrayPtr<const uint8_t> bytes);
};

template <>
struct ActiveMembershipRecordCodec<identity::CrateKey> final {
  ZC_NODISCARD static zc::Array<uint8_t> encode(const identity::CrateKey& record);
  ZC_NODISCARD static zc::Maybe<identity::CrateKey> decode(zc::ArrayPtr<const uint8_t> bytes);
};

template <>
struct ActiveMembershipRecordCodec<identity::SourceFileKey> final {
  ZC_NODISCARD static zc::Array<uint8_t> encode(const identity::SourceFileKey& record);
  ZC_NODISCARD static zc::Maybe<identity::SourceFileKey> decode(zc::ArrayPtr<const uint8_t> bytes);
};

template <>
struct ActiveMembershipRecordCodec<identity::ModuleKey> final {
  ZC_NODISCARD static zc::Array<uint8_t> encode(const identity::ModuleKey& record);
  ZC_NODISCARD static zc::Maybe<identity::ModuleKey> decode(zc::ArrayPtr<const uint8_t> bytes);
};

template <>
struct ActiveMembershipRecordCodec<identity::DefinitionIdentityRecord> final {
  ZC_NODISCARD static zc::Array<uint8_t> encode(const identity::DefinitionIdentityRecord& record);
  ZC_NODISCARD static zc::Maybe<identity::DefinitionIdentityRecord> decode(
      zc::ArrayPtr<const uint8_t> bytes);
};

template <>
struct ActiveMembershipRecordCodec<ActiveImplementationMembershipRecord> final {
  ZC_NODISCARD static zc::Array<uint8_t> encode(const ActiveImplementationMembershipRecord& record);
  ZC_NODISCARD static zc::Maybe<ActiveImplementationMembershipRecord> decode(
      zc::ArrayPtr<const uint8_t> bytes);
};

template <>
struct ActiveMembershipRecordCodec<ActiveGenericParameterMembership> final {
  ZC_NODISCARD static zc::Array<uint8_t> encode(const ActiveGenericParameterMembership& record);
  ZC_NODISCARD static zc::Maybe<ActiveGenericParameterMembership> decode(
      zc::ArrayPtr<const uint8_t> bytes);
};

template <>
struct ActiveMembershipRecordCodec<ActiveCallableParameterMembershipRecord> final {
  ZC_NODISCARD static zc::Array<uint8_t> encode(
      const ActiveCallableParameterMembershipRecord& record);
  ZC_NODISCARD static zc::Maybe<ActiveCallableParameterMembershipRecord> decode(
      zc::ArrayPtr<const uint8_t> bytes);
};

/// \brief Exact active authority record or deterministic semantic inactivity.
template <typename Record>
class ActiveMembershipResult final {
public:
  ActiveMembershipResult(ActiveMembershipResult&&) noexcept = default;
  ActiveMembershipResult& operator=(ActiveMembershipResult&&) noexcept = default;
  ZC_DISALLOW_COPY(ActiveMembershipResult);

  ZC_NODISCARD static zc::Maybe<ActiveMembershipResult> active(zc::StringPtr domain,
                                                               Record&& record) {
    ActiveMembershipResult result(zc::Maybe<Record>(zc::mv(record)));
    if (result.encodeCanonical(domain).size() > 128 * 1024 * 1024) { return zc::none; }
    return zc::mv(result);
  }
  ZC_NODISCARD static ActiveMembershipResult inactive() { return ActiveMembershipResult(zc::none); }
  ZC_NODISCARD static zc::Maybe<ActiveMembershipResult> decodeCanonical(
      zc::StringPtr domain, zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD ActiveMembershipResult clone() const {
    if (!isActive()) { return inactive(); }
    return ActiveMembershipResult(zc::Maybe<Record>(record().clone()));
  }
  ZC_NODISCARD bool isActive() const noexcept { return recordField != zc::none; }
  ZC_NODISCARD const Record& record() const { return ZC_ASSERT_NONNULL(recordField); }
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical(zc::StringPtr domain) const;

private:
  explicit ActiveMembershipResult(zc::Maybe<Record>&& record) noexcept
      : recordField(zc::mv(record)) {}

  zc::Maybe<Record> recordField;
};

template <typename Record>
zc::Maybe<ActiveMembershipResult<Record>> ActiveMembershipResult<Record>::decodeCanonical(
    zc::StringPtr domain, zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() > 128 * 1024 * 1024 || bytes.size() <= domain.size() ||
      bytes.first(domain.size()) != domain.asBytes() || bytes[domain.size()] != 0) {
    return zc::none;
  }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1));
  auto tag = decoder.decodeUint8();
  if (tag == zc::none) { return zc::none; }
  if (ZC_ASSERT_NONNULL(tag) == 0x02) {
    if (!decoder.finished()) { return zc::none; }
    return inactive();
  }
  if (ZC_ASSERT_NONNULL(tag) != 0x01) { return zc::none; }
  auto recordBytes = decoder.decodeByteString(128 * 1024 * 1024);
  if (recordBytes == zc::none || !decoder.finished()) { return zc::none; }
  auto record = ActiveMembershipRecordCodec<Record>::decode(ZC_ASSERT_NONNULL(recordBytes).asPtr());
  if (record == zc::none) { return zc::none; }
  return active(domain, zc::mv(ZC_ASSERT_NONNULL(record)));
}

template <typename Record>
zc::Array<uint8_t> ActiveMembershipResult<Record>::encodeCanonical(zc::StringPtr domain) const {
  identity::CanonicalEncoder encoder;
  encoder.encodeUint8(isActive() ? 0x01 : 0x02);
  if (isActive()) {
    auto recordBytes = ActiveMembershipRecordCodec<Record>::encode(record());
    encoder.encodeByteString(recordBytes.asPtr());
  }
  auto tail = encoder.finish();
  zc::Vector<uint8_t> result(domain.size() + 1 + tail.size());
  result.addAll(domain.asBytes());
  result.add(0);
  result.addAll(tail.asPtr());
  return result.releaseAsArray();
}

#define ZOM_DECLARE_ACTIVE_MEMBERSHIP_SURFACE(KeyType, GlobalKeyType, RecordType, ValueDomain) \
  using Key = KeyType;                                                                         \
  using GlobalKey = GlobalKeyType;                                                             \
  using Record = RecordType;                                                                   \
  using Value = ActiveMembershipResult<Record>;                                                \
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);                            \
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);             \
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);                      \
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);         \
  ZC_NODISCARD static zc::Maybe<GlobalKey> projectGlobalKey(const Key& key);                   \
  ZC_NODISCARD static bool sameAuthority(const Record& left, const Record& right);             \
  ZC_NODISCARD static bool validateAuthority(const Key& key, const GlobalKey& globalKey,       \
                                             const Record& record);                            \
  ZC_NODISCARD static query::TypedQueryResult<Value> provide(query::QueryContext& context,     \
                                                             const Key& key);                  \
  ZC_NODISCARD static bool verify(query::QueryContext& context, const Key& key,                \
                                  const query::TypedQueryResult<Value>& result);               \
  static constexpr zc::StringPtr valueDomain = ValueDomain

/// \brief Resolves contextual compilation-unit membership.
struct ActiveCompilationUnitMembershipQuery final {
  static constexpr query::SemanticDescriptorMetadata descriptor{
      "ActiveCompilationUnitMembershipQuery"_zcc,
      "zom.query.active-compilation-unit-membership"_zcc,
      query::ReuseClass::Semantic,
      query::RetentionClass::Retained,
      query::QueryEqualityPolicy::CanonicalBytes,
      query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear};
  ZOM_DECLARE_ACTIVE_MEMBERSHIP_SURFACE(ContextualCompilationUnitKey,
                                        identity::CompilationUnitIdentity,
                                        ActiveCompilationUnitMembership,
                                        "zom.query.active-compilation-unit-membership-value"_zc);
};

/// \brief Resolves contextual crate membership.
struct ActiveCrateMembershipQuery final {
  static constexpr query::SemanticDescriptorMetadata descriptor{
      "ActiveCrateMembershipQuery"_zcc,
      "zom.query.active-crate-membership"_zcc,
      query::ReuseClass::Semantic,
      query::RetentionClass::Retained,
      query::QueryEqualityPolicy::CanonicalBytes,
      query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear};
  ZOM_DECLARE_ACTIVE_MEMBERSHIP_SURFACE(ContextualCrateKey, identity::CrateKey, identity::CrateKey,
                                        "zom.query.active-crate-membership-value"_zc);
};

/// \brief Resolves contextual source membership.
struct ActiveSourceMembershipQuery final {
  static constexpr query::SemanticDescriptorMetadata descriptor{
      "ActiveSourceMembershipQuery"_zcc,
      "zom.query.active-source-membership"_zcc,
      query::ReuseClass::Semantic,
      query::RetentionClass::Retained,
      query::QueryEqualityPolicy::CanonicalBytes,
      query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear};
  ZOM_DECLARE_ACTIVE_MEMBERSHIP_SURFACE(ContextualSourceKey, identity::SourceFileKey,
                                        identity::SourceFileKey,
                                        "zom.query.active-source-membership-value"_zc);
};

/// \brief Resolves contextual module membership.
struct ActiveModuleMembershipQuery final {
  static constexpr query::SemanticDescriptorMetadata descriptor{
      "ActiveModuleMembershipQuery"_zcc,
      "zom.query.active-module-membership"_zcc,
      query::ReuseClass::Semantic,
      query::RetentionClass::Retained,
      query::QueryEqualityPolicy::CanonicalBytes,
      query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear};
  ZOM_DECLARE_ACTIVE_MEMBERSHIP_SURFACE(ContextualModuleKey, identity::ModuleKey,
                                        identity::ModuleKey,
                                        "zom.query.active-module-membership-value"_zc);
};

/// \brief Resolves contextual definition membership.
struct ActiveDefinitionMembershipQuery final {
  static constexpr query::SemanticDescriptorMetadata descriptor{
      "ActiveDefinitionMembershipQuery"_zcc,
      "zom.query.active-definition-membership"_zcc,
      query::ReuseClass::Semantic,
      query::RetentionClass::Retained,
      query::QueryEqualityPolicy::CanonicalBytes,
      query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear};
  ZOM_DECLARE_ACTIVE_MEMBERSHIP_SURFACE(ContextualDefinitionKey, identity::DefinitionKey,
                                        identity::DefinitionIdentityRecord,
                                        "zom.query.active-definition-membership-value"_zc);
};

/// \brief Resolves contextual implementation membership.
struct ActiveImplementationMembershipQuery final {
  static constexpr query::SemanticDescriptorMetadata descriptor{
      "ActiveImplementationMembershipQuery"_zcc,
      "zom.query.active-implementation-membership"_zcc,
      query::ReuseClass::Semantic,
      query::RetentionClass::Retained,
      query::QueryEqualityPolicy::CanonicalBytes,
      query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear};
  ZOM_DECLARE_ACTIVE_MEMBERSHIP_SURFACE(ContextualImplementationKey, identity::ImplKey,
                                        ActiveImplementationMembershipRecord,
                                        "zom.query.active-implementation-membership-value"_zc);
};

/// \brief Resolves contextual generic-parameter membership.
struct ActiveGenericParameterMembershipQuery final {
  static constexpr query::SemanticDescriptorMetadata descriptor{
      "ActiveGenericParameterMembershipQuery"_zcc,
      "zom.query.active-generic-parameter-membership"_zcc,
      query::ReuseClass::Semantic,
      query::RetentionClass::Retained,
      query::QueryEqualityPolicy::CanonicalBytes,
      query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear};
  ZOM_DECLARE_ACTIVE_MEMBERSHIP_SURFACE(ContextualGenericParameterKey,
                                        identity::GenericParameterKey,
                                        ActiveGenericParameterMembership,
                                        "zom.query.active-generic-parameter-membership-value"_zc);
};

/// \brief Resolves contextual callable-parameter membership.
struct ActiveCallableParameterMembershipQuery final {
  static constexpr query::SemanticDescriptorMetadata descriptor{
      "ActiveCallableParameterMembershipQuery"_zcc,
      "zom.query.active-callable-parameter-membership"_zcc,
      query::ReuseClass::Semantic,
      query::RetentionClass::Retained,
      query::QueryEqualityPolicy::CanonicalBytes,
      query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear};
  ZOM_DECLARE_ACTIVE_MEMBERSHIP_SURFACE(ContextualCallableParameterKey,
                                        identity::CallableParameterKey,
                                        ActiveCallableParameterMembershipRecord,
                                        "zom.query.active-callable-parameter-membership-value"_zc);
};

#undef ZOM_DECLARE_ACTIVE_MEMBERSHIP_SURFACE

/// \brief Registers the complete contextual authority and active-membership surface.
ZC_NODISCARD bool registerActiveIdentityMembershipQueries(query::QueryDatabase& database);

}  // namespace zomlang::compiler::driver::incremental_binding_query
