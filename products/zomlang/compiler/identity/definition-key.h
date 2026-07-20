// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/identity/canonical-impl-header.h"
#include "zomlang/compiler/identity/overload-header-digest.h"
#include "zomlang/compiler/identity/source-key.h"

namespace zomlang::compiler::identity {

class CanonicalEncoder;
class DefinitionIdentityRecord;
class ImplIdentityRecord;

enum class DefinitionKind : uint8_t {
  ModuleAlias = 0x01,
  Function = 0x02,
  Method = 0x03,
  Constructor = 0x04,
  Destructor = 0x05,
  Class = 0x06,
  Struct = 0x07,
  Interface = 0x08,
  Enum = 0x09,
  Error = 0x0a,
  TypeAlias = 0x0b,
  AssociatedType = 0x0c,
  Field = 0x0d,
  EnumVariant = 0x0e,
  Parameter = 0x0f,
  TypeParameter = 0x10,
  Constant = 0x11,
  Static = 0x12,
  Local = 0x13,
  PatternBinding = 0x14,
  Closure = 0x15
};

enum class DefinitionNamespace : uint8_t { Value = 0x01, Type = 0x02, Module = 0x03 };

ZC_NODISCARD bool isDefinitionKindValue(DefinitionKind value) noexcept;
ZC_NODISCARD bool isStableDefinitionKind(DefinitionKind value) noexcept;
ZC_NODISCARD zc::Maybe<DefinitionNamespace> definitionNamespaceFor(DefinitionKind value) noexcept;

/// \brief Raw RFC 0018 digest key for one complete named-item identity record.
class DefinitionKey final {
public:
  DefinitionKey(DefinitionKey&&) noexcept = default;
  DefinitionKey& operator=(DefinitionKey&&) noexcept = default;
  ZC_DISALLOW_COPY(DefinitionKey);

  /// \brief Computes SHA-256("zom.named-item-header.v0" || 0x00 || Encode(record)).
  ZC_NODISCARD static DefinitionKey compute(const DefinitionIdentityRecord& record);
  /// \brief Admits exactly 32 already-verified digest bytes.
  ZC_NODISCARD static zc::Maybe<DefinitionKey> fromBytes(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD DefinitionKey clone() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> bytes() const ZC_LIFETIMEBOUND;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;
  bool operator==(const DefinitionKey& other) const noexcept;
  bool operator!=(const DefinitionKey& other) const noexcept { return !(*this == other); }

private:
  explicit DefinitionKey(const Sha256Digest& digest) noexcept;

  Sha256Digest digestValue;
};

/// \brief Raw RFC 0018 digest key for one complete implementation identity record.
class ImplKey final {
public:
  ImplKey(ImplKey&&) noexcept = default;
  ImplKey& operator=(ImplKey&&) noexcept = default;
  ZC_DISALLOW_COPY(ImplKey);

  /// \brief Computes SHA-256("zom.impl-header.v0" || 0x00 || Encode(record)).
  ZC_NODISCARD static ImplKey compute(const ImplIdentityRecord& record);
  /// \brief Admits exactly 32 already-verified digest bytes.
  ZC_NODISCARD static zc::Maybe<ImplKey> fromBytes(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD ImplKey clone() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> bytes() const ZC_LIFETIMEBOUND;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;
  bool operator==(const ImplKey& other) const noexcept;
  bool operator!=(const ImplKey& other) const noexcept { return !(*this == other); }

private:
  explicit ImplKey(const Sha256Digest& digest) noexcept;

  Sha256Digest digestValue;
};

struct StableDefinitionOwner final {
  DefinitionKey key;
};

struct StableImplementationOwner final {
  ImplKey key;
};

enum class EnclosingStableOwnerKind : uint8_t { Definition = 0x01, Implementation = 0x02 };

/// \brief One raw-digest lexical owner in an outermost-first stable owner chain.
class EnclosingStableOwnerKey final {
public:
  EnclosingStableOwnerKey(EnclosingStableOwnerKey&&) noexcept = default;
  EnclosingStableOwnerKey& operator=(EnclosingStableOwnerKey&&) noexcept = default;
  ZC_DISALLOW_COPY(EnclosingStableOwnerKey);

  ZC_NODISCARD static EnclosingStableOwnerKey definition(DefinitionKey&& key);
  ZC_NODISCARD static EnclosingStableOwnerKey implementation(ImplKey&& key);
  ZC_NODISCARD EnclosingStableOwnerKey clone() const;
  ZC_NODISCARD EnclosingStableOwnerKind kind() const noexcept;
  ZC_NODISCARD zc::Maybe<const DefinitionKey&> definitionKey() const noexcept;
  ZC_NODISCARD zc::Maybe<const ImplKey&> implKey() const noexcept;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit EnclosingStableOwnerKey(StableDefinitionOwner&& owner) noexcept;
  explicit EnclosingStableOwnerKey(StableImplementationOwner&& owner) noexcept;

  zc::OneOf<StableDefinitionOwner, StableImplementationOwner> value;
};

namespace definition_identity_detail {
struct DefinitionIdentityRecordData;
struct ImplIdentityRecordData;
struct DefinitionIdentityAuthorityData;
struct ImplIdentityAuthorityData;
struct GenericParameterIdentityRecordData;
struct GenericParameterAuthorityData;
struct CallableParameterIdentityRecordData;
struct CallableParameterAuthorityData;
}  // namespace definition_identity_detail

/// \brief Complete equality authority for one stable named definition.
class DefinitionIdentityRecord final {
public:
  ~DefinitionIdentityRecord() noexcept(false);
  DefinitionIdentityRecord(DefinitionIdentityRecord&&) noexcept;
  DefinitionIdentityRecord& operator=(DefinitionIdentityRecord&&) noexcept;
  ZC_DISALLOW_COPY(DefinitionIdentityRecord);

  /// \brief Admits only the RFC 0018 stable kind/namespace and overload-presence mapping.
  ZC_NODISCARD static zc::Maybe<DefinitionIdentityRecord> from(
      ModuleKey&& module, zc::Vector<EnclosingStableOwnerKey>&& owners, DefinitionKind kind,
      DefinitionNamespace nameSpace, DeclaredDefinitionName&& name,
      zc::Maybe<OverloadHeaderDigest>&& overloadHeader);
  ZC_NODISCARD DefinitionIdentityRecord clone() const;
  ZC_NODISCARD const ModuleKey& module() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const EnclosingStableOwnerKey> owners() const noexcept;
  ZC_NODISCARD DefinitionKind kind() const noexcept;
  ZC_NODISCARD DefinitionNamespace nameSpace() const noexcept;
  ZC_NODISCARD zc::StringPtr name() const noexcept;
  ZC_NODISCARD zc::Maybe<const OverloadHeaderDigest&> overloadHeader() const noexcept;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit DefinitionIdentityRecord(
      zc::Own<definition_identity_detail::DefinitionIdentityRecordData>&& impl) noexcept;

  zc::Own<definition_identity_detail::DefinitionIdentityRecordData> impl;
};

/// \brief Complete equality authority for one stable implementation identity.
class ImplIdentityRecord final {
public:
  ~ImplIdentityRecord() noexcept(false);
  ImplIdentityRecord(ImplIdentityRecord&&) noexcept;
  ImplIdentityRecord& operator=(ImplIdentityRecord&&) noexcept;
  ZC_DISALLOW_COPY(ImplIdentityRecord);

  ZC_NODISCARD static ImplIdentityRecord from(ModuleKey&& module,
                                              zc::Vector<EnclosingStableOwnerKey>&& owners,
                                              CanonicalImplHeader&& header);
  ZC_NODISCARD ImplIdentityRecord clone() const;
  ZC_NODISCARD const ModuleKey& module() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const EnclosingStableOwnerKey> owners() const noexcept;
  ZC_NODISCARD const CanonicalImplHeader& header() const noexcept;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit ImplIdentityRecord(
      zc::Own<definition_identity_detail::ImplIdentityRecordData>&& impl) noexcept;

  zc::Own<definition_identity_detail::ImplIdentityRecordData> impl;
};

/// \brief Retained digest and complete record authority for one named definition.
class DefinitionIdentityAuthority final {
public:
  ~DefinitionIdentityAuthority() noexcept(false);
  DefinitionIdentityAuthority(DefinitionIdentityAuthority&&) noexcept;
  DefinitionIdentityAuthority& operator=(DefinitionIdentityAuthority&&) noexcept;
  ZC_DISALLOW_COPY(DefinitionIdentityAuthority);

  /// \brief Retains and verifies the complete overload header required by callable records.
  ZC_NODISCARD static zc::Maybe<DefinitionIdentityAuthority> from(
      DefinitionIdentityRecord&& record,
      zc::Maybe<OverloadHeaderAuthority>&& overloadHeaderAuthority);
  ZC_NODISCARD DefinitionIdentityAuthority clone() const;
  ZC_NODISCARD const DefinitionKey& key() const noexcept;
  ZC_NODISCARD const DefinitionIdentityRecord& record() const noexcept;
  ZC_NODISCARD zc::Maybe<const OverloadHeaderAuthority&> overloadHeaderAuthority() const noexcept;
  ZC_NODISCARD bool verify() const;
  ZC_NODISCARD bool sameRecordAs(const DefinitionIdentityAuthority& other) const;

private:
  explicit DefinitionIdentityAuthority(
      zc::Own<definition_identity_detail::DefinitionIdentityAuthorityData>&& impl) noexcept;

  zc::Own<definition_identity_detail::DefinitionIdentityAuthorityData> impl;
};

/// \brief Retained digest and complete record authority for one implementation.
class ImplIdentityAuthority final {
public:
  ~ImplIdentityAuthority() noexcept(false);
  ImplIdentityAuthority(ImplIdentityAuthority&&) noexcept;
  ImplIdentityAuthority& operator=(ImplIdentityAuthority&&) noexcept;
  ZC_DISALLOW_COPY(ImplIdentityAuthority);

  ZC_NODISCARD static ImplIdentityAuthority from(ImplIdentityRecord&& record);
  ZC_NODISCARD ImplIdentityAuthority clone() const;
  ZC_NODISCARD const ImplKey& key() const noexcept;
  ZC_NODISCARD const ImplIdentityRecord& record() const noexcept;
  ZC_NODISCARD bool verify() const;
  ZC_NODISCARD bool sameRecordAs(const ImplIdentityAuthority& other) const;

private:
  explicit ImplIdentityAuthority(
      zc::Own<definition_identity_detail::ImplIdentityAuthorityData>&& impl) noexcept;

  zc::Own<definition_identity_detail::ImplIdentityAuthorityData> impl;
};

using StableGenericParameterOwnerKind = EnclosingStableOwnerKind;

/// \brief Stable definition-or-implementation owner of a generic parameter.
class StableGenericParameterOwnerKey final {
public:
  StableGenericParameterOwnerKey(StableGenericParameterOwnerKey&&) noexcept = default;
  StableGenericParameterOwnerKey& operator=(StableGenericParameterOwnerKey&&) noexcept = default;
  ZC_DISALLOW_COPY(StableGenericParameterOwnerKey);

  ZC_NODISCARD static StableGenericParameterOwnerKey definition(DefinitionKey&& key);
  ZC_NODISCARD static StableGenericParameterOwnerKey implementation(ImplKey&& key);
  ZC_NODISCARD StableGenericParameterOwnerKey clone() const;
  ZC_NODISCARD StableGenericParameterOwnerKind kind() const noexcept;
  ZC_NODISCARD zc::Maybe<const DefinitionKey&> definitionKey() const noexcept;
  ZC_NODISCARD zc::Maybe<const ImplKey&> implKey() const noexcept;
  void encode(CanonicalEncoder& encoder) const;

private:
  explicit StableGenericParameterOwnerKey(EnclosingStableOwnerKey&& owner) noexcept;

  EnclosingStableOwnerKey owner;
};

enum class GenericParameterKind : uint8_t { Type = 0x01 };

class GenericParameterIdentityRecord;

/// \brief Raw RFC 0018 digest key for one stable subordinate generic parameter.
class GenericParameterKey final {
public:
  GenericParameterKey(GenericParameterKey&&) noexcept = default;
  GenericParameterKey& operator=(GenericParameterKey&&) noexcept = default;
  ZC_DISALLOW_COPY(GenericParameterKey);

  ZC_NODISCARD static GenericParameterKey compute(const GenericParameterIdentityRecord& record);
  ZC_NODISCARD static zc::Maybe<GenericParameterKey> fromBytes(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD GenericParameterKey clone() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> bytes() const ZC_LIFETIMEBOUND;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;
  bool operator==(const GenericParameterKey& other) const noexcept;
  bool operator!=(const GenericParameterKey& other) const noexcept { return !(*this == other); }

private:
  explicit GenericParameterKey(const Sha256Digest& digest) noexcept;

  Sha256Digest digestValue;
};

class GenericParameterIdentityRecord final {
public:
  ~GenericParameterIdentityRecord() noexcept(false);
  GenericParameterIdentityRecord(GenericParameterIdentityRecord&&) noexcept;
  GenericParameterIdentityRecord& operator=(GenericParameterIdentityRecord&&) noexcept;
  ZC_DISALLOW_COPY(GenericParameterIdentityRecord);

  ZC_NODISCARD static GenericParameterIdentityRecord type(StableGenericParameterOwnerKey&& owner,
                                                          uint32_t ordinal);
  ZC_NODISCARD GenericParameterIdentityRecord clone() const;
  ZC_NODISCARD const StableGenericParameterOwnerKey& owner() const noexcept;
  ZC_NODISCARD GenericParameterKind kind() const noexcept;
  ZC_NODISCARD uint32_t ordinal() const noexcept;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit GenericParameterIdentityRecord(
      zc::Own<definition_identity_detail::GenericParameterIdentityRecordData>&& impl) noexcept;

  zc::Own<definition_identity_detail::GenericParameterIdentityRecordData> impl;
};

class GenericParameterAuthority final {
public:
  ~GenericParameterAuthority() noexcept(false);
  GenericParameterAuthority(GenericParameterAuthority&&) noexcept;
  GenericParameterAuthority& operator=(GenericParameterAuthority&&) noexcept;
  ZC_DISALLOW_COPY(GenericParameterAuthority);

  ZC_NODISCARD static GenericParameterAuthority from(GenericParameterIdentityRecord&& record);
  ZC_NODISCARD GenericParameterAuthority clone() const;
  ZC_NODISCARD const GenericParameterKey& key() const noexcept;
  ZC_NODISCARD const GenericParameterIdentityRecord& record() const noexcept;
  ZC_NODISCARD bool verify() const;
  ZC_NODISCARD bool sameRecordAs(const GenericParameterAuthority& other) const;

private:
  explicit GenericParameterAuthority(
      zc::Own<definition_identity_detail::GenericParameterAuthorityData>&& impl) noexcept;

  zc::Own<definition_identity_detail::GenericParameterAuthorityData> impl;
};

enum class CallableParameterPositionKind : uint8_t { Receiver = 0x01, Ordinary = 0x02 };

/// \brief Receiver or zero-based ordinary position without a redundant receiver ordinal.
class CallableParameterPosition final {
public:
  ZC_NODISCARD static CallableParameterPosition receiver() noexcept;
  ZC_NODISCARD static CallableParameterPosition ordinary(uint32_t ordinal) noexcept;
  ZC_NODISCARD CallableParameterPositionKind kind() const noexcept;
  ZC_NODISCARD zc::Maybe<uint32_t> ordinal() const noexcept;
  void encode(CanonicalEncoder& encoder) const;

private:
  CallableParameterPosition(CallableParameterPositionKind kind, uint32_t ordinal) noexcept;

  CallableParameterPositionKind kindValue;
  uint32_t ordinalValue;
};

class CallableParameterIdentityRecord;

/// \brief Raw RFC 0018 digest key for one stable subordinate callable parameter.
class CallableParameterKey final {
public:
  CallableParameterKey(CallableParameterKey&&) noexcept = default;
  CallableParameterKey& operator=(CallableParameterKey&&) noexcept = default;
  ZC_DISALLOW_COPY(CallableParameterKey);

  ZC_NODISCARD static CallableParameterKey compute(const CallableParameterIdentityRecord& record);
  ZC_NODISCARD static zc::Maybe<CallableParameterKey> fromBytes(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD CallableParameterKey clone() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> bytes() const ZC_LIFETIMEBOUND;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;
  bool operator==(const CallableParameterKey& other) const noexcept;
  bool operator!=(const CallableParameterKey& other) const noexcept { return !(*this == other); }

private:
  explicit CallableParameterKey(const Sha256Digest& digest) noexcept;

  Sha256Digest digestValue;
};

class CallableParameterIdentityRecord final {
public:
  ~CallableParameterIdentityRecord() noexcept(false);
  CallableParameterIdentityRecord(CallableParameterIdentityRecord&&) noexcept;
  CallableParameterIdentityRecord& operator=(CallableParameterIdentityRecord&&) noexcept;
  ZC_DISALLOW_COPY(CallableParameterIdentityRecord);

  ZC_NODISCARD static CallableParameterIdentityRecord from(DefinitionKey&& owner,
                                                           CallableParameterPosition position);
  ZC_NODISCARD CallableParameterIdentityRecord clone() const;
  ZC_NODISCARD const DefinitionKey& owner() const noexcept;
  ZC_NODISCARD CallableParameterPosition position() const noexcept;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit CallableParameterIdentityRecord(
      zc::Own<definition_identity_detail::CallableParameterIdentityRecordData>&& impl) noexcept;

  zc::Own<definition_identity_detail::CallableParameterIdentityRecordData> impl;
};

class CallableParameterAuthority final {
public:
  ~CallableParameterAuthority() noexcept(false);
  CallableParameterAuthority(CallableParameterAuthority&&) noexcept;
  CallableParameterAuthority& operator=(CallableParameterAuthority&&) noexcept;
  ZC_DISALLOW_COPY(CallableParameterAuthority);

  ZC_NODISCARD static CallableParameterAuthority from(CallableParameterIdentityRecord&& record);
  ZC_NODISCARD CallableParameterAuthority clone() const;
  ZC_NODISCARD const CallableParameterKey& key() const noexcept;
  ZC_NODISCARD const CallableParameterIdentityRecord& record() const noexcept;
  ZC_NODISCARD bool verify() const;
  ZC_NODISCARD bool sameRecordAs(const CallableParameterAuthority& other) const;

private:
  explicit CallableParameterAuthority(
      zc::Own<definition_identity_detail::CallableParameterAuthorityData>&& impl) noexcept;

  zc::Own<definition_identity_detail::CallableParameterAuthorityData> impl;
};

}  // namespace zomlang::compiler::identity
