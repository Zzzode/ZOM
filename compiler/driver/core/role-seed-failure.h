// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "compiler/source/core-distribution.h"

namespace zomlang::compiler::identity {
class CanonicalDecoder;
class CanonicalEncoder;
}  // namespace zomlang::compiler::identity

namespace zomlang::compiler::driver::core_library_query {

/// \brief Closed reason for rejecting the source-backed core role seed.
enum class CoreRoleSeedFailureKind : uint8_t {
  InputReceiptMismatch = 0x01,
  ForeignContext = 0x02,
  StaleRevision = 0x03,
  CanonicalCodecMismatch = 0x04,
  MissingRequiredRole = 0x05,
  DuplicateRole = 0x06,
  WrongRoleModule = 0x07,
  WrongRoleKind = 0x08,
  WrongRoleNamespace = 0x09,
  WrongRoleName = 0x0a,
  WrongRoleVisibility = 0x0b
};

/// \brief Canonical role-seed rejection with its required coordinate role.
struct CoreRoleSeedFailure final {
  ZC_NODISCARD static zc::Maybe<CoreRoleSeedFailure> from(
      CoreRoleSeedFailureKind kind, zc::Maybe<source::core::CoreSemanticRole> role);
  ZC_NODISCARD static zc::Maybe<CoreRoleSeedFailure> decodeCanonical(
      zc::ArrayPtr<const uint8_t> encoded);

  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const CoreRoleSeedFailure& other) const noexcept;
  bool operator!=(const CoreRoleSeedFailure& other) const noexcept { return !(*this == other); }

  CoreRoleSeedFailureKind kind;
  zc::Maybe<source::core::CoreSemanticRole> role;
};

}  // namespace zomlang::compiler::driver::core_library_query
