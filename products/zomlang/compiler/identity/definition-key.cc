// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/identity/definition-key.h"

#include "zc/core/debug.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::identity {
namespace {

constexpr auto kDefinitionDomain = "zom.named-item-header"_zc;
constexpr auto kImplDomain = "zom.impl-header"_zc;
constexpr auto kGenericParameterDomain = "zom.generic-parameter"_zc;
constexpr auto kCallableParameterDomain = "zom.callable-parameter"_zc;
constexpr uint64_t kMaximumDefinitionIdentityRecordBytes = 4 * 1024 * 1024;
constexpr uint64_t kMaximumDefinitionOwnerCount = kMaximumDefinitionIdentityRecordBytes / 33;

template <typename Record>
Sha256Digest digestRecord(zc::StringPtr domain, const Record& record) {
  const auto encoded = record.encode();
  zc::Vector<uint8_t> preimage(domain.size() + 1 + encoded.size());
  preimage.addAll(domain.asBytes());
  preimage.add(0x00);
  preimage.addAll(encoded);
  ZC_IF_SOME(digest, sha256(preimage.asPtr())) { return digest; }
  ZC_UNREACHABLE
}

template <typename Value>
void encodeSequence(CanonicalEncoder& encoder, zc::ArrayPtr<const Value> values) {
  encoder.encodeSequenceSize(values.size());
  for (const auto& value : values) { value.encode(encoder); }
}

bool sameBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  return left == right;
}

zc::Maybe<EnclosingStableOwnerKey> decodeStableOwner(CanonicalDecoder& decoder) {
  auto kind = decoder.decodeUint8();
  auto digest = decoder.decodeDigest();
  if (kind == zc::none || digest == zc::none) { return zc::none; }
  const auto bytes = ZC_ASSERT_NONNULL(digest).bytes();
  switch (static_cast<EnclosingStableOwnerKind>(ZC_ASSERT_NONNULL(kind))) {
    case EnclosingStableOwnerKind::Definition: {
      auto key = DefinitionKey::fromBytes(bytes);
      ZC_IF_SOME(value, key) { return EnclosingStableOwnerKey::definition(zc::mv(value)); }
      return zc::none;
    }
    case EnclosingStableOwnerKind::Implementation: {
      auto key = ImplKey::fromBytes(bytes);
      ZC_IF_SOME(value, key) { return EnclosingStableOwnerKey::implementation(zc::mv(value)); }
      return zc::none;
    }
  }
  return zc::none;
}

}  // namespace

namespace definition_identity_detail {

struct DefinitionIdentityRecordData final {
  ModuleKey module;
  zc::Vector<EnclosingStableOwnerKey> owners;
  DefinitionKind kind;
  DefinitionNamespace nameSpace;
  DeclaredDefinitionName name;
  zc::Maybe<OverloadHeaderDigest> overloadHeader;
};

struct ImplIdentityRecordData final {
  ModuleKey module;
  zc::Vector<EnclosingStableOwnerKey> owners;
  CanonicalImplHeader header;
};

struct DefinitionIdentityAuthorityData final {
  DefinitionKey key;
  DefinitionIdentityRecord record;
  zc::Maybe<OverloadHeaderAuthority> overloadHeaderAuthority;
};

struct ImplIdentityAuthorityData final {
  ImplKey key;
  ImplIdentityRecord record;
};

struct GenericParameterIdentityRecordData final {
  StableGenericParameterOwnerKey owner;
  GenericParameterKind kind;
  uint32_t ordinal;
};

struct GenericParameterAuthorityData final {
  GenericParameterKey key;
  GenericParameterIdentityRecord record;
};

struct CallableParameterIdentityRecordData final {
  DefinitionKey owner;
  CallableParameterPosition position;
};

struct CallableParameterAuthorityData final {
  CallableParameterKey key;
  CallableParameterIdentityRecord record;
};

}  // namespace definition_identity_detail

bool isDefinitionKindValue(DefinitionKind value) noexcept {
  return value >= DefinitionKind::ModuleAlias && value <= DefinitionKind::Closure;
}

zc::Maybe<DefinitionNamespace> definitionNamespaceFor(DefinitionKind value) noexcept {
  switch (value) {
    case DefinitionKind::Function:
    case DefinitionKind::Method:
    case DefinitionKind::Constructor:
    case DefinitionKind::Destructor:
    case DefinitionKind::Field:
    case DefinitionKind::EnumVariant:
    case DefinitionKind::Constant:
    case DefinitionKind::Static:
      return DefinitionNamespace::Value;
    case DefinitionKind::Class:
    case DefinitionKind::Struct:
    case DefinitionKind::Interface:
    case DefinitionKind::Enum:
    case DefinitionKind::Error:
    case DefinitionKind::TypeAlias:
    case DefinitionKind::AssociatedType:
      return DefinitionNamespace::Type;
    case DefinitionKind::ModuleAlias:
      return DefinitionNamespace::Module;
    case DefinitionKind::Parameter:
    case DefinitionKind::TypeParameter:
    case DefinitionKind::Local:
    case DefinitionKind::PatternBinding:
    case DefinitionKind::Closure:
      return zc::none;
  }
  return zc::none;
}

bool isStableDefinitionKind(DefinitionKind value) noexcept {
  return definitionNamespaceFor(value) != zc::none;
}

DefinitionKey::DefinitionKey(const Sha256Digest& digest) noexcept : digestValue(digest) {}

DefinitionKey DefinitionKey::compute(const DefinitionIdentityRecord& record) {
  return DefinitionKey(digestRecord(kDefinitionDomain, record));
}

zc::Maybe<DefinitionKey> DefinitionKey::fromBytes(zc::ArrayPtr<const uint8_t> bytes) {
  ZC_IF_SOME(digest, Sha256Digest::fromBytes(bytes)) { return DefinitionKey(digest); }
  return zc::none;
}

DefinitionKey DefinitionKey::clone() const noexcept { return DefinitionKey(digestValue); }
zc::ArrayPtr<const uint8_t> DefinitionKey::bytes() const { return digestValue.bytes(); }
void DefinitionKey::encode(CanonicalEncoder& encoder) const { encoder.encodeDigest(digestValue); }
zc::Array<uint8_t> DefinitionKey::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}
bool DefinitionKey::operator==(const DefinitionKey& other) const noexcept {
  return digestValue == other.digestValue;
}

ImplKey::ImplKey(const Sha256Digest& digest) noexcept : digestValue(digest) {}
ImplKey ImplKey::compute(const ImplIdentityRecord& record) {
  return ImplKey(digestRecord(kImplDomain, record));
}
zc::Maybe<ImplKey> ImplKey::fromBytes(zc::ArrayPtr<const uint8_t> bytes) {
  ZC_IF_SOME(digest, Sha256Digest::fromBytes(bytes)) { return ImplKey(digest); }
  return zc::none;
}
ImplKey ImplKey::clone() const noexcept { return ImplKey(digestValue); }
zc::ArrayPtr<const uint8_t> ImplKey::bytes() const { return digestValue.bytes(); }
void ImplKey::encode(CanonicalEncoder& encoder) const { encoder.encodeDigest(digestValue); }
zc::Array<uint8_t> ImplKey::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}
bool ImplKey::operator==(const ImplKey& other) const noexcept {
  return digestValue == other.digestValue;
}

EnclosingStableOwnerKey::EnclosingStableOwnerKey(StableDefinitionOwner&& owner) noexcept
    : value(zc::mv(owner)) {}
EnclosingStableOwnerKey::EnclosingStableOwnerKey(StableImplementationOwner&& owner) noexcept
    : value(zc::mv(owner)) {}
EnclosingStableOwnerKey EnclosingStableOwnerKey::definition(DefinitionKey&& key) {
  return EnclosingStableOwnerKey(StableDefinitionOwner{zc::mv(key)});
}
EnclosingStableOwnerKey EnclosingStableOwnerKey::implementation(ImplKey&& key) {
  return EnclosingStableOwnerKey(StableImplementationOwner{zc::mv(key)});
}
EnclosingStableOwnerKey EnclosingStableOwnerKey::clone() const {
  if (value.is<StableDefinitionOwner>()) {
    return definition(value.get<StableDefinitionOwner>().key.clone());
  }
  return implementation(value.get<StableImplementationOwner>().key.clone());
}
EnclosingStableOwnerKind EnclosingStableOwnerKey::kind() const noexcept {
  return value.is<StableDefinitionOwner>() ? EnclosingStableOwnerKind::Definition
                                           : EnclosingStableOwnerKind::Implementation;
}
zc::Maybe<const DefinitionKey&> EnclosingStableOwnerKey::definitionKey() const noexcept {
  ZC_IF_SOME(owner, value.tryGet<StableDefinitionOwner>()) { return owner.key; }
  return zc::none;
}
zc::Maybe<const ImplKey&> EnclosingStableOwnerKey::implKey() const noexcept {
  ZC_IF_SOME(owner, value.tryGet<StableImplementationOwner>()) { return owner.key; }
  return zc::none;
}
void EnclosingStableOwnerKey::encode(CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(kind()));
  if (value.is<StableDefinitionOwner>()) {
    value.get<StableDefinitionOwner>().key.encode(encoder);
  } else {
    value.get<StableImplementationOwner>().key.encode(encoder);
  }
}
zc::Array<uint8_t> EnclosingStableOwnerKey::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

DefinitionIdentityRecord::DefinitionIdentityRecord(
    zc::Own<definition_identity_detail::DefinitionIdentityRecordData>&& value) noexcept
    : impl(zc::mv(value)) {}
DefinitionIdentityRecord::~DefinitionIdentityRecord() noexcept(false) = default;
DefinitionIdentityRecord::DefinitionIdentityRecord(DefinitionIdentityRecord&&) noexcept = default;
DefinitionIdentityRecord& DefinitionIdentityRecord::operator=(DefinitionIdentityRecord&&) noexcept =
    default;

zc::Maybe<DefinitionIdentityRecord> DefinitionIdentityRecord::from(
    ModuleKey&& module, zc::Vector<EnclosingStableOwnerKey>&& owners, DefinitionKind kind,
    DefinitionNamespace nameSpace, DeclaredDefinitionName&& name,
    zc::Maybe<OverloadHeaderDigest>&& overloadHeader) {
  auto expectedNamespace = definitionNamespaceFor(kind);
  if (expectedNamespace == zc::none || expectedNamespace != nameSpace) { return zc::none; }
  const bool callable = kind == DefinitionKind::Function || kind == DefinitionKind::Method ||
                        kind == DefinitionKind::Constructor;
  if (callable != (overloadHeader != zc::none)) { return zc::none; }
  return DefinitionIdentityRecord(
      zc::heap<definition_identity_detail::DefinitionIdentityRecordData>(
          definition_identity_detail::DefinitionIdentityRecordData{zc::mv(module), zc::mv(owners),
                                                                   kind, nameSpace, zc::mv(name),
                                                                   zc::mv(overloadHeader)}));
}

zc::Maybe<DefinitionIdentityRecord> DefinitionIdentityRecord::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumDefinitionIdentityRecordBytes) {
    return zc::none;
  }
  CanonicalDecoder decoder(bytes);
  auto module = ModuleKey::decodeCanonical(decoder);
  auto ownerCount = decoder.decodeSequenceSize(kMaximumDefinitionOwnerCount);
  if (module == zc::none || ownerCount == zc::none) { return zc::none; }
  zc::Vector<EnclosingStableOwnerKey> owners(static_cast<size_t>(ZC_ASSERT_NONNULL(ownerCount)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(ownerCount); ++index) {
    auto owner = decodeStableOwner(decoder);
    if (owner == zc::none) { return zc::none; }
    ZC_IF_SOME(value, owner) { owners.add(zc::mv(value)); }
  }
  auto kind = decoder.decodeUint8();
  auto nameSpace = decoder.decodeUint8();
  auto name = DeclaredDefinitionName::decodeCanonical(decoder);
  auto overloadPresence = decoder.decodeUint8();
  if (kind == zc::none || nameSpace == zc::none || name == zc::none ||
      overloadPresence == zc::none) {
    return zc::none;
  }
  zc::Maybe<OverloadHeaderDigest> overloadHeader;
  switch (ZC_ASSERT_NONNULL(overloadPresence)) {
    case 0x00:
      break;
    case 0x01: {
      auto digest = decoder.decodeDigest();
      if (digest == zc::none) { return zc::none; }
      overloadHeader = OverloadHeaderDigest::fromBytes(ZC_ASSERT_NONNULL(digest).bytes());
      if (overloadHeader == zc::none) { return zc::none; }
      break;
    }
    default:
      return zc::none;
  }
  if (!decoder.finished()) { return zc::none; }
  auto record = from(zc::mv(ZC_ASSERT_NONNULL(module)), zc::mv(owners),
                     static_cast<DefinitionKind>(ZC_ASSERT_NONNULL(kind)),
                     static_cast<DefinitionNamespace>(ZC_ASSERT_NONNULL(nameSpace)),
                     zc::mv(ZC_ASSERT_NONNULL(name)), zc::mv(overloadHeader));
  if (record == zc::none || ZC_ASSERT_NONNULL(record).encode().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(record);
}

DefinitionIdentityRecord DefinitionIdentityRecord::clone() const {
  zc::Vector<EnclosingStableOwnerKey> owners(impl->owners.size());
  for (const auto& owner : impl->owners) { owners.add(owner.clone()); }
  zc::Maybe<OverloadHeaderDigest> overloadHeader;
  ZC_IF_SOME(value, impl->overloadHeader) { overloadHeader = value.clone(); }
  auto cloned = from(impl->module.clone(), zc::mv(owners), impl->kind, impl->nameSpace,
                     impl->name.clone(), zc::mv(overloadHeader));
  ZC_IF_SOME(value, cloned) { return zc::mv(value); }
  ZC_UNREACHABLE
}
const ModuleKey& DefinitionIdentityRecord::module() const noexcept { return impl->module; }
zc::ArrayPtr<const EnclosingStableOwnerKey> DefinitionIdentityRecord::owners() const noexcept {
  return impl->owners.asPtr();
}
DefinitionKind DefinitionIdentityRecord::kind() const noexcept { return impl->kind; }
DefinitionNamespace DefinitionIdentityRecord::nameSpace() const noexcept { return impl->nameSpace; }
zc::StringPtr DefinitionIdentityRecord::name() const noexcept { return impl->name.text(); }
zc::Maybe<const OverloadHeaderDigest&> DefinitionIdentityRecord::overloadHeader() const noexcept {
  ZC_IF_SOME(value, impl->overloadHeader) { return value; }
  return zc::none;
}
void DefinitionIdentityRecord::encode(CanonicalEncoder& encoder) const {
  impl->module.encode(encoder);
  encodeSequence(encoder, impl->owners.asPtr());
  encoder.encodeUint8(static_cast<uint8_t>(impl->kind));
  encoder.encodeUint8(static_cast<uint8_t>(impl->nameSpace));
  impl->name.encode(encoder);
  ZC_IF_SOME(value, impl->overloadHeader) {
    encoder.encodeSome();
    value.encode(encoder);
  } else {
    encoder.encodeNone();
  }
}
zc::Array<uint8_t> DefinitionIdentityRecord::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

ImplIdentityRecord::ImplIdentityRecord(
    zc::Own<definition_identity_detail::ImplIdentityRecordData>&& value) noexcept
    : impl(zc::mv(value)) {}
ImplIdentityRecord::~ImplIdentityRecord() noexcept(false) = default;
ImplIdentityRecord::ImplIdentityRecord(ImplIdentityRecord&&) noexcept = default;
ImplIdentityRecord& ImplIdentityRecord::operator=(ImplIdentityRecord&&) noexcept = default;
ImplIdentityRecord ImplIdentityRecord::from(ModuleKey&& module,
                                            zc::Vector<EnclosingStableOwnerKey>&& owners,
                                            CanonicalImplHeader&& header) {
  return ImplIdentityRecord(zc::heap<definition_identity_detail::ImplIdentityRecordData>(
      definition_identity_detail::ImplIdentityRecordData{zc::mv(module), zc::mv(owners),
                                                         zc::mv(header)}));
}
ImplIdentityRecord ImplIdentityRecord::clone() const {
  zc::Vector<EnclosingStableOwnerKey> owners(impl->owners.size());
  for (const auto& owner : impl->owners) { owners.add(owner.clone()); }
  return from(impl->module.clone(), zc::mv(owners), impl->header.clone());
}
const ModuleKey& ImplIdentityRecord::module() const noexcept { return impl->module; }
zc::ArrayPtr<const EnclosingStableOwnerKey> ImplIdentityRecord::owners() const noexcept {
  return impl->owners.asPtr();
}
const CanonicalImplHeader& ImplIdentityRecord::header() const noexcept { return impl->header; }
void ImplIdentityRecord::encode(CanonicalEncoder& encoder) const {
  impl->module.encode(encoder);
  encodeSequence(encoder, impl->owners.asPtr());
  impl->header.encode(encoder);
}
zc::Array<uint8_t> ImplIdentityRecord::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

DefinitionIdentityAuthority::DefinitionIdentityAuthority(
    zc::Own<definition_identity_detail::DefinitionIdentityAuthorityData>&& value) noexcept
    : impl(zc::mv(value)) {}
DefinitionIdentityAuthority::~DefinitionIdentityAuthority() noexcept(false) = default;
DefinitionIdentityAuthority::DefinitionIdentityAuthority(DefinitionIdentityAuthority&&) noexcept =
    default;
DefinitionIdentityAuthority& DefinitionIdentityAuthority::operator=(
    DefinitionIdentityAuthority&&) noexcept = default;
zc::Maybe<DefinitionIdentityAuthority> DefinitionIdentityAuthority::from(
    DefinitionIdentityRecord&& record,
    zc::Maybe<OverloadHeaderAuthority>&& overloadHeaderAuthority) {
  const auto expectedCallableKind = [&]() -> zc::Maybe<CallableHeaderKind> {
    switch (record.kind()) {
      case DefinitionKind::Function:
        return CallableHeaderKind::Function;
      case DefinitionKind::Method:
        return CallableHeaderKind::Method;
      case DefinitionKind::Constructor:
        return CallableHeaderKind::Constructor;
      default:
        return zc::none;
    }
  }();
  if ((record.overloadHeader() == zc::none) != (overloadHeaderAuthority == zc::none)) {
    return zc::none;
  }
  ZC_IF_SOME(recordDigest, record.overloadHeader()) {
    ZC_IF_SOME(authority, overloadHeaderAuthority) {
      if (!authority.verify() || authority.digest() != recordDigest ||
          expectedCallableKind == zc::none || authority.header().name() != record.name()) {
        return zc::none;
      }
      ZC_IF_SOME(kind, expectedCallableKind) {
        if (authority.header().callableKind() != kind) { return zc::none; }
      }
    }
  }
  auto key = DefinitionKey::compute(record);
  return DefinitionIdentityAuthority(
      zc::heap<definition_identity_detail::DefinitionIdentityAuthorityData>(
          definition_identity_detail::DefinitionIdentityAuthorityData{
              zc::mv(key), zc::mv(record), zc::mv(overloadHeaderAuthority)}));
}
DefinitionIdentityAuthority DefinitionIdentityAuthority::clone() const {
  zc::Maybe<OverloadHeaderAuthority> overloadHeaderAuthority;
  ZC_IF_SOME(value, impl->overloadHeaderAuthority) { overloadHeaderAuthority = value.clone(); }
  return DefinitionIdentityAuthority(
      zc::heap<definition_identity_detail::DefinitionIdentityAuthorityData>(
          definition_identity_detail::DefinitionIdentityAuthorityData{
              impl->key.clone(), impl->record.clone(), zc::mv(overloadHeaderAuthority)}));
}
const DefinitionKey& DefinitionIdentityAuthority::key() const noexcept { return impl->key; }
const DefinitionIdentityRecord& DefinitionIdentityAuthority::record() const noexcept {
  return impl->record;
}
zc::Maybe<const OverloadHeaderAuthority&> DefinitionIdentityAuthority::overloadHeaderAuthority()
    const noexcept {
  ZC_IF_SOME(value, impl->overloadHeaderAuthority) { return value; }
  return zc::none;
}
bool DefinitionIdentityAuthority::verify() const {
  if (DefinitionKey::compute(impl->record) != impl->key) { return false; }
  if ((impl->record.overloadHeader() == zc::none) != (impl->overloadHeaderAuthority == zc::none)) {
    return false;
  }
  ZC_IF_SOME(recordDigest, impl->record.overloadHeader()) {
    ZC_IF_SOME(authority, impl->overloadHeaderAuthority) {
      if (!authority.verify() || authority.digest() != recordDigest ||
          authority.header().name() != impl->record.name()) {
        return false;
      }
      switch (impl->record.kind()) {
        case DefinitionKind::Function:
          return authority.header().callableKind() == CallableHeaderKind::Function;
        case DefinitionKind::Method:
          return authority.header().callableKind() == CallableHeaderKind::Method;
        case DefinitionKind::Constructor:
          return authority.header().callableKind() == CallableHeaderKind::Constructor;
        default:
          return false;
      }
    }
  }
  return true;
}
bool DefinitionIdentityAuthority::sameRecordAs(const DefinitionIdentityAuthority& other) const {
  const auto left = impl->record.encode();
  const auto right = other.impl->record.encode();
  if (!sameBytes(left.asPtr(), right.asPtr())) { return false; }
  if ((impl->overloadHeaderAuthority == zc::none) !=
      (other.impl->overloadHeaderAuthority == zc::none)) {
    return false;
  }
  ZC_IF_SOME(leftAuthority, impl->overloadHeaderAuthority) {
    ZC_IF_SOME(rightAuthority, other.impl->overloadHeaderAuthority) {
      return leftAuthority.sameRecordAs(rightAuthority);
    }
  }
  return true;
}

ImplIdentityAuthority::ImplIdentityAuthority(
    zc::Own<definition_identity_detail::ImplIdentityAuthorityData>&& value) noexcept
    : impl(zc::mv(value)) {}
ImplIdentityAuthority::~ImplIdentityAuthority() noexcept(false) = default;
ImplIdentityAuthority::ImplIdentityAuthority(ImplIdentityAuthority&&) noexcept = default;
ImplIdentityAuthority& ImplIdentityAuthority::operator=(ImplIdentityAuthority&&) noexcept = default;
ImplIdentityAuthority ImplIdentityAuthority::from(ImplIdentityRecord&& record) {
  auto key = ImplKey::compute(record);
  return ImplIdentityAuthority(zc::heap<definition_identity_detail::ImplIdentityAuthorityData>(
      definition_identity_detail::ImplIdentityAuthorityData{zc::mv(key), zc::mv(record)}));
}
ImplIdentityAuthority ImplIdentityAuthority::clone() const {
  return ImplIdentityAuthority(zc::heap<definition_identity_detail::ImplIdentityAuthorityData>(
      definition_identity_detail::ImplIdentityAuthorityData{impl->key.clone(),
                                                            impl->record.clone()}));
}
const ImplKey& ImplIdentityAuthority::key() const noexcept { return impl->key; }
const ImplIdentityRecord& ImplIdentityAuthority::record() const noexcept { return impl->record; }
bool ImplIdentityAuthority::verify() const { return ImplKey::compute(impl->record) == impl->key; }
bool ImplIdentityAuthority::sameRecordAs(const ImplIdentityAuthority& other) const {
  const auto left = impl->record.encode();
  const auto right = other.impl->record.encode();
  return sameBytes(left.asPtr(), right.asPtr());
}

StableGenericParameterOwnerKey::StableGenericParameterOwnerKey(
    EnclosingStableOwnerKey&& value) noexcept
    : owner(zc::mv(value)) {}
StableGenericParameterOwnerKey StableGenericParameterOwnerKey::definition(DefinitionKey&& key) {
  return StableGenericParameterOwnerKey(EnclosingStableOwnerKey::definition(zc::mv(key)));
}
StableGenericParameterOwnerKey StableGenericParameterOwnerKey::implementation(ImplKey&& key) {
  return StableGenericParameterOwnerKey(EnclosingStableOwnerKey::implementation(zc::mv(key)));
}
StableGenericParameterOwnerKey StableGenericParameterOwnerKey::clone() const {
  return StableGenericParameterOwnerKey(owner.clone());
}
StableGenericParameterOwnerKind StableGenericParameterOwnerKey::kind() const noexcept {
  return owner.kind();
}
zc::Maybe<const DefinitionKey&> StableGenericParameterOwnerKey::definitionKey() const noexcept {
  return owner.definitionKey();
}
zc::Maybe<const ImplKey&> StableGenericParameterOwnerKey::implKey() const noexcept {
  return owner.implKey();
}
void StableGenericParameterOwnerKey::encode(CanonicalEncoder& encoder) const {
  owner.encode(encoder);
}

GenericParameterKey::GenericParameterKey(const Sha256Digest& digest) noexcept
    : digestValue(digest) {}
GenericParameterKey GenericParameterKey::compute(const GenericParameterIdentityRecord& record) {
  return GenericParameterKey(digestRecord(kGenericParameterDomain, record));
}
zc::Maybe<GenericParameterKey> GenericParameterKey::fromBytes(zc::ArrayPtr<const uint8_t> bytes) {
  ZC_IF_SOME(digest, Sha256Digest::fromBytes(bytes)) { return GenericParameterKey(digest); }
  return zc::none;
}
GenericParameterKey GenericParameterKey::clone() const noexcept {
  return GenericParameterKey(digestValue);
}
zc::ArrayPtr<const uint8_t> GenericParameterKey::bytes() const { return digestValue.bytes(); }
void GenericParameterKey::encode(CanonicalEncoder& encoder) const {
  encoder.encodeDigest(digestValue);
}
zc::Array<uint8_t> GenericParameterKey::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}
bool GenericParameterKey::operator==(const GenericParameterKey& other) const noexcept {
  return digestValue == other.digestValue;
}

GenericParameterIdentityRecord::GenericParameterIdentityRecord(
    zc::Own<definition_identity_detail::GenericParameterIdentityRecordData>&& value) noexcept
    : impl(zc::mv(value)) {}
GenericParameterIdentityRecord::~GenericParameterIdentityRecord() noexcept(false) = default;
GenericParameterIdentityRecord::GenericParameterIdentityRecord(
    GenericParameterIdentityRecord&&) noexcept = default;
GenericParameterIdentityRecord& GenericParameterIdentityRecord::operator=(
    GenericParameterIdentityRecord&&) noexcept = default;
GenericParameterIdentityRecord GenericParameterIdentityRecord::type(
    StableGenericParameterOwnerKey&& owner, uint32_t ordinal) {
  return GenericParameterIdentityRecord(
      zc::heap<definition_identity_detail::GenericParameterIdentityRecordData>(
          definition_identity_detail::GenericParameterIdentityRecordData{
              zc::mv(owner), GenericParameterKind::Type, ordinal}));
}
GenericParameterIdentityRecord GenericParameterIdentityRecord::clone() const {
  return type(impl->owner.clone(), impl->ordinal);
}
const StableGenericParameterOwnerKey& GenericParameterIdentityRecord::owner() const noexcept {
  return impl->owner;
}
GenericParameterKind GenericParameterIdentityRecord::kind() const noexcept { return impl->kind; }
uint32_t GenericParameterIdentityRecord::ordinal() const noexcept { return impl->ordinal; }
void GenericParameterIdentityRecord::encode(CanonicalEncoder& encoder) const {
  impl->owner.encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(impl->kind));
  encoder.encodeUint32(impl->ordinal);
}
zc::Array<uint8_t> GenericParameterIdentityRecord::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

GenericParameterAuthority::GenericParameterAuthority(
    zc::Own<definition_identity_detail::GenericParameterAuthorityData>&& value) noexcept
    : impl(zc::mv(value)) {}
GenericParameterAuthority::~GenericParameterAuthority() noexcept(false) = default;
GenericParameterAuthority::GenericParameterAuthority(GenericParameterAuthority&&) noexcept =
    default;
GenericParameterAuthority& GenericParameterAuthority::operator=(
    GenericParameterAuthority&&) noexcept = default;
GenericParameterAuthority GenericParameterAuthority::from(GenericParameterIdentityRecord&& record) {
  auto key = GenericParameterKey::compute(record);
  return GenericParameterAuthority(
      zc::heap<definition_identity_detail::GenericParameterAuthorityData>(
          definition_identity_detail::GenericParameterAuthorityData{zc::mv(key), zc::mv(record)}));
}
GenericParameterAuthority GenericParameterAuthority::clone() const {
  return GenericParameterAuthority(
      zc::heap<definition_identity_detail::GenericParameterAuthorityData>(
          definition_identity_detail::GenericParameterAuthorityData{impl->key.clone(),
                                                                    impl->record.clone()}));
}
const GenericParameterKey& GenericParameterAuthority::key() const noexcept { return impl->key; }
const GenericParameterIdentityRecord& GenericParameterAuthority::record() const noexcept {
  return impl->record;
}
bool GenericParameterAuthority::verify() const {
  return GenericParameterKey::compute(impl->record) == impl->key;
}
bool GenericParameterAuthority::sameRecordAs(const GenericParameterAuthority& other) const {
  const auto left = impl->record.encode();
  const auto right = other.impl->record.encode();
  return sameBytes(left.asPtr(), right.asPtr());
}

CallableParameterPosition::CallableParameterPosition(CallableParameterPositionKind kind,
                                                     uint32_t ordinal) noexcept
    : kindValue(kind), ordinalValue(ordinal) {}
CallableParameterPosition CallableParameterPosition::receiver() noexcept {
  return CallableParameterPosition(CallableParameterPositionKind::Receiver, 0);
}
CallableParameterPosition CallableParameterPosition::ordinary(uint32_t ordinal) noexcept {
  return CallableParameterPosition(CallableParameterPositionKind::Ordinary, ordinal);
}
CallableParameterPositionKind CallableParameterPosition::kind() const noexcept { return kindValue; }
zc::Maybe<uint32_t> CallableParameterPosition::ordinal() const noexcept {
  return kindValue == CallableParameterPositionKind::Ordinary ? zc::Maybe<uint32_t>(ordinalValue)
                                                              : zc::none;
}
void CallableParameterPosition::encode(CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(kindValue));
  if (kindValue == CallableParameterPositionKind::Ordinary) { encoder.encodeUint32(ordinalValue); }
}

CallableParameterKey::CallableParameterKey(const Sha256Digest& digest) noexcept
    : digestValue(digest) {}
CallableParameterKey CallableParameterKey::compute(const CallableParameterIdentityRecord& record) {
  return CallableParameterKey(digestRecord(kCallableParameterDomain, record));
}
zc::Maybe<CallableParameterKey> CallableParameterKey::fromBytes(zc::ArrayPtr<const uint8_t> bytes) {
  ZC_IF_SOME(digest, Sha256Digest::fromBytes(bytes)) { return CallableParameterKey(digest); }
  return zc::none;
}
CallableParameterKey CallableParameterKey::clone() const noexcept {
  return CallableParameterKey(digestValue);
}
zc::ArrayPtr<const uint8_t> CallableParameterKey::bytes() const { return digestValue.bytes(); }
void CallableParameterKey::encode(CanonicalEncoder& encoder) const {
  encoder.encodeDigest(digestValue);
}
zc::Array<uint8_t> CallableParameterKey::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}
bool CallableParameterKey::operator==(const CallableParameterKey& other) const noexcept {
  return digestValue == other.digestValue;
}

CallableParameterIdentityRecord::CallableParameterIdentityRecord(
    zc::Own<definition_identity_detail::CallableParameterIdentityRecordData>&& value) noexcept
    : impl(zc::mv(value)) {}
CallableParameterIdentityRecord::~CallableParameterIdentityRecord() noexcept(false) = default;
CallableParameterIdentityRecord::CallableParameterIdentityRecord(
    CallableParameterIdentityRecord&&) noexcept = default;
CallableParameterIdentityRecord& CallableParameterIdentityRecord::operator=(
    CallableParameterIdentityRecord&&) noexcept = default;
CallableParameterIdentityRecord CallableParameterIdentityRecord::from(
    DefinitionKey&& owner, CallableParameterPosition position) {
  return CallableParameterIdentityRecord(
      zc::heap<definition_identity_detail::CallableParameterIdentityRecordData>(
          definition_identity_detail::CallableParameterIdentityRecordData{zc::mv(owner),
                                                                          position}));
}
CallableParameterIdentityRecord CallableParameterIdentityRecord::clone() const {
  return from(impl->owner.clone(), impl->position);
}
const DefinitionKey& CallableParameterIdentityRecord::owner() const noexcept { return impl->owner; }
CallableParameterPosition CallableParameterIdentityRecord::position() const noexcept {
  return impl->position;
}
void CallableParameterIdentityRecord::encode(CanonicalEncoder& encoder) const {
  impl->owner.encode(encoder);
  impl->position.encode(encoder);
}
zc::Array<uint8_t> CallableParameterIdentityRecord::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

CallableParameterAuthority::CallableParameterAuthority(
    zc::Own<definition_identity_detail::CallableParameterAuthorityData>&& value) noexcept
    : impl(zc::mv(value)) {}
CallableParameterAuthority::~CallableParameterAuthority() noexcept(false) = default;
CallableParameterAuthority::CallableParameterAuthority(CallableParameterAuthority&&) noexcept =
    default;
CallableParameterAuthority& CallableParameterAuthority::operator=(
    CallableParameterAuthority&&) noexcept = default;
CallableParameterAuthority CallableParameterAuthority::from(
    CallableParameterIdentityRecord&& record) {
  auto key = CallableParameterKey::compute(record);
  return CallableParameterAuthority(
      zc::heap<definition_identity_detail::CallableParameterAuthorityData>(
          definition_identity_detail::CallableParameterAuthorityData{zc::mv(key), zc::mv(record)}));
}
CallableParameterAuthority CallableParameterAuthority::clone() const {
  return CallableParameterAuthority(
      zc::heap<definition_identity_detail::CallableParameterAuthorityData>(
          definition_identity_detail::CallableParameterAuthorityData{impl->key.clone(),
                                                                     impl->record.clone()}));
}
const CallableParameterKey& CallableParameterAuthority::key() const noexcept { return impl->key; }
const CallableParameterIdentityRecord& CallableParameterAuthority::record() const noexcept {
  return impl->record;
}
bool CallableParameterAuthority::verify() const {
  return CallableParameterKey::compute(impl->record) == impl->key;
}
bool CallableParameterAuthority::sameRecordAs(const CallableParameterAuthority& other) const {
  const auto left = impl->record.encode();
  const auto right = other.impl->record.encode();
  return sameBytes(left.asPtr(), right.asPtr());
}

}  // namespace zomlang::compiler::identity
