// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/stable-binding-codec.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::binder {
namespace {

constexpr uint64_t kMaximumRoutedKeyBytes = 65536;
constexpr uint64_t kMaximumBinderValueBytes = 134217728;
constexpr uint64_t kMaximumIdentityRecordBytes = 4 * 1024 * 1024;

bool hasDomain(zc::ArrayPtr<const uint8_t> bytes, zc::StringPtr domain) {
  return bytes.size() > domain.size() && bytes[domain.size()] == 0x00 &&
         bytes.first(domain.size()) == domain.asBytes();
}

zc::Array<uint8_t> withDomain(zc::StringPtr domain, zc::ArrayPtr<const uint8_t> record) {
  zc::Vector<uint8_t> result(domain.size() + 1 + record.size());
  result.addAll(domain.asBytes());
  result.add(0x00);
  result.addAll(record);
  return result.releaseAsArray();
}

zc::Array<uint8_t> encodeNested(zc::StringPtr domain, const identity::ModuleKey& module,
                                zc::ArrayPtr<const uint8_t> nested) {
  identity::CanonicalEncoder record;
  const auto moduleBytes = module.encode();
  record.encodeByteString(moduleBytes.asPtr());
  record.encodeByteString(nested);
  const auto bytes = record.finish();
  return withDomain(domain, bytes.asPtr());
}

zc::Array<uint8_t> encodeDigest(zc::StringPtr domain, const identity::ModuleKey& module,
                                zc::ArrayPtr<const uint8_t> digest) {
  identity::CanonicalEncoder prefix;
  const auto moduleBytes = module.encode();
  prefix.encodeByteString(moduleBytes.asPtr());
  const auto prefixBytes = prefix.finish();
  zc::Vector<uint8_t> record(prefixBytes.size() + digest.size());
  record.addAll(prefixBytes.asPtr());
  record.addAll(digest);
  const auto bytes = record.releaseAsArray();
  return withDomain(domain, bytes.asPtr());
}

zc::Maybe<identity::ModuleKey> decodeModule(identity::CanonicalDecoder& decoder) {
  auto bytes = decoder.decodeByteString(kMaximumRoutedKeyBytes);
  if (bytes == zc::none) { return zc::none; }
  identity::CanonicalDecoder nested(ZC_ASSERT_NONNULL(bytes).asPtr());
  auto module = identity::ModuleKey::decodeCanonical(nested);
  if (module == zc::none || !nested.finished() ||
      ZC_ASSERT_NONNULL(module).encode().asPtr() != ZC_ASSERT_NONNULL(bytes).asPtr()) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(module));
}

template <typename Enum>
zc::Array<uint8_t> encodeEnum(Enum value) {
  identity::CanonicalEncoder encoder;
  encoder.encodeUint8(static_cast<uint8_t>(value));
  return encoder.finish();
}

template <typename Enum>
zc::Maybe<Enum> decodeEnum(zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto tag = decoder.decodeUint8();
  if (tag == zc::none || !decoder.finished()) { return zc::none; }
  auto value = static_cast<Enum>(ZC_ASSERT_NONNULL(tag));
  return isStableBindingValue(value) ? zc::Maybe<Enum>(value) : zc::none;
}

void encodeHeaderSite(identity::CanonicalEncoder& encoder, const StableHeaderSite& value) {
  if (value.value().is<DefinitionAuthoritySite>()) {
    encoder.encodeUint8(0x01);
    value.value().get<DefinitionAuthoritySite>().site.encode(encoder);
    return;
  }
  encoder.encodeUint8(0x02);
  value.value().get<ImplementationOccurrenceSite>().site.encode(encoder);
}

zc::Maybe<StableHeaderSite> decodeHeaderSite(identity::CanonicalDecoder& decoder) {
  auto tag = decoder.decodeUint8();
  if (tag == zc::none) { return zc::none; }
  if (ZC_ASSERT_NONNULL(tag) == 0x01) {
    auto site = IdentitySyntaxSiteKey::decodeCanonical(decoder);
    if (site == zc::none) { return zc::none; }
    return StableHeaderSite::definition(zc::mv(ZC_ASSERT_NONNULL(site)));
  }
  if (ZC_ASSERT_NONNULL(tag) != 0x02) { return zc::none; }
  auto implementationBytes = decoder.decodeBytes(32);
  if (implementationBytes == zc::none) { return zc::none; }
  auto implementation =
      identity::ImplKey::fromBytes(ZC_ASSERT_NONNULL(implementationBytes).asPtr());
  auto site = IdentitySyntaxSiteKey::decodeCanonical(decoder);
  if (implementation == zc::none || site == zc::none) { return zc::none; }
  return StableHeaderSite::implementation(ImplSourceOccurrenceKey::from(
      zc::mv(ZC_ASSERT_NONNULL(implementation)), zc::mv(ZC_ASSERT_NONNULL(site))));
}

zc::Maybe<identity::GenericParameterIdentityRecord> decodeGenericRecord(
    identity::CanonicalDecoder& decoder) {
  auto ownerTag = decoder.decodeUint8();
  auto ownerBytes = decoder.decodeBytes(32);
  auto kind = decoder.decodeUint8();
  auto ordinal = decoder.decodeUint32();
  if (ownerTag == zc::none || ownerBytes == zc::none || kind == zc::none || ordinal == zc::none ||
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
  return identity::GenericParameterIdentityRecord::type(zc::mv(ZC_ASSERT_NONNULL(owner)),
                                                        ZC_ASSERT_NONNULL(ordinal));
}

zc::Maybe<identity::CallableParameterPosition> decodeCallablePosition(
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
  return ordinal == zc::none
             ? zc::none
             : zc::Maybe<identity::CallableParameterPosition>(
                   identity::CallableParameterPosition::ordinary(ZC_ASSERT_NONNULL(ordinal)));
}

zc::Maybe<identity::CallableParameterIdentityRecord> decodeCallableRecord(
    identity::CanonicalDecoder& decoder) {
  auto ownerBytes = decoder.decodeBytes(32);
  if (ownerBytes == zc::none) { return zc::none; }
  auto owner = identity::DefinitionKey::fromBytes(ZC_ASSERT_NONNULL(ownerBytes).asPtr());
  auto position = decodeCallablePosition(decoder);
  if (owner == zc::none || position == zc::none) { return zc::none; }
  return identity::CallableParameterIdentityRecord::from(zc::mv(ZC_ASSERT_NONNULL(owner)),
                                                         ZC_ASSERT_NONNULL(position));
}

void encodeBindingName(identity::CanonicalEncoder& encoder, const BindingNameKey& value) {
  encoder.encodeUint8(static_cast<uint8_t>(value.nameSpace()));
  value.name().encode(encoder);
}

zc::Maybe<BindingNameKey> decodeBindingName(identity::CanonicalDecoder& decoder) {
  auto nameSpace = decoder.decodeUint8();
  auto name = identity::DeclaredDefinitionName::decodeCanonical(decoder);
  if (nameSpace == zc::none || name == zc::none) { return zc::none; }
  return BindingNameKey::from(static_cast<Namespace>(ZC_ASSERT_NONNULL(nameSpace)),
                              zc::mv(ZC_ASSERT_NONNULL(name)));
}

template <typename T>
void encodeSequence(identity::CanonicalEncoder& encoder, const CanonicalSequence<T>& values) {
  encoder.encodeSequenceSize(values.values().size());
  for (const auto& value : values.values()) {
    const auto bytes = StableBindingCodec<T>::encode(value);
    encoder.encodeByteString(bytes.asPtr());
  }
}

template <typename T>
zc::Maybe<CanonicalSequence<T>> decodeSequence(identity::CanonicalDecoder& decoder) {
  auto count =
      decoder.decodeSequenceSize(stable_binding_codec_detail::kBinderSemanticSequenceRecords);
  if (count == zc::none || ZC_ASSERT_NONNULL(count) > decoder.remaining() / sizeof(uint64_t)) {
    return zc::none;
  }
  zc::Vector<T> values(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto bytes = decoder.decodeByteString(kMaximumBinderValueBytes);
    if (bytes == zc::none) { return zc::none; }
    auto value = StableBindingCodec<T>::decode(ZC_ASSERT_NONNULL(bytes).asPtr());
    if (value == zc::none) { return zc::none; }
    values.add(zc::mv(ZC_ASSERT_NONNULL(value)));
  }
  return StableBindingSequenceBuilder<T>::from(zc::mv(values));
}

template <typename T>
void encodeNonEmptySequence(identity::CanonicalEncoder& encoder,
                            const CanonicalNonEmptySequence<T>& values) {
  encoder.encodeSequenceSize(values.values().size());
  for (const auto& value : values.values()) {
    encoder.encodeByteString(StableBindingCodec<T>::encode(value).asPtr());
  }
}

template <typename T>
zc::Maybe<CanonicalNonEmptySequence<T>> decodeNonEmptySequence(
    identity::CanonicalDecoder& decoder,
    uint64_t maximumCount = stable_binding_codec_detail::kBinderSemanticSequenceRecords) {
  auto count = decoder.decodeSequenceSize(maximumCount);
  if (count == zc::none || ZC_ASSERT_NONNULL(count) > decoder.remaining() / sizeof(uint64_t)) {
    return zc::none;
  }
  zc::Vector<T> values(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto bytes = decoder.decodeByteString(kMaximumBinderValueBytes);
    if (bytes == zc::none) { return zc::none; }
    auto value = StableBindingCodec<T>::decode(ZC_ASSERT_NONNULL(bytes).asPtr());
    if (value == zc::none) { return zc::none; }
    values.add(zc::mv(ZC_ASSERT_NONNULL(value)));
  }
  return StableBindingSequenceBuilder<T>::fromNonEmpty(zc::mv(values));
}

template <typename Key, typename Routed>
zc::Maybe<Routed> decodeDigestKey(zc::ArrayPtr<const uint8_t> bytes, zc::StringPtr domain) {
  if (bytes.size() > kMaximumRoutedKeyBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto module = decodeModule(decoder);
  auto digest = decoder.decodeBytes(32);
  if (module == zc::none || digest == zc::none || !decoder.finished()) { return zc::none; }
  auto key = Key::fromBytes(ZC_ASSERT_NONNULL(digest).asPtr());
  if (key == zc::none) { return zc::none; }
  return Routed::from(zc::mv(ZC_ASSERT_NONNULL(module)), zc::mv(ZC_ASSERT_NONNULL(key)));
}

zc::Maybe<identity::SemanticImportBindingKey> decodeSemanticImportBinding(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.semantic-import-binding"_zc;
  if (bytes.size() > kMaximumRoutedKeyBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto requester = identity::ModuleKey::decodeCanonical(decoder);
  auto resolutionBytes = decoder.decodeByteString(kMaximumRoutedKeyBytes);
  auto operation = decoder.decodeUint8();
  auto sourceNamespace = decoder.decodeUint8();
  auto sourceName = identity::DeclaredDefinitionName::decodeCanonical(decoder);
  auto localNamespace = decoder.decodeUint8();
  auto localName = identity::DeclaredDefinitionName::decodeCanonical(decoder);
  if (requester == zc::none || resolutionBytes == zc::none || operation == zc::none ||
      sourceNamespace == zc::none || sourceName == zc::none || localNamespace == zc::none ||
      localName == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto resolution =
      identity::ModuleResolutionKey::decodeCanonical(ZC_ASSERT_NONNULL(resolutionBytes).asPtr());
  if (resolution == zc::none) { return zc::none; }
  auto result = identity::SemanticImportBindingKey::from(
      zc::mv(ZC_ASSERT_NONNULL(requester)), zc::mv(ZC_ASSERT_NONNULL(resolution)),
      static_cast<identity::SemanticImportOperation>(ZC_ASSERT_NONNULL(operation)),
      static_cast<identity::DefinitionNamespace>(ZC_ASSERT_NONNULL(sourceNamespace)),
      zc::mv(ZC_ASSERT_NONNULL(sourceName)),
      static_cast<identity::DefinitionNamespace>(ZC_ASSERT_NONNULL(localNamespace)),
      zc::mv(ZC_ASSERT_NONNULL(localName)));
  if (result == zc::none || ZC_ASSERT_NONNULL(result).encode().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(result);
}

template <typename Nested, typename Routed>
zc::Maybe<Routed> decodeNestedKey(zc::ArrayPtr<const uint8_t> bytes, zc::StringPtr domain,
                                  zc::Maybe<Nested> (*decode)(zc::ArrayPtr<const uint8_t>)) {
  if (bytes.size() > kMaximumRoutedKeyBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto module = decodeModule(decoder);
  auto nestedBytes = decoder.decodeByteString(kMaximumRoutedKeyBytes);
  if (module == zc::none || nestedBytes == zc::none || !decoder.finished()) { return zc::none; }
  auto nested = decode(ZC_ASSERT_NONNULL(nestedBytes).asPtr());
  if (nested == zc::none) { return zc::none; }
  return Routed::from(zc::mv(ZC_ASSERT_NONNULL(module)), zc::mv(ZC_ASSERT_NONNULL(nested)));
}

void encodeFrame(identity::CanonicalEncoder& encoder, zc::ArrayPtr<const uint8_t> bytes) {
  encoder.encodeByteString(bytes);
}
template <typename T>
void encodeStableFrame(identity::CanonicalEncoder& encoder, const T& value) {
  const auto bytes = StableBindingCodec<T>::encode(value);
  encodeFrame(encoder, bytes.asPtr());
}
template <typename T>
zc::Maybe<T> decodeStableFrame(identity::CanonicalDecoder& decoder) {
  auto bytes = decoder.decodeByteString(kMaximumRoutedKeyBytes);
  return bytes == zc::none ? zc::none
                           : StableBindingCodec<T>::decode(ZC_ASSERT_NONNULL(bytes).asPtr());
}
template <typename T>
zc::Maybe<T> decodeLocalFrame(identity::CanonicalDecoder& decoder) {
  auto bytes = decoder.decodeByteString(kMaximumRoutedKeyBytes);
  return bytes == zc::none ? zc::none : T::decodeCanonical(ZC_ASSERT_NONNULL(bytes).asPtr());
}
zc::Maybe<identity::ModuleKey> decodeModuleFrame(identity::CanonicalDecoder& decoder) {
  auto bytes = decoder.decodeByteString(kMaximumRoutedKeyBytes);
  if (bytes == zc::none) { return zc::none; }
  identity::CanonicalDecoder nested(ZC_ASSERT_NONNULL(bytes).asPtr());
  auto module = identity::ModuleKey::decodeCanonical(nested);
  return module != zc::none && nested.finished() &&
                 ZC_ASSERT_NONNULL(module).encode().asPtr() == ZC_ASSERT_NONNULL(bytes).asPtr()
             ? zc::mv(module)
             : zc::none;
}
template <typename T>
zc::Maybe<T> finishSum(zc::Maybe<T>&& result, identity::CanonicalDecoder& decoder,
                       zc::ArrayPtr<const uint8_t> bytes) {
  return result != zc::none && decoder.finished() &&
                 StableBindingCodec<T>::encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes
             ? zc::mv(result)
             : zc::none;
}

}  // namespace

#define ZOM_DEFINE_ENUM_CODEC(Type)                                                     \
  zc::Array<uint8_t> StableBindingCodec<Type>::encode(const Type& value) {              \
    return encodeEnum(value);                                                           \
  }                                                                                     \
  zc::Maybe<Type> StableBindingCodec<Type>::decode(zc::ArrayPtr<const uint8_t> bytes) { \
    return decodeEnum<Type>(bytes);                                                     \
  }

ZOM_DEFINE_ENUM_CODEC(DefinitionBodyDisposition)
ZOM_DEFINE_ENUM_CODEC(ImplementationSourceForm)
ZOM_DEFINE_ENUM_CODEC(ScopeRole)
ZOM_DEFINE_ENUM_CODEC(ScopeKind)
ZOM_DEFINE_ENUM_CODEC(StableExplicitCaptureMode)
ZOM_DEFINE_ENUM_CODEC(BinderKeyFailureKind)

#undef ZOM_DEFINE_ENUM_CODEC

zc::Array<uint8_t> StableBindingCodec<Namespace>::encode(const Namespace& value) {
  if (value < Namespace::Value || value > Namespace::Attribute) { return zc::Array<uint8_t>(); }
  identity::CanonicalEncoder encoder;
  encoder.encodeUint8(static_cast<uint8_t>(value));
  return encoder.finish();
}
zc::Maybe<Namespace> StableBindingCodec<Namespace>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto tag = decoder.decodeUint8();
  if (tag == zc::none || !decoder.finished()) { return zc::none; }
  auto value = static_cast<Namespace>(ZC_ASSERT_NONNULL(tag));
  return value >= Namespace::Value && value <= Namespace::Attribute ? zc::Maybe<Namespace>(value)
                                                                    : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableHeaderSite>::encode(const StableHeaderSite& value) {
  identity::CanonicalEncoder encoder;
  encodeHeaderSite(encoder, value);
  return encoder.finish();
}

zc::Maybe<StableHeaderSite> StableBindingCodec<StableHeaderSite>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto result = decodeHeaderSite(decoder);
  return result != zc::none && decoder.finished() ? zc::mv(result) : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableHeaderGenericParameter>::encode(
    const StableHeaderGenericParameter& value) {
  identity::CanonicalEncoder record;
  value.key().encode(record);
  value.record().encode(record);
  encodeHeaderSite(record, value.site());
  value.name().encode(record);
  record.encodeUint32(value.ordinal());
  const auto bytes = record.finish();
  return withDomain("zom.binder.header-generic-parameter"_zc, bytes.asPtr());
}

zc::Maybe<StableHeaderGenericParameter> StableBindingCodec<StableHeaderGenericParameter>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.header-generic-parameter"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto keyBytes = decoder.decodeBytes(32);
  auto record = decodeGenericRecord(decoder);
  auto site = decodeHeaderSite(decoder);
  auto name = identity::DeclaredDefinitionName::decodeCanonical(decoder);
  auto ordinal = decoder.decodeUint32();
  if (keyBytes == zc::none || record == zc::none || site == zc::none || name == zc::none ||
      ordinal == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto key = identity::GenericParameterKey::fromBytes(ZC_ASSERT_NONNULL(keyBytes).asPtr());
  if (key == zc::none) { return zc::none; }
  auto result = StableHeaderGenericParameter::from(
      zc::mv(ZC_ASSERT_NONNULL(key)), zc::mv(ZC_ASSERT_NONNULL(record)),
      zc::mv(ZC_ASSERT_NONNULL(site)), zc::mv(ZC_ASSERT_NONNULL(name)), ZC_ASSERT_NONNULL(ordinal));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableHeaderCallableParameter>::encode(
    const StableHeaderCallableParameter& value) {
  identity::CanonicalEncoder record;
  value.key().encode(record);
  value.record().encode(record);
  encodeHeaderSite(record, value.site());
  ZC_IF_SOME(name, value.name()) {
    record.encodeUint8(0x01);
    name.encode(record);
  } else {
    record.encodeUint8(0x00);
  }
  value.position().encode(record);
  const auto bytes = record.finish();
  return withDomain("zom.binder.header-callable-parameter"_zc, bytes.asPtr());
}

zc::Maybe<StableHeaderCallableParameter> StableBindingCodec<StableHeaderCallableParameter>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.header-callable-parameter"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto keyBytes = decoder.decodeBytes(32);
  auto record = decodeCallableRecord(decoder);
  auto site = decodeHeaderSite(decoder);
  auto nameTag = decoder.decodeUint8();
  if (keyBytes == zc::none || record == zc::none || site == zc::none || nameTag == zc::none) {
    return zc::none;
  }
  zc::Maybe<identity::DeclaredDefinitionName> name;
  if (ZC_ASSERT_NONNULL(nameTag) == 0x01) {
    name = identity::DeclaredDefinitionName::decodeCanonical(decoder);
    if (name == zc::none) { return zc::none; }
  } else if (ZC_ASSERT_NONNULL(nameTag) != 0x00) {
    return zc::none;
  }
  auto position = decodeCallablePosition(decoder);
  auto key = identity::CallableParameterKey::fromBytes(ZC_ASSERT_NONNULL(keyBytes).asPtr());
  if (position == zc::none || key == zc::none || !decoder.finished()) { return zc::none; }
  auto result = StableHeaderCallableParameter::from(
      zc::mv(ZC_ASSERT_NONNULL(key)), zc::mv(ZC_ASSERT_NONNULL(record)),
      zc::mv(ZC_ASSERT_NONNULL(site)), zc::mv(name), ZC_ASSERT_NONNULL(position));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableDefinitionHeader>::encode(
    const StableDefinitionHeader& value) {
  identity::CanonicalEncoder record;
  const auto queryKey = value.queryKey().encodeCanonical();
  const auto identityRecord = value.record().encode();
  record.encodeByteString(queryKey.asPtr());
  record.encodeByteString(identityRecord.asPtr());
  value.authoritySite().encode(record);
  record.encodeUint8(static_cast<uint8_t>(value.kind()));
  record.encodeUint8(static_cast<uint8_t>(value.nameSpace()));
  value.name().encode(record);
  record.encodeUint8(static_cast<uint8_t>(value.activation()));
  ZC_IF_SOME(visibility, value.visibility()) {
    record.encodeUint8(0x01);
    record.encodeUint8(static_cast<uint8_t>(visibility));
  } else {
    record.encodeUint8(0x00);
  }
  record.encodeUint8(static_cast<uint8_t>(value.bodyDisposition()));
  encodeSequence(record, value.genericParameters());
  encodeSequence(record, value.callableParameters());
  encodeSequence(record, value.declaredScopeRoles());
  const auto bytes = record.finish();
  return withDomain("zom.binder.definition-header"_zc, bytes.asPtr());
}

zc::Maybe<StableDefinitionHeader> StableBindingCodec<StableDefinitionHeader>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.definition-header"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto queryKeyBytes = decoder.decodeByteString(kMaximumRoutedKeyBytes);
  auto identityRecordBytes = decoder.decodeByteString(kMaximumIdentityRecordBytes);
  if (queryKeyBytes == zc::none || identityRecordBytes == zc::none) { return zc::none; }
  auto queryKey =
      StableDefinitionQueryKey::decodeCanonical(ZC_ASSERT_NONNULL(queryKeyBytes).asPtr());
  auto identityRecord = identity::DefinitionIdentityRecord::decodeCanonical(
      ZC_ASSERT_NONNULL(identityRecordBytes).asPtr());
  auto authoritySite = IdentitySyntaxSiteKey::decodeCanonical(decoder);
  auto kind = decoder.decodeUint8();
  auto nameSpace = decoder.decodeUint8();
  auto name = identity::DeclaredDefinitionName::decodeCanonical(decoder);
  auto activation = decoder.decodeUint8();
  auto visibilityTag = decoder.decodeUint8();
  if (queryKey == zc::none || identityRecord == zc::none || authoritySite == zc::none ||
      kind == zc::none || nameSpace == zc::none || name == zc::none || activation == zc::none ||
      visibilityTag == zc::none) {
    return zc::none;
  }
  zc::Maybe<MemberVisibility> visibility;
  if (ZC_ASSERT_NONNULL(visibilityTag) == 0x01) {
    auto value = decoder.decodeUint8();
    if (value == zc::none) { return zc::none; }
    visibility = static_cast<MemberVisibility>(ZC_ASSERT_NONNULL(value));
  } else if (ZC_ASSERT_NONNULL(visibilityTag) != 0x00) {
    return zc::none;
  }
  auto bodyDisposition = decoder.decodeUint8();
  auto genericParameters = decodeSequence<StableHeaderGenericParameter>(decoder);
  auto callableParameters = decodeSequence<StableHeaderCallableParameter>(decoder);
  auto declaredScopeRoles = decodeSequence<ScopeRole>(decoder);
  if (bodyDisposition == zc::none || genericParameters == zc::none ||
      callableParameters == zc::none || declaredScopeRoles == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto result = StableDefinitionHeader::from(
      zc::mv(ZC_ASSERT_NONNULL(queryKey)), zc::mv(ZC_ASSERT_NONNULL(identityRecord)),
      zc::mv(ZC_ASSERT_NONNULL(authoritySite)),
      static_cast<identity::DefinitionKind>(ZC_ASSERT_NONNULL(kind)),
      static_cast<Namespace>(ZC_ASSERT_NONNULL(nameSpace)), zc::mv(ZC_ASSERT_NONNULL(name)),
      static_cast<DefinitionActivation>(ZC_ASSERT_NONNULL(activation)), zc::mv(visibility),
      static_cast<DefinitionBodyDisposition>(ZC_ASSERT_NONNULL(bodyDisposition)),
      zc::mv(ZC_ASSERT_NONNULL(genericParameters)), zc::mv(ZC_ASSERT_NONNULL(callableParameters)),
      zc::mv(ZC_ASSERT_NONNULL(declaredScopeRoles)));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableImplementationOccurrenceHeader>::encode(
    const StableImplementationOccurrenceHeader& value) {
  identity::CanonicalEncoder record;
  const auto queryKey = value.queryKey().encodeCanonical();
  const auto authority = value.authority().encodeCanonical();
  const auto identityRecord = value.record().encode();
  record.encodeByteString(queryKey.asPtr());
  record.encodeByteString(authority.asPtr());
  record.encodeByteString(identityRecord.asPtr());
  encodeSequence(record, value.genericParameters());
  encodeSequence(record, value.declaredScopeRoles());
  record.encodeUint8(static_cast<uint8_t>(value.sourceForm()));
  const auto bytes = record.finish();
  return withDomain("zom.binder.implementation-occurrence-header"_zc, bytes.asPtr());
}

zc::Maybe<StableImplementationOccurrenceHeader> StableBindingCodec<
    StableImplementationOccurrenceHeader>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.implementation-occurrence-header"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto queryKeyBytes = decoder.decodeByteString(kMaximumRoutedKeyBytes);
  auto authorityBytes = decoder.decodeByteString(kMaximumRoutedKeyBytes);
  auto identityRecordBytes = decoder.decodeByteString(kMaximumIdentityRecordBytes);
  if (queryKeyBytes == zc::none || authorityBytes == zc::none || identityRecordBytes == zc::none) {
    return zc::none;
  }
  auto queryKey = StableImplementationOccurrenceQueryKey::decodeCanonical(
      ZC_ASSERT_NONNULL(queryKeyBytes).asPtr());
  auto authority =
      StableImplementationQueryKey::decodeCanonical(ZC_ASSERT_NONNULL(authorityBytes).asPtr());
  auto identityRecord =
      identity::ImplIdentityRecord::decodeCanonical(ZC_ASSERT_NONNULL(identityRecordBytes).asPtr());
  auto genericParameters = decodeSequence<StableHeaderGenericParameter>(decoder);
  auto declaredScopeRoles = decodeSequence<ScopeRole>(decoder);
  auto sourceForm = decoder.decodeUint8();
  if (queryKey == zc::none || authority == zc::none || identityRecord == zc::none ||
      genericParameters == zc::none || declaredScopeRoles == zc::none || sourceForm == zc::none ||
      !decoder.finished()) {
    return zc::none;
  }
  auto result = StableImplementationOccurrenceHeader::from(
      zc::mv(ZC_ASSERT_NONNULL(queryKey)), zc::mv(ZC_ASSERT_NONNULL(authority)),
      zc::mv(ZC_ASSERT_NONNULL(identityRecord)), zc::mv(ZC_ASSERT_NONNULL(genericParameters)),
      zc::mv(ZC_ASSERT_NONNULL(declaredScopeRoles)),
      static_cast<ImplementationSourceForm>(ZC_ASSERT_NONNULL(sourceForm)));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableScopeOwnerKey>::encode(
    const StableScopeOwnerKey& value) {
  identity::CanonicalEncoder record;
  if (value.value().is<StableModuleScope>()) {
    record.encodeUint8(0x01);
    encodeFrame(record, value.value().get<StableModuleScope>().module.encode().asPtr());
  } else if (value.value().is<StableDefinitionScope>()) {
    const auto& scope = value.value().get<StableDefinitionScope>();
    record.encodeUint8(0x02);
    encodeStableFrame(record, scope.definition);
    record.encodeUint8(static_cast<uint8_t>(scope.role));
  } else if (value.value().is<StableImplementationOccurrenceScope>()) {
    const auto& scope = value.value().get<StableImplementationOccurrenceScope>();
    record.encodeUint8(0x03);
    encodeStableFrame(record, scope.occurrence);
    record.encodeUint8(static_cast<uint8_t>(scope.role));
  } else {
    const auto& scope = value.value().get<StableBodyScope>();
    record.encodeUint8(0x04);
    encodeStableFrame(record, scope.owner);
    encodeFrame(record, scope.path.encode().asPtr());
  }
  const auto bytes = record.finish();
  return withDomain("zom.binder.scope-owner-key"_zc, bytes.asPtr());
}
zc::Maybe<StableScopeOwnerKey> StableBindingCodec<StableScopeOwnerKey>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.scope-owner-key"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto tag = decoder.decodeUint8();
  if (tag == zc::none) { return zc::none; }
  if (ZC_ASSERT_NONNULL(tag) == 0x01) {
    auto module = decodeModuleFrame(decoder);
    return module == zc::none ? zc::none
                              : finishSum<StableScopeOwnerKey>(
                                    StableScopeOwnerKey::module(zc::mv(ZC_ASSERT_NONNULL(module))),
                                    decoder, bytes);
  }
  if (ZC_ASSERT_NONNULL(tag) == 0x02) {
    auto definition = decodeStableFrame<StableDefinitionQueryKey>(decoder);
    auto role = decoder.decodeUint8();
    if (definition == zc::none || role == zc::none) { return zc::none; }
    return finishSum(
        StableScopeOwnerKey::definition(zc::mv(ZC_ASSERT_NONNULL(definition)),
                                        static_cast<ScopeRole>(ZC_ASSERT_NONNULL(role))),
        decoder, bytes);
  }
  if (ZC_ASSERT_NONNULL(tag) == 0x03) {
    auto occurrence = decodeStableFrame<StableImplementationOccurrenceQueryKey>(decoder);
    auto role = decoder.decodeUint8();
    if (occurrence == zc::none || role == zc::none) { return zc::none; }
    return finishSum(
        StableScopeOwnerKey::implementationOccurrence(
            zc::mv(ZC_ASSERT_NONNULL(occurrence)), static_cast<ScopeRole>(ZC_ASSERT_NONNULL(role))),
        decoder, bytes);
  }
  if (ZC_ASSERT_NONNULL(tag) != 0x04) { return zc::none; }
  auto owner = decodeStableFrame<StableOwnerBodyQueryKey>(decoder);
  auto path = decodeLocalFrame<LocalSyntaxPath>(decoder);
  return owner == zc::none || path == zc::none
             ? zc::none
             : finishSum<StableScopeOwnerKey>(
                   StableScopeOwnerKey::body(zc::mv(ZC_ASSERT_NONNULL(owner)),
                                             zc::mv(ZC_ASSERT_NONNULL(path))),
                   decoder, bytes);
}
zc::Array<uint8_t> StableBindingCodec<StableNodeSyntaxRoot>::encode(
    const StableNodeSyntaxRoot& value) {
  identity::CanonicalEncoder record;
  if (value.value().is<StableModuleBodySyntaxRoot>()) {
    record.encodeUint8(0x01);
    encodeFrame(record, value.value().get<StableModuleBodySyntaxRoot>().module.encode().asPtr());
  } else if (value.value().is<StableDefinitionHeaderSyntaxRoot>()) {
    record.encodeUint8(0x02);
    encodeStableFrame(record, value.value().get<StableDefinitionHeaderSyntaxRoot>().definition);
  } else {
    record.encodeUint8(0x03);
    encodeStableFrame(record, value.value().get<StableImplementationHeaderSyntaxRoot>().occurrence);
  }
  const auto bytes = record.finish();
  return withDomain("zom.binder.node-syntax-root"_zc, bytes.asPtr());
}
zc::Maybe<StableNodeSyntaxRoot> StableBindingCodec<StableNodeSyntaxRoot>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.node-syntax-root"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto tag = decoder.decodeUint8();
  if (tag == zc::none) { return zc::none; }
  if (ZC_ASSERT_NONNULL(tag) == 0x01) {
    auto module = decodeModuleFrame(decoder);
    return module == zc::none
               ? zc::none
               : finishSum<StableNodeSyntaxRoot>(
                     StableNodeSyntaxRoot::moduleBody(zc::mv(ZC_ASSERT_NONNULL(module))), decoder,
                     bytes);
  }
  if (ZC_ASSERT_NONNULL(tag) == 0x02) {
    auto definition = decodeStableFrame<StableDefinitionQueryKey>(decoder);
    return definition == zc::none
               ? zc::none
               : finishSum<StableNodeSyntaxRoot>(
                     StableNodeSyntaxRoot::definitionHeader(zc::mv(ZC_ASSERT_NONNULL(definition))),
                     decoder, bytes);
  }
  if (ZC_ASSERT_NONNULL(tag) != 0x03) { return zc::none; }
  auto occurrence = decodeStableFrame<StableImplementationOccurrenceQueryKey>(decoder);
  return occurrence == zc::none
             ? zc::none
             : finishSum<StableNodeSyntaxRoot>(StableNodeSyntaxRoot::implementationHeader(
                                                   zc::mv(ZC_ASSERT_NONNULL(occurrence))),
                                               decoder, bytes);
}

zc::Array<uint8_t> StableBindingCodec<StableScopeFact>::encode(const StableScopeFact& value) {
  identity::CanonicalEncoder record;
  encodeStableFrame(record, value.owner());
  ZC_IF_SOME(parent, value.parent()) {
    record.encodeUint8(0x01);
    encodeStableFrame(record, parent);
  } else {
    record.encodeUint8(0x00);
  }
  record.encodeUint8(static_cast<uint8_t>(value.kind()));
  const auto bytes = record.finish();
  return withDomain("zom.binder.skeleton-scope"_zc, bytes.asPtr());
}

zc::Maybe<StableScopeFact> StableBindingCodec<StableScopeFact>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.skeleton-scope"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto owner = decodeStableFrame<StableScopeOwnerKey>(decoder);
  auto parentTag = decoder.decodeUint8();
  if (owner == zc::none || parentTag == zc::none || ZC_ASSERT_NONNULL(parentTag) > 0x01) {
    return zc::none;
  }
  zc::Maybe<StableScopeOwnerKey> parent;
  if (ZC_ASSERT_NONNULL(parentTag) == 0x01) {
    parent = decodeStableFrame<StableScopeOwnerKey>(decoder);
    if (parent == zc::none) { return zc::none; }
  }
  auto kind = decoder.decodeUint8();
  if (kind == zc::none || !decoder.finished()) { return zc::none; }
  auto result = StableScopeFact::from(zc::mv(ZC_ASSERT_NONNULL(owner)), zc::mv(parent),
                                      static_cast<ScopeKind>(ZC_ASSERT_NONNULL(kind)));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableNodeScopeFact>::encode(
    const StableNodeScopeFact& value) {
  identity::CanonicalEncoder record;
  encodeStableFrame(record, value.root());
  encodeFrame(record, value.nodePath().encode().asPtr());
  encodeStableFrame(record, value.scope());
  const auto bytes = record.finish();
  return withDomain("zom.binder.skeleton-node-scope"_zc, bytes.asPtr());
}

zc::Maybe<StableNodeScopeFact> StableBindingCodec<StableNodeScopeFact>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.skeleton-node-scope"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto root = decodeStableFrame<StableNodeSyntaxRoot>(decoder);
  auto nodePath = decodeLocalFrame<LocalSyntaxPath>(decoder);
  auto scope = decodeStableFrame<StableScopeOwnerKey>(decoder);
  if (root == zc::none || nodePath == zc::none || scope == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto result = StableNodeScopeFact::from(zc::mv(ZC_ASSERT_NONNULL(root)),
                                          zc::mv(ZC_ASSERT_NONNULL(nodePath)),
                                          zc::mv(ZC_ASSERT_NONNULL(scope)));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableBodyScopeFact>::encode(
    const StableBodyScopeFact& value) {
  identity::CanonicalEncoder record;
  encodeStableFrame(record, value.owner());
  encodeStableFrame(record, value.scope());
  encodeStableFrame(record, value.parent());
  record.encodeUint8(static_cast<uint8_t>(value.kind()));
  const auto bytes = record.finish();
  return withDomain("zom.binder.body-scope"_zc, bytes.asPtr());
}

zc::Maybe<StableBodyScopeFact> StableBindingCodec<StableBodyScopeFact>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.body-scope"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto owner = decodeStableFrame<StableOwnerBodyQueryKey>(decoder);
  auto scope = decodeStableFrame<StableScopeOwnerKey>(decoder);
  auto parent = decodeStableFrame<StableScopeOwnerKey>(decoder);
  auto kind = decoder.decodeUint8();
  if (owner == zc::none || scope == zc::none || parent == zc::none || kind == zc::none ||
      !decoder.finished()) {
    return zc::none;
  }
  auto result = StableBodyScopeFact::from(
      zc::mv(ZC_ASSERT_NONNULL(owner)), zc::mv(ZC_ASSERT_NONNULL(scope)),
      zc::mv(ZC_ASSERT_NONNULL(parent)), static_cast<ScopeKind>(ZC_ASSERT_NONNULL(kind)));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableBodyNodeScopeFact>::encode(
    const StableBodyNodeScopeFact& value) {
  identity::CanonicalEncoder record;
  encodeStableFrame(record, value.owner());
  encodeFrame(record, value.nodePath().encode().asPtr());
  encodeStableFrame(record, value.scope());
  const auto bytes = record.finish();
  return withDomain("zom.binder.body-node-scope"_zc, bytes.asPtr());
}

zc::Maybe<StableBodyNodeScopeFact> StableBindingCodec<StableBodyNodeScopeFact>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.body-node-scope"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto owner = decodeStableFrame<StableOwnerBodyQueryKey>(decoder);
  auto nodePath = decodeLocalFrame<LocalSyntaxPath>(decoder);
  auto scope = decodeStableFrame<StableScopeOwnerKey>(decoder);
  if (owner == zc::none || nodePath == zc::none || scope == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto result = StableBodyNodeScopeFact::from(zc::mv(ZC_ASSERT_NONNULL(owner)),
                                              zc::mv(ZC_ASSERT_NONNULL(nodePath)),
                                              zc::mv(ZC_ASSERT_NONNULL(scope)));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableOwnerLocalBindingFact>::encode(
    const StableOwnerLocalBindingFact& value) {
  identity::CanonicalEncoder record;
  encodeStableFrame(record, value.owner());
  encodeFrame(record, value.key().encode().asPtr());
  record.encodeUint8(static_cast<uint8_t>(value.kind()));
  value.name().encode(record);
  record.encodeUint8(static_cast<uint8_t>(value.nameSpace()));
  encodeStableFrame(record, value.declaringScope());
  record.encodeUint8(static_cast<uint8_t>(value.activation()));
  const auto bytes = record.finish();
  return withDomain("zom.binder.body-owner-local-binding"_zc, bytes.asPtr());
}

zc::Maybe<StableOwnerLocalBindingFact> StableBindingCodec<StableOwnerLocalBindingFact>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.body-owner-local-binding"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto owner = decodeStableFrame<StableOwnerBodyQueryKey>(decoder);
  auto key = decodeLocalFrame<OwnerLocalBindingKey>(decoder);
  auto kind = decoder.decodeUint8();
  auto name = identity::DeclaredDefinitionName::decodeCanonical(decoder);
  auto nameSpace = decoder.decodeUint8();
  auto declaringScope = decodeStableFrame<StableScopeOwnerKey>(decoder);
  auto activation = decoder.decodeUint8();
  if (owner == zc::none || key == zc::none || kind == zc::none || name == zc::none ||
      nameSpace == zc::none || declaringScope == zc::none || activation == zc::none ||
      !decoder.finished()) {
    return zc::none;
  }
  auto result = StableOwnerLocalBindingFact::from(
      zc::mv(ZC_ASSERT_NONNULL(owner)), zc::mv(ZC_ASSERT_NONNULL(key)),
      static_cast<OwnerLocalBindingKind>(ZC_ASSERT_NONNULL(kind)), zc::mv(ZC_ASSERT_NONNULL(name)),
      static_cast<Namespace>(ZC_ASSERT_NONNULL(nameSpace)),
      zc::mv(ZC_ASSERT_NONNULL(declaringScope)),
      static_cast<DefinitionActivation>(ZC_ASSERT_NONNULL(activation)));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableResolutionFact>::encode(
    const StableResolutionFact& value) {
  identity::CanonicalEncoder record;
  encodeStableFrame(record, value.owner());
  encodeFrame(record, value.usePath().encode().asPtr());
  record.encodeUint8(static_cast<uint8_t>(value.nameSpace()));
  encodeStableFrame(record, value.binding());
  encodeStableFrame(record, value.canonicalTarget());
  record.encodeUint8(static_cast<uint8_t>(value.origin()));
  const auto bytes = record.finish();
  return withDomain("zom.binder.body-resolution"_zc, bytes.asPtr());
}

zc::Maybe<StableResolutionFact> StableBindingCodec<StableResolutionFact>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.body-resolution"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto owner = decodeStableFrame<StableOwnerBodyQueryKey>(decoder);
  auto usePath = decodeLocalFrame<LocalSyntaxPath>(decoder);
  auto nameSpace = decoder.decodeUint8();
  auto binding = decodeStableFrame<StableBindingTargetKey>(decoder);
  auto canonicalTarget = decodeStableFrame<StableBindingTargetKey>(decoder);
  auto origin = decoder.decodeUint8();
  if (owner == zc::none || usePath == zc::none || nameSpace == zc::none || binding == zc::none ||
      canonicalTarget == zc::none || origin == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto result = StableResolutionFact::from(
      zc::mv(ZC_ASSERT_NONNULL(owner)), zc::mv(ZC_ASSERT_NONNULL(usePath)),
      static_cast<Namespace>(ZC_ASSERT_NONNULL(nameSpace)), zc::mv(ZC_ASSERT_NONNULL(binding)),
      zc::mv(ZC_ASSERT_NONNULL(canonicalTarget)),
      static_cast<BindingOrigin>(ZC_ASSERT_NONNULL(origin)));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<LocalSyntaxPath>::encode(const LocalSyntaxPath& value) {
  return value.encode();
}

zc::Maybe<LocalSyntaxPath> StableBindingCodec<LocalSyntaxPath>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() > kMaximumBinderValueBytes) { return zc::none; }
  return LocalSyntaxPath::decodeCanonical(bytes);
}

zc::Array<uint8_t> StableBindingCodec<StableDeferredMemberFact>::encode(
    const StableDeferredMemberFact& value) {
  identity::CanonicalEncoder record;
  encodeStableFrame(record, value.owner());
  encodeFrame(record, value.usePath().encode().asPtr());
  encodeFrame(record, value.basePath().encode().asPtr());
  record.encodeUint8(static_cast<uint8_t>(value.accessKind()));
  value.member().encode(record);
  encodeNonEmptySequence(record, value.expectedNamespaces());
  encodeSequence(record, value.genericArgumentPaths());
  const auto bytes = record.finish();
  return withDomain("zom.binder.body-deferred-member"_zc, bytes.asPtr());
}

zc::Maybe<StableDeferredMemberFact> StableBindingCodec<StableDeferredMemberFact>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.body-deferred-member"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto owner = decodeStableFrame<StableOwnerBodyQueryKey>(decoder);
  auto usePath = decodeLocalFrame<LocalSyntaxPath>(decoder);
  auto basePath = decodeLocalFrame<LocalSyntaxPath>(decoder);
  auto accessKind = decoder.decodeUint8();
  auto member = identity::DeclaredDefinitionName::decodeCanonical(decoder);
  auto expectedNamespaces = decodeNonEmptySequence<Namespace>(decoder);
  auto genericArgumentPaths = decodeSequence<LocalSyntaxPath>(decoder);
  if (owner == zc::none || usePath == zc::none || basePath == zc::none || accessKind == zc::none ||
      member == zc::none || expectedNamespaces == zc::none || genericArgumentPaths == zc::none ||
      !decoder.finished()) {
    return zc::none;
  }
  auto result = StableDeferredMemberFact::from(
      zc::mv(ZC_ASSERT_NONNULL(owner)), zc::mv(ZC_ASSERT_NONNULL(usePath)),
      zc::mv(ZC_ASSERT_NONNULL(basePath)),
      static_cast<MemberAccessKind>(ZC_ASSERT_NONNULL(accessKind)),
      zc::mv(ZC_ASSERT_NONNULL(member)), zc::mv(ZC_ASSERT_NONNULL(expectedNamespaces)),
      zc::mv(ZC_ASSERT_NONNULL(genericArgumentPaths)));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableSelfOwner>::encode(const StableSelfOwner& value) {
  identity::CanonicalEncoder record;
  if (value.value().is<StableNominalSelfOwner>()) {
    record.encodeUint8(0x01);
    encodeStableFrame(record, value.value().get<StableNominalSelfOwner>().definition);
  } else if (value.value().is<StableInterfaceSelfOwner>()) {
    record.encodeUint8(0x02);
    encodeStableFrame(record, value.value().get<StableInterfaceSelfOwner>().definition);
  } else {
    record.encodeUint8(0x03);
    encodeStableFrame(record,
                      value.value().get<StableImplementationOccurrenceSelfOwner>().occurrence);
  }
  return withDomain("zom.binder.self-owner"_zc, record.finish().asPtr());
}

zc::Maybe<StableSelfOwner> StableBindingCodec<StableSelfOwner>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.self-owner"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto tag = decoder.decodeUint8();
  if (tag == zc::none) { return zc::none; }
  if (ZC_ASSERT_NONNULL(tag) == 0x01 || ZC_ASSERT_NONNULL(tag) == 0x02) {
    auto definition = decodeStableFrame<StableDefinitionQueryKey>(decoder);
    if (definition == zc::none) { return zc::none; }
    auto result = ZC_ASSERT_NONNULL(tag) == 0x01
                      ? StableSelfOwner::nominal(zc::mv(ZC_ASSERT_NONNULL(definition)))
                      : StableSelfOwner::interface(zc::mv(ZC_ASSERT_NONNULL(definition)));
    return finishSum<StableSelfOwner>(zc::mv(result), decoder, bytes);
  }
  if (ZC_ASSERT_NONNULL(tag) == 0x03) {
    auto occurrence = decodeStableFrame<StableImplementationOccurrenceQueryKey>(decoder);
    return occurrence == zc::none
               ? zc::none
               : finishSum<StableSelfOwner>(StableSelfOwner::implementationOccurrence(
                                                zc::mv(ZC_ASSERT_NONNULL(occurrence))),
                                            decoder, bytes);
  }
  return zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableSelfTypeFact>::encode(const StableSelfTypeFact& value) {
  identity::CanonicalEncoder record;
  encodeStableFrame(record, value.owner());
  encodeFrame(record, value.syntaxPath().encode().asPtr());
  encodeStableFrame(record, value.selfOwner());
  return withDomain("zom.binder.body-self-type"_zc, record.finish().asPtr());
}

zc::Maybe<StableSelfTypeFact> StableBindingCodec<StableSelfTypeFact>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.body-self-type"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto owner = decodeStableFrame<StableOwnerBodyQueryKey>(decoder);
  auto syntaxPath = decodeLocalFrame<LocalSyntaxPath>(decoder);
  auto selfOwner = decodeStableFrame<StableSelfOwner>(decoder);
  if (owner == zc::none || syntaxPath == zc::none || selfOwner == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto result = StableSelfTypeFact::from(zc::mv(ZC_ASSERT_NONNULL(owner)),
                                         zc::mv(ZC_ASSERT_NONNULL(syntaxPath)),
                                         zc::mv(ZC_ASSERT_NONNULL(selfOwner)));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableThisBindingFact>::encode(
    const StableThisBindingFact& value) {
  identity::CanonicalEncoder record;
  encodeStableFrame(record, value.owner());
  encodeFrame(record, value.expressionPath().encode().asPtr());
  encodeStableFrame(record, value.receiver());
  return withDomain("zom.binder.body-this-binding"_zc, record.finish().asPtr());
}

zc::Maybe<StableThisBindingFact> StableBindingCodec<StableThisBindingFact>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.body-this-binding"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto owner = decodeStableFrame<StableOwnerBodyQueryKey>(decoder);
  auto expressionPath = decodeLocalFrame<LocalSyntaxPath>(decoder);
  auto receiver = decodeStableFrame<StableCallableParameterQueryKey>(decoder);
  if (owner == zc::none || expressionPath == zc::none || receiver == zc::none ||
      !decoder.finished()) {
    return zc::none;
  }
  auto result = StableThisBindingFact::from(zc::mv(ZC_ASSERT_NONNULL(owner)),
                                            zc::mv(ZC_ASSERT_NONNULL(expressionPath)),
                                            zc::mv(ZC_ASSERT_NONNULL(receiver)));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableShadowTargetFact>::encode(
    const StableShadowTargetFact& value) {
  identity::CanonicalEncoder record;
  encodeStableFrame(record, value.owner());
  encodeStableFrame(record, value.binding());
  encodeStableFrame(record, value.shadowed());
  return withDomain("zom.binder.body-shadow-target"_zc, record.finish().asPtr());
}

zc::Maybe<StableShadowTargetFact> StableBindingCodec<StableShadowTargetFact>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.body-shadow-target"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto owner = decodeStableFrame<StableOwnerBodyQueryKey>(decoder);
  auto binding = decodeStableFrame<StableBindingTargetKey>(decoder);
  auto shadowed = decodeStableFrame<StableBindingTargetKey>(decoder);
  if (owner == zc::none || binding == zc::none || shadowed == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto result = StableShadowTargetFact::from(
      zc::mv(ZC_ASSERT_NONNULL(owner)), zc::mv(ZC_ASSERT_NONNULL(binding)),
      zc::mv(ZC_ASSERT_NONNULL(shadowed)));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableLabelKey>::encode(const StableLabelKey& value) {
  identity::CanonicalEncoder record;
  encodeStableFrame(record, value.owner());
  encodeFrame(record, value.declarationPath().encode().asPtr());
  return withDomain("zom.binder.label-key"_zc, record.finish().asPtr());
}

zc::Maybe<StableLabelKey> StableBindingCodec<StableLabelKey>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.label-key"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto owner = decodeStableFrame<StableOwnerBodyQueryKey>(decoder);
  auto declarationPath = decodeLocalFrame<LocalSyntaxPath>(decoder);
  if (owner == zc::none || declarationPath == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto result = StableLabelKey::from(zc::mv(ZC_ASSERT_NONNULL(owner)),
                                     zc::mv(ZC_ASSERT_NONNULL(declarationPath)));
  return encode(result).asPtr() == bytes ? zc::mv(result) : zc::Maybe<StableLabelKey>();
}

zc::Array<uint8_t> StableBindingCodec<StableLabelTarget>::encode(
    const StableLabelTarget& value) {
  identity::CanonicalEncoder record;
  record.encodeUint8(value.value().is<StableBlockLabelTarget>() ? 0x01 : 0x02);
  encodeStableFrame(record, value.scope());
  return withDomain("zom.binder.label-target"_zc, record.finish().asPtr());
}

zc::Maybe<StableLabelTarget> StableBindingCodec<StableLabelTarget>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.label-target"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto tag = decoder.decodeUint8();
  auto scope = decodeStableFrame<StableScopeOwnerKey>(decoder);
  if (tag == zc::none || scope == zc::none || !decoder.finished()) { return zc::none; }
  if (ZC_ASSERT_NONNULL(tag) != 0x01 && ZC_ASSERT_NONNULL(tag) != 0x02) { return zc::none; }
  StableLabelTarget result =
      ZC_ASSERT_NONNULL(tag) == 0x01
          ? StableLabelTarget::block(zc::mv(ZC_ASSERT_NONNULL(scope)))
          : StableLabelTarget::loop(zc::mv(ZC_ASSERT_NONNULL(scope)));
  return encode(result).asPtr() == bytes ? zc::Maybe<StableLabelTarget>(zc::mv(result)) : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableLabelFact>::encode(const StableLabelFact& value) {
  identity::CanonicalEncoder record;
  encodeStableFrame(record, value.key());
  value.name().encode(record);
  encodeFrame(record, value.statementPath().encode().asPtr());
  encodeStableFrame(record, value.target());
  return withDomain("zom.binder.body-label"_zc, record.finish().asPtr());
}

zc::Maybe<StableLabelFact> StableBindingCodec<StableLabelFact>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.body-label"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto key = decodeStableFrame<StableLabelKey>(decoder);
  auto name = identity::DeclaredDefinitionName::decodeCanonical(decoder);
  auto statementPath = decodeLocalFrame<LocalSyntaxPath>(decoder);
  auto target = decodeStableFrame<StableLabelTarget>(decoder);
  if (key == zc::none || name == zc::none || statementPath == zc::none || target == zc::none ||
      !decoder.finished()) {
    return zc::none;
  }
  auto result = StableLabelFact::from(
      zc::mv(ZC_ASSERT_NONNULL(key)), zc::mv(ZC_ASSERT_NONNULL(name)),
      zc::mv(ZC_ASSERT_NONNULL(statementPath)), zc::mv(ZC_ASSERT_NONNULL(target)));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableControlTarget>::encode(
    const StableControlTarget& value) {
  identity::CanonicalEncoder record;
  if (value.value().is<StableExplicitLabelControlTarget>()) {
    record.encodeUint8(0x01);
    encodeStableFrame(record, value.value().get<StableExplicitLabelControlTarget>().label);
  } else if (value.value().is<StableLoopControlTarget>()) {
    record.encodeUint8(0x02);
    encodeStableFrame(record, value.value().get<StableLoopControlTarget>().scope);
  } else {
    record.encodeUint8(0x03);
    encodeStableFrame(record, value.value().get<StableMatchControlTarget>().scope);
  }
  return withDomain("zom.binder.control-target"_zc, record.finish().asPtr());
}

zc::Maybe<StableControlTarget> StableBindingCodec<StableControlTarget>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.control-target"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto tag = decoder.decodeUint8();
  if (tag == zc::none) { return zc::none; }
  zc::Maybe<StableControlTarget> result;
  if (ZC_ASSERT_NONNULL(tag) == 0x01) {
    auto label = decodeStableFrame<StableLabelKey>(decoder);
    if (label != zc::none) {
      result = StableControlTarget::explicitLabel(zc::mv(ZC_ASSERT_NONNULL(label)));
    }
  } else if (ZC_ASSERT_NONNULL(tag) == 0x02 || ZC_ASSERT_NONNULL(tag) == 0x03) {
    auto scope = decodeStableFrame<StableScopeOwnerKey>(decoder);
    if (scope != zc::none) {
      result = ZC_ASSERT_NONNULL(tag) == 0x02
                   ? StableControlTarget::loop(zc::mv(ZC_ASSERT_NONNULL(scope)))
                   : StableControlTarget::match(zc::mv(ZC_ASSERT_NONNULL(scope)));
    }
  }
  return result != zc::none && decoder.finished() &&
                 encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes
             ? zc::mv(result)
             : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableControlTransferFact>::encode(
    const StableControlTransferFact& value) {
  identity::CanonicalEncoder record;
  encodeStableFrame(record, value.owner());
  encodeFrame(record, value.transferPath().encode().asPtr());
  record.encodeUint8(static_cast<uint8_t>(value.kind()));
  encodeStableFrame(record, value.target());
  return withDomain("zom.binder.body-control-transfer"_zc, record.finish().asPtr());
}

zc::Maybe<StableControlTransferFact> StableBindingCodec<StableControlTransferFact>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.body-control-transfer"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto owner = decodeStableFrame<StableOwnerBodyQueryKey>(decoder);
  auto transferPath = decodeLocalFrame<LocalSyntaxPath>(decoder);
  auto kind = decoder.decodeUint8();
  auto target = decodeStableFrame<StableControlTarget>(decoder);
  if (owner == zc::none || transferPath == zc::none || kind == zc::none || target == zc::none ||
      !decoder.finished()) {
    return zc::none;
  }
  auto result = StableControlTransferFact::from(
      zc::mv(ZC_ASSERT_NONNULL(owner)), zc::mv(ZC_ASSERT_NONNULL(transferPath)),
      static_cast<ControlTransferKind>(ZC_ASSERT_NONNULL(kind)),
      zc::mv(ZC_ASSERT_NONNULL(target)));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableClosureFact>::encode(
    const StableClosureFact& value) {
  identity::CanonicalEncoder record;
  encodeStableFrame(record, value.owner());
  encodeFrame(record, value.closure().encode().asPtr());
  encodeStableFrame(record, value.scope());
  return withDomain("zom.binder.body-closure"_zc, record.finish().asPtr());
}

zc::Maybe<StableClosureFact> StableBindingCodec<StableClosureFact>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.body-closure"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto owner = decodeStableFrame<StableOwnerBodyQueryKey>(decoder);
  auto closure = decodeLocalFrame<AnonymousOwnerLocalKey>(decoder);
  auto scope = decodeStableFrame<StableScopeOwnerKey>(decoder);
  if (owner == zc::none || closure == zc::none || scope == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto result = StableClosureFact::from(zc::mv(ZC_ASSERT_NONNULL(owner)),
                                        zc::mv(ZC_ASSERT_NONNULL(closure)),
                                        zc::mv(ZC_ASSERT_NONNULL(scope)));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableClosureFreeVariable>::encode(
    const StableClosureFreeVariable& value) {
  identity::CanonicalEncoder record;
  encodeStableFrame(record, value.target());
  encodeNonEmptySequence(record, value.referencePaths());
  return withDomain("zom.binder.closure-free-variable"_zc, record.finish().asPtr());
}

zc::Maybe<StableClosureFreeVariable> StableBindingCodec<StableClosureFreeVariable>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.closure-free-variable"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto target = decodeStableFrame<StableBindingTargetKey>(decoder);
  auto referencePaths = decodeNonEmptySequence<LocalSyntaxPath>(decoder);
  if (target == zc::none || referencePaths == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto result = StableClosureFreeVariable::from(
      zc::mv(ZC_ASSERT_NONNULL(target)), zc::mv(ZC_ASSERT_NONNULL(referencePaths)));
  return encode(result).asPtr() == bytes ? zc::Maybe<StableClosureFreeVariable>(zc::mv(result))
                                        : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableClosureFreeVariableFact>::encode(
    const StableClosureFreeVariableFact& value) {
  identity::CanonicalEncoder record;
  encodeStableFrame(record, value.owner());
  encodeFrame(record, value.closure().encode().asPtr());
  encodeSequence(record, value.variables());
  return withDomain("zom.binder.body-closure-free-variables"_zc, record.finish().asPtr());
}

zc::Maybe<StableClosureFreeVariableFact>
StableBindingCodec<StableClosureFreeVariableFact>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.body-closure-free-variables"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto owner = decodeStableFrame<StableOwnerBodyQueryKey>(decoder);
  auto closure = decodeLocalFrame<AnonymousOwnerLocalKey>(decoder);
  auto variables = decodeSequence<StableClosureFreeVariable>(decoder);
  if (owner == zc::none || closure == zc::none || variables == zc::none ||
      !decoder.finished()) {
    return zc::none;
  }
  auto result = StableClosureFreeVariableFact::from(
      zc::mv(ZC_ASSERT_NONNULL(owner)), zc::mv(ZC_ASSERT_NONNULL(closure)),
      zc::mv(ZC_ASSERT_NONNULL(variables)));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableExplicitCaptureBindingFact>::encode(
    const StableExplicitCaptureBindingFact& value) {
  identity::CanonicalEncoder record;
  encodeFrame(record, value.itemPath().encode().asPtr());
  encodeStableFrame(record, value.target());
  record.encodeUint8(static_cast<uint8_t>(value.mode()));
  return withDomain("zom.binder.explicit-capture-binding"_zc, record.finish().asPtr());
}

zc::Maybe<StableExplicitCaptureBindingFact>
StableBindingCodec<StableExplicitCaptureBindingFact>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.explicit-capture-binding"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto itemPath = decodeLocalFrame<LocalSyntaxPath>(decoder);
  auto target = decodeStableFrame<StableBindingTargetKey>(decoder);
  auto mode = decoder.decodeUint8();
  if (itemPath == zc::none || target == zc::none || mode == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto result = StableExplicitCaptureBindingFact::from(
      zc::mv(ZC_ASSERT_NONNULL(itemPath)), zc::mv(ZC_ASSERT_NONNULL(target)),
      static_cast<StableExplicitCaptureMode>(ZC_ASSERT_NONNULL(mode)));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableExplicitClosureCaptureFact>::encode(
    const StableExplicitClosureCaptureFact& value) {
  identity::CanonicalEncoder record;
  encodeStableFrame(record, value.owner());
  encodeFrame(record, value.closure().encode().asPtr());
  encodeFrame(record, value.captureListPath().encode().asPtr());
  encodeSequence(record, value.captures());
  return withDomain("zom.binder.body-explicit-closure-capture"_zc, record.finish().asPtr());
}

zc::Maybe<StableExplicitClosureCaptureFact>
StableBindingCodec<StableExplicitClosureCaptureFact>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.body-explicit-closure-capture"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto owner = decodeStableFrame<StableOwnerBodyQueryKey>(decoder);
  auto closure = decodeLocalFrame<AnonymousOwnerLocalKey>(decoder);
  auto captureListPath = decodeLocalFrame<LocalSyntaxPath>(decoder);
  auto captures = decodeSequence<StableExplicitCaptureBindingFact>(decoder);
  if (owner == zc::none || closure == zc::none || captureListPath == zc::none ||
      captures == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto result = StableExplicitClosureCaptureFact::from(
      zc::mv(ZC_ASSERT_NONNULL(owner)), zc::mv(ZC_ASSERT_NONNULL(closure)),
      zc::mv(ZC_ASSERT_NONNULL(captureListPath)), zc::mv(ZC_ASSERT_NONNULL(captures)));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableDeclarationFact>::encode(
    const StableDeclarationFact& value) {
  identity::CanonicalEncoder record;
  encodeStableFrame(record, value.queryKey());
  encodeFrame(record, value.record().encode().asPtr());
  encodeStableFrame(record, value.declaringScope());
  record.encodeUint8(static_cast<uint8_t>(value.kind()));
  record.encodeUint8(static_cast<uint8_t>(value.nameSpace()));
  value.name().encode(record);
  record.encodeUint8(static_cast<uint8_t>(value.activation()));
  ZC_IF_SOME(visibility, value.visibility()) {
    record.encodeUint8(0x01);
    record.encodeUint8(static_cast<uint8_t>(visibility));
  } else {
    record.encodeUint8(0x00);
  }
  const auto bytes = record.finish();
  return withDomain("zom.binder.skeleton-declaration"_zc, bytes.asPtr());
}

zc::Maybe<StableDeclarationFact> StableBindingCodec<StableDeclarationFact>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.skeleton-declaration"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto queryKey = decodeStableFrame<StableDefinitionQueryKey>(decoder);
  auto identityRecordBytes = decoder.decodeByteString(kMaximumIdentityRecordBytes);
  auto declaringScope = decodeStableFrame<StableScopeOwnerKey>(decoder);
  if (queryKey == zc::none || identityRecordBytes == zc::none || declaringScope == zc::none) {
    return zc::none;
  }
  auto identityRecord = identity::DefinitionIdentityRecord::decodeCanonical(
      ZC_ASSERT_NONNULL(identityRecordBytes).asPtr());
  auto kind = decoder.decodeUint8();
  auto nameSpace = decoder.decodeUint8();
  auto name = identity::DeclaredDefinitionName::decodeCanonical(decoder);
  auto activation = decoder.decodeUint8();
  auto visibilityTag = decoder.decodeUint8();
  if (identityRecord == zc::none || kind == zc::none || nameSpace == zc::none || name == zc::none ||
      activation == zc::none || visibilityTag == zc::none) {
    return zc::none;
  }
  zc::Maybe<MemberVisibility> visibility;
  if (ZC_ASSERT_NONNULL(visibilityTag) == 0x01) {
    auto value = decoder.decodeUint8();
    if (value == zc::none) { return zc::none; }
    visibility = static_cast<MemberVisibility>(ZC_ASSERT_NONNULL(value));
  } else if (ZC_ASSERT_NONNULL(visibilityTag) != 0x00) {
    return zc::none;
  }
  if (!decoder.finished()) { return zc::none; }
  auto result = StableDeclarationFact::from(
      zc::mv(ZC_ASSERT_NONNULL(queryKey)), zc::mv(ZC_ASSERT_NONNULL(identityRecord)),
      zc::mv(ZC_ASSERT_NONNULL(declaringScope)),
      static_cast<identity::DefinitionKind>(ZC_ASSERT_NONNULL(kind)),
      static_cast<Namespace>(ZC_ASSERT_NONNULL(nameSpace)), zc::mv(ZC_ASSERT_NONNULL(name)),
      static_cast<DefinitionActivation>(ZC_ASSERT_NONNULL(activation)), zc::mv(visibility));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableImplementationOccurrenceFact>::encode(
    const StableImplementationOccurrenceFact& value) {
  identity::CanonicalEncoder record;
  encodeStableFrame(record, value.occurrence());
  encodeStableFrame(record, value.authority());
  encodeFrame(record, value.record().encode().asPtr());
  encodeStableFrame(record, value.declaringScope());
  const auto bytes = record.finish();
  return withDomain("zom.binder.skeleton-implementation-occurrence"_zc, bytes.asPtr());
}

zc::Maybe<StableImplementationOccurrenceFact>
StableBindingCodec<StableImplementationOccurrenceFact>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.skeleton-implementation-occurrence"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto occurrence = decodeStableFrame<StableImplementationOccurrenceQueryKey>(decoder);
  auto authority = decodeStableFrame<StableImplementationQueryKey>(decoder);
  auto identityRecordBytes = decoder.decodeByteString(kMaximumIdentityRecordBytes);
  auto declaringScope = decodeStableFrame<StableScopeOwnerKey>(decoder);
  if (occurrence == zc::none || authority == zc::none || identityRecordBytes == zc::none ||
      declaringScope == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto identityRecord =
      identity::ImplIdentityRecord::decodeCanonical(ZC_ASSERT_NONNULL(identityRecordBytes).asPtr());
  if (identityRecord == zc::none) { return zc::none; }
  auto result = StableImplementationOccurrenceFact::from(
      zc::mv(ZC_ASSERT_NONNULL(occurrence)), zc::mv(ZC_ASSERT_NONNULL(authority)),
      zc::mv(ZC_ASSERT_NONNULL(identityRecord)), zc::mv(ZC_ASSERT_NONNULL(declaringScope)));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableGenericParameterDeclarationFact>::encode(
    const StableGenericParameterDeclarationFact& value) {
  identity::CanonicalEncoder record;
  encodeStableFrame(record, value.queryKey());
  value.record().encode(record);
  encodeHeaderSite(record, value.headerSite());
  encodeStableFrame(record, value.declaringScope());
  value.name().encode(record);
  const auto bytes = record.finish();
  return withDomain("zom.binder.skeleton-generic-parameter-declaration"_zc, bytes.asPtr());
}

zc::Maybe<StableGenericParameterDeclarationFact> StableBindingCodec<
    StableGenericParameterDeclarationFact>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.skeleton-generic-parameter-declaration"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto queryKey = decodeStableFrame<StableGenericParameterQueryKey>(decoder);
  auto identityRecord = decodeGenericRecord(decoder);
  auto headerSite = decodeHeaderSite(decoder);
  auto declaringScope = decodeStableFrame<StableScopeOwnerKey>(decoder);
  auto name = identity::DeclaredDefinitionName::decodeCanonical(decoder);
  if (queryKey == zc::none || identityRecord == zc::none || headerSite == zc::none ||
      declaringScope == zc::none || name == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto result = StableGenericParameterDeclarationFact::from(
      zc::mv(ZC_ASSERT_NONNULL(queryKey)), zc::mv(ZC_ASSERT_NONNULL(identityRecord)),
      zc::mv(ZC_ASSERT_NONNULL(headerSite)), zc::mv(ZC_ASSERT_NONNULL(declaringScope)),
      zc::mv(ZC_ASSERT_NONNULL(name)));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableCallableParameterDeclarationFact>::encode(
    const StableCallableParameterDeclarationFact& value) {
  identity::CanonicalEncoder record;
  encodeStableFrame(record, value.queryKey());
  value.record().encode(record);
  encodeHeaderSite(record, value.headerSite());
  encodeStableFrame(record, value.declaringScope());
  ZC_IF_SOME(name, value.name()) {
    record.encodeUint8(0x01);
    name.encode(record);
  } else {
    record.encodeUint8(0x00);
  }
  const auto bytes = record.finish();
  return withDomain("zom.binder.skeleton-callable-parameter-declaration"_zc, bytes.asPtr());
}

zc::Maybe<StableCallableParameterDeclarationFact> StableBindingCodec<
    StableCallableParameterDeclarationFact>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.skeleton-callable-parameter-declaration"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto queryKey = decodeStableFrame<StableCallableParameterQueryKey>(decoder);
  auto identityRecord = decodeCallableRecord(decoder);
  auto headerSite = decodeHeaderSite(decoder);
  auto declaringScope = decodeStableFrame<StableScopeOwnerKey>(decoder);
  auto nameTag = decoder.decodeUint8();
  if (queryKey == zc::none || identityRecord == zc::none || headerSite == zc::none ||
      declaringScope == zc::none || nameTag == zc::none) {
    return zc::none;
  }
  zc::Maybe<identity::DeclaredDefinitionName> name;
  if (ZC_ASSERT_NONNULL(nameTag) == 0x01) {
    name = identity::DeclaredDefinitionName::decodeCanonical(decoder);
    if (name == zc::none) { return zc::none; }
  } else if (ZC_ASSERT_NONNULL(nameTag) != 0x00) {
    return zc::none;
  }
  if (!decoder.finished()) { return zc::none; }
  auto result = StableCallableParameterDeclarationFact::from(
      zc::mv(ZC_ASSERT_NONNULL(queryKey)), zc::mv(ZC_ASSERT_NONNULL(identityRecord)),
      zc::mv(ZC_ASSERT_NONNULL(headerSite)), zc::mv(ZC_ASSERT_NONNULL(declaringScope)),
      zc::mv(name));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableBindingTargetKey>::encode(
    const StableBindingTargetKey& value) {
  identity::CanonicalEncoder record;
#define ZOM_ENCODE_TARGET(Variant, Tag, Field)                     \
  if (value.value().is<Variant>()) {                               \
    record.encodeUint8(Tag);                                       \
    encodeStableFrame(record, value.value().get<Variant>().Field); \
  } else
  ZOM_ENCODE_TARGET(StableDefinitionBindingTarget, 0x01, definition)
  ZOM_ENCODE_TARGET(StableImplementationBindingTarget, 0x02, implementation)
  if (value.value().is<StableModuleBindingTarget>()) {
    record.encodeUint8(0x03);
    encodeFrame(record, value.value().get<StableModuleBindingTarget>().module.encode().asPtr());
  } else
    ZOM_ENCODE_TARGET(StableSemanticImportBindingTarget, 0x04, import)
  if (value.value().is<StableOwnerLocalBindingTarget>()) {
    const auto& target = value.value().get<StableOwnerLocalBindingTarget>();
    record.encodeUint8(0x05);
    encodeStableFrame(record, target.owner);
    encodeFrame(record, target.binding.encode().asPtr());
  } else if (value.value().is<StableAnonymousOwnerBindingTarget>()) {
    const auto& target = value.value().get<StableAnonymousOwnerBindingTarget>();
    record.encodeUint8(0x06);
    encodeStableFrame(record, target.owner);
    encodeFrame(record, target.binding.encode().asPtr());
  } else
    ZOM_ENCODE_TARGET(StableGenericParameterBindingTarget, 0x07, parameter) {
      record.encodeUint8(0x08);
      encodeStableFrame(record,
                        value.value().get<StableCallableParameterBindingTarget>().parameter);
    }
#undef ZOM_ENCODE_TARGET
  const auto bytes = record.finish();
  return withDomain("zom.binder.binding-target-key"_zc, bytes.asPtr());
}
zc::Maybe<StableBindingTargetKey> StableBindingCodec<StableBindingTargetKey>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.binding-target-key"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto tag = decoder.decodeUint8();
  if (tag == zc::none) { return zc::none; }
#define ZOM_DECODE_TARGET(Tag, Type, Factory)                                                    \
  if (ZC_ASSERT_NONNULL(tag) == Tag) {                                                           \
    auto value = decodeStableFrame<Type>(decoder);                                               \
    return value == zc::none                                                                     \
               ? zc::none                                                                        \
               : finishSum<StableBindingTargetKey>(                                              \
                     StableBindingTargetKey::Factory(zc::mv(ZC_ASSERT_NONNULL(value))), decoder, \
                     bytes);                                                                     \
  }
  ZOM_DECODE_TARGET(0x01, StableDefinitionQueryKey, definition)
  ZOM_DECODE_TARGET(0x02, StableImplementationQueryKey, implementation)
  if (ZC_ASSERT_NONNULL(tag) == 0x03) {
    auto module = decodeModuleFrame(decoder);
    return module == zc::none
               ? zc::none
               : finishSum<StableBindingTargetKey>(
                     StableBindingTargetKey::module(zc::mv(ZC_ASSERT_NONNULL(module))), decoder,
                     bytes);
  }
  ZOM_DECODE_TARGET(0x04, StableSemanticImportQueryKey, semanticImport)
  if (ZC_ASSERT_NONNULL(tag) == 0x05) {
    auto owner = decodeStableFrame<StableOwnerBodyQueryKey>(decoder);
    auto binding = decodeLocalFrame<OwnerLocalBindingKey>(decoder);
    if (owner == zc::none || binding == zc::none) { return zc::none; }
    return finishSum(StableBindingTargetKey::ownerLocal(zc::mv(ZC_ASSERT_NONNULL(owner)),
                                                        zc::mv(ZC_ASSERT_NONNULL(binding))),
                     decoder, bytes);
  }
  if (ZC_ASSERT_NONNULL(tag) == 0x06) {
    auto owner = decodeStableFrame<StableOwnerBodyQueryKey>(decoder);
    auto binding = decodeLocalFrame<AnonymousOwnerLocalKey>(decoder);
    if (owner == zc::none || binding == zc::none) { return zc::none; }
    return finishSum(StableBindingTargetKey::anonymousOwner(zc::mv(ZC_ASSERT_NONNULL(owner)),
                                                            zc::mv(ZC_ASSERT_NONNULL(binding))),
                     decoder, bytes);
  }
  ZOM_DECODE_TARGET(0x07, StableGenericParameterQueryKey, genericParameter)
  ZOM_DECODE_TARGET(0x08, StableCallableParameterQueryKey, callableParameter)
#undef ZOM_DECODE_TARGET
  return zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableImportFact>::encode(const StableImportFact& value) {
  identity::CanonicalEncoder record;
  encodeStableFrame(record, value.queryKey());
  encodeStableFrame(record, value.declaringScope());
  encodeStableFrame(record, value.target());
  encodeStableFrame(record, value.canonicalTarget());
  record.encodeUint8(static_cast<uint8_t>(value.nameSpace()));
  record.encodeUint8(static_cast<uint8_t>(value.origin()));
  ZC_IF_SOME(visibility, value.visibility()) {
    record.encodeUint8(0x01);
    record.encodeUint8(static_cast<uint8_t>(visibility));
  } else {
    record.encodeUint8(0x00);
  }
  record.encodeUint8(value.exported() ? 0x01 : 0x00);
  return withDomain("zom.binder.skeleton-import"_zc, record.finish().asPtr());
}

zc::Maybe<StableImportFact> StableBindingCodec<StableImportFact>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.skeleton-import"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto queryKey = decodeStableFrame<StableSemanticImportQueryKey>(decoder);
  auto declaringScope = decodeStableFrame<StableScopeOwnerKey>(decoder);
  auto target = decodeStableFrame<StableBindingTargetKey>(decoder);
  auto canonicalTarget = decodeStableFrame<StableBindingTargetKey>(decoder);
  auto nameSpace = decoder.decodeUint8();
  auto origin = decoder.decodeUint8();
  auto visibilityTag = decoder.decodeUint8();
  if (queryKey == zc::none || declaringScope == zc::none || target == zc::none ||
      canonicalTarget == zc::none || nameSpace == zc::none || origin == zc::none ||
      visibilityTag == zc::none) {
    return zc::none;
  }
  zc::Maybe<MemberVisibility> visibility;
  if (ZC_ASSERT_NONNULL(visibilityTag) == 0x01) {
    auto value = decoder.decodeUint8();
    if (value == zc::none) { return zc::none; }
    visibility = static_cast<MemberVisibility>(ZC_ASSERT_NONNULL(value));
  } else if (ZC_ASSERT_NONNULL(visibilityTag) != 0x00) {
    return zc::none;
  }
  auto exported = decoder.decodeUint8();
  if (exported == zc::none || ZC_ASSERT_NONNULL(exported) > 0x01 || !decoder.finished()) {
    return zc::none;
  }
  auto result = StableImportFact::from(
      zc::mv(ZC_ASSERT_NONNULL(queryKey)), zc::mv(ZC_ASSERT_NONNULL(declaringScope)),
      zc::mv(ZC_ASSERT_NONNULL(target)), zc::mv(ZC_ASSERT_NONNULL(canonicalTarget)),
      static_cast<Namespace>(ZC_ASSERT_NONNULL(nameSpace)),
      static_cast<BindingOrigin>(ZC_ASSERT_NONNULL(origin)), zc::mv(visibility),
      ZC_ASSERT_NONNULL(exported) == 0x01);
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableModuleAliasFact>::encode(
    const StableModuleAliasFact& value) {
  identity::CanonicalEncoder record;
  encodeStableFrame(record, value.queryKey());
  encodeStableFrame(record, value.declaringScope());
  encodeStableFrame(record, value.alias());
  encodeFrame(record, value.canonicalModule().encode().asPtr());
  record.encodeDigest(value.targetSurfaceRevision().digest());
  return withDomain("zom.binder.skeleton-module-alias"_zc, record.finish().asPtr());
}

zc::Maybe<StableModuleAliasFact> StableBindingCodec<StableModuleAliasFact>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.skeleton-module-alias"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto queryKey = decodeStableFrame<StableSemanticImportQueryKey>(decoder);
  auto declaringScope = decodeStableFrame<StableScopeOwnerKey>(decoder);
  auto alias = decodeStableFrame<StableDefinitionQueryKey>(decoder);
  auto canonicalModule = decodeModuleFrame(decoder);
  auto revisionDigest = decoder.decodeDigest();
  if (queryKey == zc::none || declaringScope == zc::none || alias == zc::none ||
      canonicalModule == zc::none || revisionDigest == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto result = StableModuleAliasFact::from(
      zc::mv(ZC_ASSERT_NONNULL(queryKey)), zc::mv(ZC_ASSERT_NONNULL(declaringScope)),
      zc::mv(ZC_ASSERT_NONNULL(alias)), zc::mv(ZC_ASSERT_NONNULL(canonicalModule)),
      ExportSurfaceRevision::fromDigest(ZC_ASSERT_NONNULL(revisionDigest)));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableReexportStep>::encode(const StableReexportStep& value) {
  identity::CanonicalEncoder record;
  encodeFrame(record, value.module().encode().asPtr());
  encodeFrame(record, value.exportPath().encode().asPtr());
  encodeStableFrame(record, value.binding());
  encodeStableFrame(record, value.canonicalTarget());
  return withDomain("zom.binder.skeleton-reexport-step"_zc, record.finish().asPtr());
}

zc::Maybe<StableReexportStep> StableBindingCodec<StableReexportStep>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.skeleton-reexport-step"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto module = decodeModuleFrame(decoder);
  auto exportPath = decodeLocalFrame<LocalSyntaxPath>(decoder);
  auto binding = decodeStableFrame<StableBindingTargetKey>(decoder);
  auto canonicalTarget = decodeStableFrame<StableBindingTargetKey>(decoder);
  if (module == zc::none || exportPath == zc::none || binding == zc::none ||
      canonicalTarget == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto result = StableReexportStep::from(
      zc::mv(ZC_ASSERT_NONNULL(module)), zc::mv(ZC_ASSERT_NONNULL(exportPath)),
      zc::mv(ZC_ASSERT_NONNULL(binding)), zc::mv(ZC_ASSERT_NONNULL(canonicalTarget)));
  return encode(result).asPtr() == bytes ? zc::Maybe<StableReexportStep>(zc::mv(result)) : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableLocalExportFact>::encode(
    const StableLocalExportFact& value) {
  identity::CanonicalEncoder record;
  encodeFrame(record, value.declaringModule().encode().asPtr());
  encodeFrame(record, value.exportPath().encode().asPtr());
  record.encodeUint8(static_cast<uint8_t>(value.name().nameSpace()));
  value.name().name().encode(record);
  encodeStableFrame(record, value.binding());
  encodeStableFrame(record, value.canonicalTarget());
  ZC_IF_SOME(visibility, value.visibility()) {
    record.encodeUint8(0x01);
    record.encodeUint8(static_cast<uint8_t>(visibility));
  } else {
    record.encodeUint8(0x00);
  }
  encodeSequence(record, value.reexportChain());
  return withDomain("zom.binder.skeleton-local-export"_zc, record.finish().asPtr());
}

zc::Maybe<StableLocalExportFact> StableBindingCodec<StableLocalExportFact>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.skeleton-local-export"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto declaringModule = decodeModuleFrame(decoder);
  auto exportPath = decodeLocalFrame<LocalSyntaxPath>(decoder);
  auto nameSpace = decoder.decodeUint8();
  auto declaredName = identity::DeclaredDefinitionName::decodeCanonical(decoder);
  if (declaringModule == zc::none || exportPath == zc::none || nameSpace == zc::none ||
      declaredName == zc::none) {
    return zc::none;
  }
  auto name = BindingNameKey::from(static_cast<Namespace>(ZC_ASSERT_NONNULL(nameSpace)),
                                   zc::mv(ZC_ASSERT_NONNULL(declaredName)));
  auto binding = decodeStableFrame<StableBindingTargetKey>(decoder);
  auto canonicalTarget = decodeStableFrame<StableBindingTargetKey>(decoder);
  auto visibilityTag = decoder.decodeUint8();
  if (name == zc::none || binding == zc::none || canonicalTarget == zc::none ||
      visibilityTag == zc::none) {
    return zc::none;
  }
  zc::Maybe<MemberVisibility> visibility;
  if (ZC_ASSERT_NONNULL(visibilityTag) == 0x01) {
    auto value = decoder.decodeUint8();
    if (value == zc::none) { return zc::none; }
    visibility = static_cast<MemberVisibility>(ZC_ASSERT_NONNULL(value));
  } else if (ZC_ASSERT_NONNULL(visibilityTag) != 0x00) {
    return zc::none;
  }
  auto reexportChain = decodeSequence<StableReexportStep>(decoder);
  if (reexportChain == zc::none || !decoder.finished()) { return zc::none; }
  auto result = StableLocalExportFact::from(
      zc::mv(ZC_ASSERT_NONNULL(declaringModule)), zc::mv(ZC_ASSERT_NONNULL(exportPath)),
      zc::mv(ZC_ASSERT_NONNULL(name)), zc::mv(ZC_ASSERT_NONNULL(binding)),
      zc::mv(ZC_ASSERT_NONNULL(canonicalTarget)), zc::mv(visibility),
      zc::mv(ZC_ASSERT_NONNULL(reexportChain)));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<BinderQueryOwner>::encode(const BinderQueryOwner& value) {
  identity::CanonicalEncoder record;
  if (value.value().is<BinderModuleQueryOwner>()) {
    record.encodeUint8(0x01);
    encodeFrame(record, value.value().get<BinderModuleQueryOwner>().module.encode().asPtr());
  } else if (value.value().is<BinderDefinitionHeaderQueryOwner>()) {
    record.encodeUint8(0x02);
    encodeStableFrame(record, value.value().get<BinderDefinitionHeaderQueryOwner>().definition);
  } else if (value.value().is<BinderImplementationHeaderQueryOwner>()) {
    record.encodeUint8(0x03);
    encodeStableFrame(record,
                      value.value().get<BinderImplementationHeaderQueryOwner>().implementation);
  } else {
    record.encodeUint8(0x04);
    encodeStableFrame(record, value.value().get<BinderBodyQueryOwner>().body);
  }
  return record.finish();
}

zc::Maybe<BinderQueryOwner> StableBindingCodec<BinderQueryOwner>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() > kMaximumRoutedKeyBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto tag = decoder.decodeUint8();
  if (tag == zc::none) { return zc::none; }
  if (ZC_ASSERT_NONNULL(tag) == 0x01) {
    auto module = decodeModuleFrame(decoder);
    return module == zc::none
               ? zc::none
               : finishSum<BinderQueryOwner>(
                     BinderQueryOwner::module(zc::mv(ZC_ASSERT_NONNULL(module))), decoder, bytes);
  }
#define ZOM_DECODE_BINDER_OWNER(Tag, Type, Factory)                                                \
  if (ZC_ASSERT_NONNULL(tag) == Tag) {                                                             \
    auto value = decodeStableFrame<Type>(decoder);                                                 \
    return value == zc::none                                                                       \
               ? zc::none                                                                          \
               : finishSum<BinderQueryOwner>(                                                      \
                     BinderQueryOwner::Factory(zc::mv(ZC_ASSERT_NONNULL(value))), decoder, bytes); \
  }
  ZOM_DECODE_BINDER_OWNER(0x02, StableDefinitionQueryKey, definitionHeader)
  ZOM_DECODE_BINDER_OWNER(0x03, StableImplementationOccurrenceQueryKey, implementationHeader)
  ZOM_DECODE_BINDER_OWNER(0x04, StableOwnerBodyQueryKey, body)
#undef ZOM_DECODE_BINDER_OWNER
  return zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableFailedLookupOutcome>::encode(
    const StableFailedLookupOutcome& value) {
  identity::CanonicalEncoder record;
  if (value.value().is<StableMissingLookupOutcome>()) {
    record.encodeUint8(0x01);
  } else if (value.value().is<StableNamespaceMismatchLookupOutcome>()) {
    record.encodeUint8(0x02);
    encodeNonEmptySequence(
        record, value.value().get<StableNamespaceMismatchLookupOutcome>().availableNamespaces);
  } else {
    record.encodeUint8(0x03);
    encodeNonEmptySequence(record, value.value().get<StableAmbiguousLookupOutcome>().candidates);
  }
  return withDomain("zom.binder.failed-lookup-outcome"_zc, record.finish().asPtr());
}

zc::Maybe<StableFailedLookupOutcome> StableBindingCodec<StableFailedLookupOutcome>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.failed-lookup-outcome"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto tag = decoder.decodeUint8();
  if (tag == zc::none) { return zc::none; }
  zc::Maybe<StableFailedLookupOutcome> result;
  if (ZC_ASSERT_NONNULL(tag) == 0x01) {
    result = StableFailedLookupOutcome::missing();
  } else if (ZC_ASSERT_NONNULL(tag) == 0x02) {
    auto namespaces = decodeNonEmptySequence<Namespace>(decoder);
    if (namespaces == zc::none) { return zc::none; }
    result = StableFailedLookupOutcome::namespaceMismatch(zc::mv(ZC_ASSERT_NONNULL(namespaces)));
  } else if (ZC_ASSERT_NONNULL(tag) == 0x03) {
    auto candidates = decodeNonEmptySequence<StableBindingTargetKey>(
        decoder, stable_binding_codec_detail::kAmbiguityCandidates);
    if (candidates == zc::none) { return zc::none; }
    result = StableFailedLookupOutcome::ambiguous(zc::mv(ZC_ASSERT_NONNULL(candidates)));
  } else {
    return zc::none;
  }
  return result != zc::none && decoder.finished() &&
                 encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes
             ? zc::mv(result)
             : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableFailedLookupFact>::encode(
    const StableFailedLookupFact& value) {
  identity::CanonicalEncoder record;
  encodeStableFrame(record, value.owner());
  encodeFrame(record, value.usePath().encode().asPtr());
  record.encodeUint8(static_cast<uint8_t>(value.nameSpace()));
  value.name().encode(record);
  encodeStableFrame(record, value.outcome());
  return withDomain("zom.binder.failed-lookup"_zc, record.finish().asPtr());
}

zc::Maybe<StableFailedLookupFact> StableBindingCodec<StableFailedLookupFact>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.failed-lookup"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto owner = decodeStableFrame<BinderQueryOwner>(decoder);
  auto usePath = decodeLocalFrame<LocalSyntaxPath>(decoder);
  auto nameSpace = decoder.decodeUint8();
  auto name = identity::DeclaredDefinitionName::decodeCanonical(decoder);
  auto outcome = decodeStableFrame<StableFailedLookupOutcome>(decoder);
  if (owner == zc::none || usePath == zc::none || nameSpace == zc::none || name == zc::none ||
      outcome == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto result = StableFailedLookupFact::from(
      zc::mv(ZC_ASSERT_NONNULL(owner)), zc::mv(ZC_ASSERT_NONNULL(usePath)),
      static_cast<Namespace>(ZC_ASSERT_NONNULL(nameSpace)), zc::mv(ZC_ASSERT_NONNULL(name)),
      zc::mv(ZC_ASSERT_NONNULL(outcome)));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<BoundOwnerBody>::encode(const BoundOwnerBody& value) {
  identity::CanonicalEncoder record;
  encodeStableFrame(record, value.owner());
  encodeSequence(record, value.scopes());
  encodeSequence(record, value.nodeScopes());
  encodeSequence(record, value.bindings());
  encodeSequence(record, value.resolutions());
  encodeSequence(record, value.deferredMembers());
  encodeSequence(record, value.selfTypes());
  encodeSequence(record, value.thisBindings());
  encodeSequence(record, value.shadowTargets());
  encodeSequence(record, value.labels());
  encodeSequence(record, value.controlTransfers());
  encodeSequence(record, value.closures());
  encodeSequence(record, value.closureFreeVariables());
  encodeSequence(record, value.explicitClosureCaptures());
  encodeSequence(record, value.failedLookups());
  return withDomain("zom.binder.owner-body"_zc, record.finish().asPtr());
}

zc::Maybe<BoundOwnerBody> StableBindingCodec<BoundOwnerBody>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.owner-body"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) return zc::none;
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto owner = decodeStableFrame<StableOwnerBodyQueryKey>(decoder);
  auto scopes = decodeSequence<StableBodyScopeFact>(decoder);
  auto nodeScopes = decodeSequence<StableBodyNodeScopeFact>(decoder);
  auto bindings = decodeSequence<StableOwnerLocalBindingFact>(decoder);
  auto resolutions = decodeSequence<StableResolutionFact>(decoder);
  auto deferredMembers = decodeSequence<StableDeferredMemberFact>(decoder);
  auto selfTypes = decodeSequence<StableSelfTypeFact>(decoder);
  auto thisBindings = decodeSequence<StableThisBindingFact>(decoder);
  auto shadowTargets = decodeSequence<StableShadowTargetFact>(decoder);
  auto labels = decodeSequence<StableLabelFact>(decoder);
  auto controlTransfers = decodeSequence<StableControlTransferFact>(decoder);
  auto closures = decodeSequence<StableClosureFact>(decoder);
  auto closureFreeVariables = decodeSequence<StableClosureFreeVariableFact>(decoder);
  auto explicitClosureCaptures = decodeSequence<StableExplicitClosureCaptureFact>(decoder);
  auto failedLookups = decodeSequence<StableFailedLookupFact>(decoder);
  if (owner == zc::none || scopes == zc::none || nodeScopes == zc::none || bindings == zc::none ||
      resolutions == zc::none || deferredMembers == zc::none || selfTypes == zc::none ||
      thisBindings == zc::none || shadowTargets == zc::none || labels == zc::none ||
      controlTransfers == zc::none || closures == zc::none || closureFreeVariables == zc::none ||
      explicitClosureCaptures == zc::none || failedLookups == zc::none || !decoder.finished())
    return zc::none;
  auto result = BoundOwnerBody::from(
      zc::mv(ZC_ASSERT_NONNULL(owner)), zc::mv(ZC_ASSERT_NONNULL(scopes)),
      zc::mv(ZC_ASSERT_NONNULL(nodeScopes)), zc::mv(ZC_ASSERT_NONNULL(bindings)),
      zc::mv(ZC_ASSERT_NONNULL(resolutions)), zc::mv(ZC_ASSERT_NONNULL(deferredMembers)),
      zc::mv(ZC_ASSERT_NONNULL(selfTypes)), zc::mv(ZC_ASSERT_NONNULL(thisBindings)),
      zc::mv(ZC_ASSERT_NONNULL(shadowTargets)), zc::mv(ZC_ASSERT_NONNULL(labels)),
      zc::mv(ZC_ASSERT_NONNULL(controlTransfers)), zc::mv(ZC_ASSERT_NONNULL(closures)),
      zc::mv(ZC_ASSERT_NONNULL(closureFreeVariables)),
      zc::mv(ZC_ASSERT_NONNULL(explicitClosureCaptures)), zc::mv(ZC_ASSERT_NONNULL(failedLookups)));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<OwnerAllocationRange>::encode(
    const OwnerAllocationRange& value) {
  identity::CanonicalEncoder record;
  encodeStableFrame(record, value.owner());
  record.encodeUint32(value.scopeBegin());
  record.encodeUint32(value.scopeCount());
  record.encodeUint32(value.ownerLocalBegin());
  record.encodeUint32(value.ownerLocalCount());
  record.encodeUint32(value.anonymousBegin());
  record.encodeUint32(value.anonymousCount());
  record.encodeUint32(value.labelBegin());
  record.encodeUint32(value.labelCount());
  return withDomain("zom.binder.owner-allocation-range"_zc, record.finish().asPtr());
}

zc::Maybe<OwnerAllocationRange> StableBindingCodec<OwnerAllocationRange>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.owner-allocation-range"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto owner = decodeStableFrame<StableOwnerBodyQueryKey>(decoder);
  auto scopeBegin = decoder.decodeUint32();
  auto scopeCount = decoder.decodeUint32();
  auto ownerLocalBegin = decoder.decodeUint32();
  auto ownerLocalCount = decoder.decodeUint32();
  auto anonymousBegin = decoder.decodeUint32();
  auto anonymousCount = decoder.decodeUint32();
  auto labelBegin = decoder.decodeUint32();
  auto labelCount = decoder.decodeUint32();
  if (owner == zc::none || scopeBegin == zc::none || scopeCount == zc::none ||
      ownerLocalBegin == zc::none || ownerLocalCount == zc::none || anonymousBegin == zc::none ||
      anonymousCount == zc::none || labelBegin == zc::none || labelCount == zc::none ||
      !decoder.finished()) {
    return zc::none;
  }
  auto result = OwnerAllocationRange::from(
      zc::mv(ZC_ASSERT_NONNULL(owner)), ZC_ASSERT_NONNULL(scopeBegin),
      ZC_ASSERT_NONNULL(scopeCount), ZC_ASSERT_NONNULL(ownerLocalBegin),
      ZC_ASSERT_NONNULL(ownerLocalCount), ZC_ASSERT_NONNULL(anonymousBegin),
      ZC_ASSERT_NONNULL(anonymousCount), ZC_ASSERT_NONNULL(labelBegin),
      ZC_ASSERT_NONNULL(labelCount));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<ModuleBindingAllocationPlan>::encode(
    const ModuleBindingAllocationPlan& value) {
  identity::CanonicalEncoder record;
  encodeFrame(record, value.key().encode().asPtr());
  record.encodeUint32(value.skeletonScopeCount());
  record.encodeUint32(value.implementationOccurrenceCount());
  encodeSequence(record, value.owners());
  return withDomain("zom.binder.module-allocation-plan"_zc, record.finish().asPtr());
}

zc::Maybe<ModuleBindingAllocationPlan> StableBindingCodec<ModuleBindingAllocationPlan>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.module-allocation-plan"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto key = decodeModule(decoder);
  auto skeletonScopeCount = decoder.decodeUint32();
  auto implementationOccurrenceCount = decoder.decodeUint32();
  auto owners = decodeSequence<OwnerAllocationRange>(decoder);
  if (key == zc::none || skeletonScopeCount == zc::none ||
      implementationOccurrenceCount == zc::none || owners == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto result = ModuleBindingAllocationPlan::from(
      zc::mv(ZC_ASSERT_NONNULL(key)), ZC_ASSERT_NONNULL(skeletonScopeCount),
      ZC_ASSERT_NONNULL(implementationOccurrenceCount), zc::mv(ZC_ASSERT_NONNULL(owners)));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<BoundModuleSkeleton>::encode(
    const BoundModuleSkeleton& value) {
  identity::CanonicalEncoder record;
  encodeFrame(record, value.module().encode().asPtr());
  encodeSequence(record, value.scopes());
  encodeSequence(record, value.nodeScopes());
  encodeSequence(record, value.declarations());
  encodeSequence(record, value.implementationOccurrences());
  encodeSequence(record, value.genericParameterDeclarations());
  encodeSequence(record, value.callableParameterDeclarations());
  encodeSequence(record, value.moduleAliases());
  encodeSequence(record, value.imports());
  encodeSequence(record, value.localExports());
  encodeNonEmptySequence(record, value.bodyOwners());
  encodeSequence(record, value.failedLookups());
  return withDomain("zom.binder.module-skeleton"_zc, record.finish().asPtr());
}

zc::Maybe<BoundModuleSkeleton> StableBindingCodec<BoundModuleSkeleton>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.module-skeleton"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto module = decodeModule(decoder);
  auto scopes = decodeSequence<StableScopeFact>(decoder);
  auto nodeScopes = decodeSequence<StableNodeScopeFact>(decoder);
  auto declarations = decodeSequence<StableDeclarationFact>(decoder);
  auto occurrences = decodeSequence<StableImplementationOccurrenceFact>(decoder);
  auto generics = decodeSequence<StableGenericParameterDeclarationFact>(decoder);
  auto callables = decodeSequence<StableCallableParameterDeclarationFact>(decoder);
  auto aliases = decodeSequence<StableModuleAliasFact>(decoder);
  auto imports = decodeSequence<StableImportFact>(decoder);
  auto exports = decodeSequence<StableLocalExportFact>(decoder);
  auto bodyOwners = decodeNonEmptySequence<StableOwnerBodyQueryKey>(decoder);
  auto failures = decodeSequence<StableFailedLookupFact>(decoder);
  if (module == zc::none || scopes == zc::none || nodeScopes == zc::none ||
      declarations == zc::none || occurrences == zc::none || generics == zc::none ||
      callables == zc::none || aliases == zc::none || imports == zc::none || exports == zc::none ||
      bodyOwners == zc::none || failures == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto result = BoundModuleSkeleton::from(
      zc::mv(ZC_ASSERT_NONNULL(module)), zc::mv(ZC_ASSERT_NONNULL(scopes)),
      zc::mv(ZC_ASSERT_NONNULL(nodeScopes)), zc::mv(ZC_ASSERT_NONNULL(declarations)),
      zc::mv(ZC_ASSERT_NONNULL(occurrences)), zc::mv(ZC_ASSERT_NONNULL(generics)),
      zc::mv(ZC_ASSERT_NONNULL(callables)), zc::mv(ZC_ASSERT_NONNULL(aliases)),
      zc::mv(ZC_ASSERT_NONNULL(imports)), zc::mv(ZC_ASSERT_NONNULL(exports)),
      zc::mv(ZC_ASSERT_NONNULL(bodyOwners)), zc::mv(ZC_ASSERT_NONNULL(failures)));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableExportedBinding>::encode(
    const StableExportedBinding& value) {
  identity::CanonicalEncoder record;
  encodeBindingName(record, value.name());
  encodeStableFrame(record, value.binding());
  encodeStableFrame(record, value.canonicalTarget());
  ZC_IF_SOME(visibility, value.visibility()) {
    record.encodeUint8(0x01);
    record.encodeUint8(static_cast<uint8_t>(visibility));
  } else {
    record.encodeUint8(0x00);
  }
  record.encodeUint8(value.exported() ? 0x01 : 0x00);
  return withDomain("zom.binder.exported-binding"_zc, record.finish().asPtr());
}

zc::Maybe<StableExportedBinding> StableBindingCodec<StableExportedBinding>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.exported-binding"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto name = decodeBindingName(decoder);
  auto binding = decodeStableFrame<StableBindingTargetKey>(decoder);
  auto canonicalTarget = decodeStableFrame<StableBindingTargetKey>(decoder);
  auto visibilityTag = decoder.decodeUint8();
  if (name == zc::none || binding == zc::none || canonicalTarget == zc::none ||
      visibilityTag == zc::none) {
    return zc::none;
  }
  zc::Maybe<MemberVisibility> visibility;
  if (ZC_ASSERT_NONNULL(visibilityTag) == 0x01) {
    auto value = decoder.decodeUint8();
    if (value == zc::none) { return zc::none; }
    visibility = static_cast<MemberVisibility>(ZC_ASSERT_NONNULL(value));
  } else if (ZC_ASSERT_NONNULL(visibilityTag) != 0x00) {
    return zc::none;
  }
  auto exported = decoder.decodeUint8();
  if (exported == zc::none || ZC_ASSERT_NONNULL(exported) > 0x01 || !decoder.finished()) {
    return zc::none;
  }
  auto result = StableExportedBinding::from(
      zc::mv(ZC_ASSERT_NONNULL(name)), zc::mv(ZC_ASSERT_NONNULL(binding)),
      zc::mv(ZC_ASSERT_NONNULL(canonicalTarget)), zc::mv(visibility),
      ZC_ASSERT_NONNULL(exported) == 0x01);
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableExportedBindingQueryKey>::encode(
    const StableExportedBindingQueryKey& value) {
  identity::CanonicalEncoder record;
  encodeFrame(record, value.module().encode().asPtr());
  encodeBindingName(record, value.name());
  return withDomain("zom.query.exported-binding-key"_zc, record.finish().asPtr());
}

zc::Maybe<StableExportedBindingQueryKey> StableBindingCodec<StableExportedBindingQueryKey>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.query.exported-binding-key"_zc;
  if (bytes.size() > kMaximumRoutedKeyBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto module = decodeModuleFrame(decoder);
  auto name = decodeBindingName(decoder);
  if (module == zc::none || name == zc::none || !decoder.finished()) { return zc::none; }
  auto result = StableExportedBindingQueryKey::from(zc::mv(ZC_ASSERT_NONNULL(module)),
                                                    zc::mv(ZC_ASSERT_NONNULL(name)));
  return encode(result).asPtr() == bytes ? zc::Maybe<StableExportedBindingQueryKey>(zc::mv(result))
                                         : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<StableScopeNameBucketQueryKey>::encode(
    const StableScopeNameBucketQueryKey& value) {
  identity::CanonicalEncoder record;
  encodeStableFrame(record, value.scope());
  encodeBindingName(record, value.name());
  return withDomain("zom.query.scope-name-bucket-key"_zc, record.finish().asPtr());
}

zc::Maybe<StableScopeNameBucketQueryKey> StableBindingCodec<StableScopeNameBucketQueryKey>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.query.scope-name-bucket-key"_zc;
  if (bytes.size() > kMaximumRoutedKeyBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto scope = decodeStableFrame<StableScopeOwnerKey>(decoder);
  auto name = decodeBindingName(decoder);
  if (scope == zc::none || name == zc::none || !decoder.finished()) { return zc::none; }
  auto result = StableScopeNameBucketQueryKey::from(zc::mv(ZC_ASSERT_NONNULL(scope)),
                                                    zc::mv(ZC_ASSERT_NONNULL(name)));
  return result != zc::none && encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes ? zc::mv(result)
                                                                                  : zc::none;
}

zc::Array<uint8_t> StableBindingCodec<BinderKeyFailure>::encode(const BinderKeyFailure& value) {
  identity::CanonicalEncoder record;
  record.encodeUint8(static_cast<uint8_t>(value.kind()));
  encodeStableFrame(record, value.owner());
  ZC_IF_SOME(path, value.path()) {
    record.encodeUint8(0x01);
    encodeFrame(record, path.encode().asPtr());
  } else {
    record.encodeUint8(0x00);
  }
  const auto bytes = record.finish();
  return withDomain("zom.binder.key-failure"_zc, bytes.asPtr());
}

zc::Maybe<BinderKeyFailure> StableBindingCodec<BinderKeyFailure>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.binder.key-failure"_zc;
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto kind = decoder.decodeUint8();
  auto owner = decodeStableFrame<BinderQueryOwner>(decoder);
  auto pathTag = decoder.decodeUint8();
  if (kind == zc::none || owner == zc::none || pathTag == zc::none) { return zc::none; }
  zc::Maybe<LocalSyntaxPath> path;
  if (ZC_ASSERT_NONNULL(pathTag) == 0x01) {
    path = decodeLocalFrame<LocalSyntaxPath>(decoder);
    if (path == zc::none) { return zc::none; }
  } else if (ZC_ASSERT_NONNULL(pathTag) != 0x00) {
    return zc::none;
  }
  return finishSum(
      BinderKeyFailure::from(static_cast<BinderKeyFailureKind>(ZC_ASSERT_NONNULL(kind)),
                             zc::mv(ZC_ASSERT_NONNULL(owner)), zc::mv(path)),
      decoder, bytes);
}

template <typename T>
zc::Array<uint8_t> StableBindingCodec<BinderQueryResult<T>>::encode(
    const BinderQueryResult<T>& value) {
  identity::CanonicalEncoder record;
  if (value.storage().template is<BinderQueryValue<T>>()) {
    const auto& result = value.storage().template get<BinderQueryValue<T>>();
    record.encodeUint8(0x01);
    encodeStableFrame(record, result.value);
    auto diagnosticBytes =
        diagnostics::encodeDiagnosticFacts(zc::none, result.diagnostics.values(),
                                           stable_binding_codec_detail::kBinderDiagnosticLimits);
    encodeFrame(record, ZC_REQUIRE_NONNULL(diagnosticBytes).asPtr());
  } else if (value.storage().template is<BinderSourceRejected>()) {
    record.encodeUint8(0x02);
    auto diagnosticBytes = diagnostics::encodeDiagnosticFacts(
        zc::none, value.storage().template get<BinderSourceRejected>().diagnostics.values(),
        stable_binding_codec_detail::kBinderDiagnosticLimits);
    encodeFrame(record, ZC_REQUIRE_NONNULL(diagnosticBytes).asPtr());
  } else {
    record.encodeUint8(0x03);
    encodeStableFrame(record, value.storage().template get<BinderKeyRejected>().failure);
  }
  const auto bytes = record.finish();
  return withDomain(binderQueryResultDomain<T>(), bytes.asPtr());
}

template <typename T>
zc::Maybe<BinderQueryResult<T>> StableBindingCodec<BinderQueryResult<T>>::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  const auto domain = binderQueryResultDomain<T>();
  if (bytes.size() > kMaximumBinderValueBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto tag = decoder.decodeUint8();
  if (tag == zc::none) { return zc::none; }
  zc::Maybe<BinderQueryResult<T>> result;
  if (ZC_ASSERT_NONNULL(tag) == 0x01) {
    auto valueBytes = decoder.decodeByteString(kMaximumBinderValueBytes);
    auto diagnosticBytes =
        decoder.decodeByteString(stable_binding_codec_detail::kDiagnosticPayloadBytes);
    if (valueBytes == zc::none || diagnosticBytes == zc::none) { return zc::none; }
    auto value = StableBindingCodec<T>::decode(ZC_ASSERT_NONNULL(valueBytes).asPtr());
    auto diagnostics =
        diagnostics::decodeDiagnosticFacts(zc::none, ZC_ASSERT_NONNULL(diagnosticBytes).asPtr(),
                                           stable_binding_codec_detail::kBinderDiagnosticLimits);
    if (value == zc::none || diagnostics == zc::none) { return zc::none; }
    auto admitted = StableBindingSequenceBuilder<diagnostics::DiagnosticFact>::from(
        zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
    if (admitted == zc::none) { return zc::none; }
    result = BinderQueryResult<T>::value(zc::mv(ZC_ASSERT_NONNULL(value)),
                                         zc::mv(ZC_ASSERT_NONNULL(admitted)));
  } else if (ZC_ASSERT_NONNULL(tag) == 0x02) {
    auto diagnosticBytes =
        decoder.decodeByteString(stable_binding_codec_detail::kDiagnosticPayloadBytes);
    if (diagnosticBytes == zc::none) { return zc::none; }
    auto diagnostics =
        diagnostics::decodeDiagnosticFacts(zc::none, ZC_ASSERT_NONNULL(diagnosticBytes).asPtr(),
                                           stable_binding_codec_detail::kBinderDiagnosticLimits);
    if (diagnostics == zc::none) { return zc::none; }
    auto admitted = StableBindingSequenceBuilder<diagnostics::DiagnosticFact>::fromNonEmpty(
        zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
    if (admitted == zc::none) { return zc::none; }
    result = BinderQueryResult<T>::sourceRejected(zc::mv(ZC_ASSERT_NONNULL(admitted)));
  } else if (ZC_ASSERT_NONNULL(tag) == 0x03) {
    auto failureBytes = decoder.decodeByteString(kMaximumBinderValueBytes);
    if (failureBytes == zc::none) { return zc::none; }
    auto failure =
        StableBindingCodec<BinderKeyFailure>::decode(ZC_ASSERT_NONNULL(failureBytes).asPtr());
    if (failure == zc::none) { return zc::none; }
    result = BinderQueryResult<T>::keyRejected(zc::mv(ZC_ASSERT_NONNULL(failure)));
  } else {
    return zc::none;
  }
  return result != zc::none && decoder.finished() &&
                 encode(ZC_ASSERT_NONNULL(result)).asPtr() == bytes
             ? zc::mv(result)
             : zc::none;
}

template struct StableBindingCodec<BinderQueryResult<StableDefinitionHeader>>;
template struct StableBindingCodec<BinderQueryResult<StableImplementationOccurrenceHeader>>;

#define ZOM_DEFINE_DIGEST_CODEC(Name, Domain, KeyType, KeyName)              \
  zc::Array<uint8_t> Name::encodeCanonical() const {                         \
    return encodeDigest(Domain##_zc, module(), KeyName().bytes());           \
  }                                                                          \
  zc::Maybe<Name> Name::decodeCanonical(zc::ArrayPtr<const uint8_t> bytes) { \
    return decodeDigestKey<KeyType, Name>(bytes, Domain##_zc);               \
  }

ZOM_DEFINE_DIGEST_CODEC(StableDefinitionQueryKey, "zom.binder.definition-query-key",
                        identity::DefinitionKey, definition)
ZOM_DEFINE_DIGEST_CODEC(StableImplementationQueryKey, "zom.binder.implementation-query-key",
                        identity::ImplKey, implementation)
ZOM_DEFINE_DIGEST_CODEC(StableGenericParameterQueryKey, "zom.binder.generic-parameter-query-key",
                        identity::GenericParameterKey, parameter)
ZOM_DEFINE_DIGEST_CODEC(StableCallableParameterQueryKey, "zom.binder.callable-parameter-query-key",
                        identity::CallableParameterKey, parameter)

#undef ZOM_DEFINE_DIGEST_CODEC

zc::Array<uint8_t> StableImplementationOccurrenceQueryKey::encodeCanonical() const {
  const auto nested = occurrence().encode();
  return encodeNested("zom.binder.implementation-occurrence-query-key"_zc, module(),
                      nested.asPtr());
}

zc::Maybe<StableImplementationOccurrenceQueryKey>
StableImplementationOccurrenceQueryKey::decodeCanonical(zc::ArrayPtr<const uint8_t> bytes) {
  return decodeNestedKey<ImplSourceOccurrenceKey, StableImplementationOccurrenceQueryKey>(
      bytes, "zom.binder.implementation-occurrence-query-key"_zc,
      &ImplSourceOccurrenceKey::decodeCanonical);
}

zc::Array<uint8_t> StableSemanticImportQueryKey::encodeCanonical() const {
  const auto nested = binding().encode();
  return encodeNested("zom.binder.semantic-import-query-key"_zc, requester(), nested.asPtr());
}

zc::Maybe<StableSemanticImportQueryKey> StableSemanticImportQueryKey::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  return decodeNestedKey<identity::SemanticImportBindingKey, StableSemanticImportQueryKey>(
      bytes, "zom.binder.semantic-import-query-key"_zc, &decodeSemanticImportBinding);
}

zc::Array<uint8_t> StableOwnerBodyQueryKey::encodeCanonical() const {
  const auto nested = owner().encode();
  return encodeNested("zom.binder.owner-body-query-key"_zc, module(), nested.asPtr());
}

zc::Maybe<StableOwnerBodyQueryKey> StableOwnerBodyQueryKey::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  return decodeNestedKey<StableBodyOwnerKey, StableOwnerBodyQueryKey>(
      bytes, "zom.binder.owner-body-query-key"_zc, &StableBodyOwnerKey::decodeCanonical);
}

}  // namespace zomlang::compiler::binder
