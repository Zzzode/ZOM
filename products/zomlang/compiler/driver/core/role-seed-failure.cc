// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/core/role-seed-failure.h"

#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::driver::core_library_query {
namespace {

bool isKnownKind(CoreRoleSeedFailureKind kind) {
  return kind >= CoreRoleSeedFailureKind::InputReceiptMismatch &&
         kind <= CoreRoleSeedFailureKind::WrongRoleVisibility;
}

bool requiresRole(CoreRoleSeedFailureKind kind) {
  return kind >= CoreRoleSeedFailureKind::MissingRequiredRole &&
         kind <= CoreRoleSeedFailureKind::WrongRoleVisibility;
}

bool isCoreRole(source::core::CoreSemanticRole role) {
  return role == source::core::CoreSemanticRole::Copy ||
         role == source::core::CoreSemanticRole::Linear;
}

}  // namespace

zc::Maybe<CoreRoleSeedFailure> CoreRoleSeedFailure::from(
    CoreRoleSeedFailureKind kind, zc::Maybe<source::core::CoreSemanticRole> role) {
  if (!isKnownKind(kind) || requiresRole(kind) != (role != zc::none)) { return zc::none; }
  ZC_IF_SOME(value, role) {
    if (!isCoreRole(value)) { return zc::none; }
  }
  return CoreRoleSeedFailure{kind, role};
}

zc::Maybe<CoreRoleSeedFailure> CoreRoleSeedFailure::decodeCanonical(
    zc::ArrayPtr<const uint8_t> encoded) {
  identity::CanonicalDecoder decoder(encoded);
  auto kind = decoder.decodeUint8();
  auto hasRole = decoder.decodeBool();
  if (kind == zc::none || hasRole == zc::none) { return zc::none; }
  zc::Maybe<source::core::CoreSemanticRole> role;
  if (ZC_ASSERT_NONNULL(hasRole)) {
    auto tag = decoder.decodeUint8();
    if (tag == zc::none) { return zc::none; }
    role = static_cast<source::core::CoreSemanticRole>(ZC_ASSERT_NONNULL(tag));
  }
  if (!decoder.finished()) { return zc::none; }
  return from(static_cast<CoreRoleSeedFailureKind>(ZC_ASSERT_NONNULL(kind)), role);
}

void CoreRoleSeedFailure::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(kind));
  if (role == zc::none) {
    encoder.encodeNone();
    return;
  }
  encoder.encodeSome();
  ZC_IF_SOME(value, role) { encoder.encodeUint8(static_cast<uint8_t>(value)); }
}

zc::Array<uint8_t> CoreRoleSeedFailure::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

bool CoreRoleSeedFailure::operator==(const CoreRoleSeedFailure& other) const noexcept {
  return kind == other.kind && role == other.role;
}

}  // namespace zomlang::compiler::driver::core_library_query
