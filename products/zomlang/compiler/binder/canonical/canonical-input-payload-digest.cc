// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/canonical/canonical-input-payload-digest.h"

namespace zomlang::compiler::binder {

CanonicalInputPayloadDigest::CanonicalInputPayloadDigest(
    const identity::Sha256Digest& digest) noexcept
    : digestField(digest) {}

zc::Maybe<CanonicalInputPayloadDigest> CanonicalInputPayloadDigest::fromBytes(
    zc::ArrayPtr<const uint8_t> bytes) {
  auto digest = identity::Sha256Digest::fromBytes(bytes);
  if (digest == zc::none) { return zc::none; }
  return CanonicalInputPayloadDigest(ZC_ASSERT_NONNULL(digest));
}

CanonicalInputPayloadDigest CanonicalInputPayloadDigest::clone() const noexcept {
  return CanonicalInputPayloadDigest(digestField);
}

zc::ArrayPtr<const uint8_t> CanonicalInputPayloadDigest::bytes() const {
  return digestField.bytes();
}

bool CanonicalInputPayloadDigest::operator==(
    const CanonicalInputPayloadDigest& other) const noexcept {
  return digestField == other.digestField;
}

}  // namespace zomlang::compiler::binder
