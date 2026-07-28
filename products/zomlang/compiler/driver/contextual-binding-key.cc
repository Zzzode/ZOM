// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/contextual-binding-key.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::driver::incremental_binding_query {
namespace {

constexpr uint64_t kMaximumContextComponentBytes = 64 * 1024 * 1024;
constexpr uint64_t kMaximumContextualKeyBytes = 128 * 1024 * 1024;

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

template <typename T>
struct PayloadCodec final {
  static zc::Array<uint8_t> encode(const T& value) { return value.encodeCanonical(); }
  static zc::Maybe<T> decode(zc::ArrayPtr<const uint8_t> bytes) {
    return T::decodeCanonical(bytes);
  }
};

template <typename T>
zc::Maybe<T> decodeIdentity(zc::ArrayPtr<const uint8_t> bytes,
                            zc::Maybe<T> (*decode)(identity::CanonicalDecoder&)) {
  identity::CanonicalDecoder decoder(bytes);
  auto value = decode(decoder);
  if (value == zc::none || !decoder.finished() ||
      ZC_ASSERT_NONNULL(value).encode().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(value);
}

#define ZOM_DEFINE_IDENTITY_PAYLOAD_CODEC(Type)                                    \
  template <>                                                                      \
  struct PayloadCodec<Type> final {                                                \
    static zc::Array<uint8_t> encode(const Type& value) { return value.encode(); } \
    static zc::Maybe<Type> decode(zc::ArrayPtr<const uint8_t> bytes) {             \
      return decodeIdentity<Type>(bytes, &Type::decodeCanonical);                  \
    }                                                                              \
  }

ZOM_DEFINE_IDENTITY_PAYLOAD_CODEC(identity::CompilationUnitIdentity);
ZOM_DEFINE_IDENTITY_PAYLOAD_CODEC(identity::CrateKey);
ZOM_DEFINE_IDENTITY_PAYLOAD_CODEC(identity::SourceFileKey);
ZOM_DEFINE_IDENTITY_PAYLOAD_CODEC(identity::ModuleKey);

#undef ZOM_DEFINE_IDENTITY_PAYLOAD_CODEC

template <typename T>
zc::Array<uint8_t> encodeKey(zc::StringPtr domain, const CompilationRootSetQueryKey& roots,
                             const T& payload) {
  identity::CanonicalEncoder record;
  const auto rootBytes = roots.encodeCanonical();
  const auto payloadBytes = PayloadCodec<T>::encode(payload);
  record.encodeByteString(rootBytes.asPtr());
  record.encodeByteString(payloadBytes.asPtr());
  const auto bytes = record.finish();
  return withDomain(domain, bytes.asPtr());
}

template <typename T, typename Result>
zc::Maybe<Result> decodeKey(zc::ArrayPtr<const uint8_t> bytes, zc::StringPtr domain) {
  if (bytes.size() > kMaximumContextualKeyBytes || !hasDomain(bytes, domain)) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto rootBytes = decoder.decodeByteString(kMaximumContextComponentBytes);
  auto payloadBytes = decoder.decodeByteString(kMaximumContextComponentBytes);
  if (rootBytes == zc::none || payloadBytes == zc::none || !decoder.finished()) { return zc::none; }
  auto roots = CompilationRootSetQueryKey::decodeCanonical(ZC_ASSERT_NONNULL(rootBytes).asPtr());
  auto payload = PayloadCodec<T>::decode(ZC_ASSERT_NONNULL(payloadBytes).asPtr());
  if (roots == zc::none || payload == zc::none ||
      ZC_ASSERT_NONNULL(roots).encodeCanonical().asPtr() != ZC_ASSERT_NONNULL(rootBytes).asPtr()) {
    return zc::none;
  }
  return Result::from(zc::mv(ZC_ASSERT_NONNULL(roots)), zc::mv(ZC_ASSERT_NONNULL(payload)));
}

}  // namespace

#define ZOM_DEFINE_CONTEXTUAL_BINDING_KEY(Name, Domain, ValueType, ValueName)                    \
  Name::Name(CompilationRootSetQueryKey&& contextRoots, ValueType&& ValueName) noexcept          \
      : contextRootsField(zc::mv(contextRoots)), ValueName##Field(zc::mv(ValueName)) {}          \
  Name Name::from(CompilationRootSetQueryKey&& contextRoots, ValueType&& ValueName) {            \
    return Name(zc::mv(contextRoots), zc::mv(ValueName));                                        \
  }                                                                                              \
  Name Name::clone() const { return Name(contextRootsField.clone(), ValueName##Field.clone()); } \
  const CompilationRootSetQueryKey& Name::contextRoots() const noexcept {                        \
    return contextRootsField;                                                                    \
  }                                                                                              \
  const ValueType& Name::ValueName() const noexcept { return ValueName##Field; }                 \
  zc::Array<uint8_t> Name::encodeCanonical() const {                                             \
    return encodeKey(Domain##_zc, contextRootsField, ValueName##Field);                          \
  }                                                                                              \
  zc::Maybe<Name> Name::decodeCanonical(zc::ArrayPtr<const uint8_t> bytes) {                     \
    return decodeKey<ValueType, Name>(bytes, Domain##_zc);                                       \
  }                                                                                              \
  bool Name::operator==(const Name& other) const {                                               \
    return contextRootsField == other.contextRootsField &&                                       \
           PayloadCodec<ValueType>::encode(ValueName##Field).asPtr() ==                          \
               PayloadCodec<ValueType>::encode(other.ValueName##Field).asPtr();                  \
  }

ZOM_DEFINE_CONTEXTUAL_BINDING_KEY(ContextualBodyOwnerKey, "zom.binder.contextual-body-owner-key",
                                  binder::StableOwnerBodyQueryKey, body)
ZOM_DEFINE_CONTEXTUAL_BINDING_KEY(ContextualCompilationUnitKey,
                                  "zom.binder.contextual-compilation-unit-key",
                                  identity::CompilationUnitIdentity, unit)
ZOM_DEFINE_CONTEXTUAL_BINDING_KEY(ContextualCrateKey, "zom.binder.contextual-crate-key",
                                  identity::CrateKey, crate)
ZOM_DEFINE_CONTEXTUAL_BINDING_KEY(ContextualSourceKey, "zom.binder.contextual-source-key",
                                  identity::SourceFileKey, source)
ZOM_DEFINE_CONTEXTUAL_BINDING_KEY(ContextualModuleKey, "zom.binder.contextual-module-key",
                                  identity::ModuleKey, module)
ZOM_DEFINE_CONTEXTUAL_BINDING_KEY(ContextualDefinitionKey, "zom.binder.contextual-definition-key",
                                  binder::StableDefinitionQueryKey, definition)
ZOM_DEFINE_CONTEXTUAL_BINDING_KEY(ContextualImplementationKey,
                                  "zom.binder.contextual-implementation-key",
                                  binder::StableImplementationQueryKey, implementation)
ZOM_DEFINE_CONTEXTUAL_BINDING_KEY(ContextualGenericParameterKey,
                                  "zom.binder.contextual-generic-parameter-key",
                                  binder::StableGenericParameterQueryKey, parameter)
ZOM_DEFINE_CONTEXTUAL_BINDING_KEY(ContextualCallableParameterKey,
                                  "zom.binder.contextual-callable-parameter-key",
                                  binder::StableCallableParameterQueryKey, parameter)

#undef ZOM_DEFINE_CONTEXTUAL_BINDING_KEY

}  // namespace zomlang::compiler::driver::incremental_binding_query
