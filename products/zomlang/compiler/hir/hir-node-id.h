// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/common.h"
#include "zc/core/one-of.h"

namespace zomlang::compiler::hir {

/// \brief Deterministic layer-local identity assigned in HIR preorder.
class HirNodeId final {
public:
  constexpr HirNodeId() noexcept = default;

  /// \brief Construct a valid identity from its one-based deterministic ordinal.
  ZC_NODISCARD static zc::Maybe<HirNodeId> fromOrdinal(uint32_t ordinal) noexcept {
    if (ordinal == 0) { return zc::none; }
    return HirNodeId(ordinal);
  }

  ZC_NODISCARD constexpr bool isValid() const noexcept { return value != 0; }
  ZC_NODISCARD constexpr uint32_t ordinal() const noexcept { return value; }

  constexpr bool operator==(HirNodeId other) const noexcept { return value == other.value; }
  constexpr bool operator!=(HirNodeId other) const noexcept { return !(*this == other); }

private:
  explicit constexpr HirNodeId(uint32_t ordinal) noexcept : value(ordinal) {}
  uint32_t value = 0;
};

}  // namespace zomlang::compiler::hir
