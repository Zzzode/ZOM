// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zomlang/compiler/identity/sha256.h"

namespace zomlang::compiler::binder {

/// \brief SHA-256 digest of one complete canonical session-input payload.
class CanonicalInputPayloadDigest final {
public:
  /// \brief Constructs the digest from exactly 32 verified bytes.
  ZC_NODISCARD static zc::Maybe<CanonicalInputPayloadDigest> fromBytes(
      zc::ArrayPtr<const uint8_t> bytes);

  ZC_NODISCARD CanonicalInputPayloadDigest clone() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> bytes() const ZC_LIFETIMEBOUND;
  bool operator==(const CanonicalInputPayloadDigest& other) const noexcept;
  bool operator!=(const CanonicalInputPayloadDigest& other) const noexcept {
    return !(*this == other);
  }

private:
  explicit CanonicalInputPayloadDigest(const identity::Sha256Digest& digest) noexcept;

  identity::Sha256Digest digestField;
};

}  // namespace zomlang::compiler::binder
