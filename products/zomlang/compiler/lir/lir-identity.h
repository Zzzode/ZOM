// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include <cstdint>

#include "zc/core/common.h"

namespace zomlang::compiler::lir {

// RFC 0021 defines `LirValueTypeId`, `LayoutId`, and `FnAbiId` as distinct
// store-local branded handles that cannot compare equal across stores, and
// `RuntimeSymbolId` plus `LirSourceLocationId` as one-based deterministic
// module-local identities where zero is invalid. Each identity below is its
// own C++ type, so a handle issued by one store cannot be compared, assigned,
// or passed where another store's handle is expected. Numeric slots never
// participate in canonical ordering or revision inputs; canonical structural
// records are the only persistent comparison keys.

/// \brief Store-local branded identity of one SSA carrier type record.
class LirValueTypeId final {
public:
  constexpr LirValueTypeId() noexcept = default;

  /// \brief Builds a valid identity from a store's one-based interning order.
  /// \param ordinal One-based value-type ordinal.
  /// \return The identity, or none for zero.
  ZC_NODISCARD static zc::Maybe<LirValueTypeId> fromOrdinal(uint32_t ordinal) noexcept {
    if (ordinal == 0) { return zc::none; }
    return LirValueTypeId(ordinal);
  }

  ZC_NODISCARD constexpr bool isValid() const noexcept { return value != 0; }
  ZC_NODISCARD constexpr uint32_t ordinal() const noexcept { return value; }

  constexpr bool operator==(LirValueTypeId other) const noexcept { return value == other.value; }
  constexpr bool operator!=(LirValueTypeId other) const noexcept { return !(*this == other); }

private:
  explicit constexpr LirValueTypeId(uint32_t ordinal) noexcept : value(ordinal) {}

  uint32_t value = 0;
};

/// \brief Store-local branded identity of one storage-layout record.
class LayoutId final {
public:
  constexpr LayoutId() noexcept = default;

  /// \brief Builds a valid identity from a store's one-based interning order.
  /// \param ordinal One-based layout ordinal.
  /// \return The identity, or none for zero.
  ZC_NODISCARD static zc::Maybe<LayoutId> fromOrdinal(uint32_t ordinal) noexcept {
    if (ordinal == 0) { return zc::none; }
    return LayoutId(ordinal);
  }

  ZC_NODISCARD constexpr bool isValid() const noexcept { return value != 0; }
  ZC_NODISCARD constexpr uint32_t ordinal() const noexcept { return value; }

  constexpr bool operator==(LayoutId other) const noexcept { return value == other.value; }
  constexpr bool operator!=(LayoutId other) const noexcept { return !(*this == other); }

private:
  explicit constexpr LayoutId(uint32_t ordinal) noexcept : value(ordinal) {}

  uint32_t value = 0;
};

/// \brief Store-local branded identity of one function-ABI (FnAbi) record.
class FnAbiId final {
public:
  constexpr FnAbiId() noexcept = default;

  /// \brief Builds a valid identity from a store's one-based interning order.
  /// \param ordinal One-based function-ABI ordinal.
  /// \return The identity, or none for zero.
  ZC_NODISCARD static zc::Maybe<FnAbiId> fromOrdinal(uint32_t ordinal) noexcept {
    if (ordinal == 0) { return zc::none; }
    return FnAbiId(ordinal);
  }

  ZC_NODISCARD constexpr bool isValid() const noexcept { return value != 0; }
  ZC_NODISCARD constexpr uint32_t ordinal() const noexcept { return value; }

  constexpr bool operator==(FnAbiId other) const noexcept { return value == other.value; }
  constexpr bool operator!=(FnAbiId other) const noexcept { return !(*this == other); }

private:
  explicit constexpr FnAbiId(uint32_t ordinal) noexcept : value(ordinal) {}

  uint32_t value = 0;
};

/// \brief Module-local branded identity of one imported runtime symbol.
class RuntimeSymbolId final {
public:
  constexpr RuntimeSymbolId() noexcept = default;

  /// \brief Builds a valid identity from a store's one-based interning order.
  /// \param ordinal One-based runtime-symbol ordinal.
  /// \return The identity, or none for zero.
  ZC_NODISCARD static zc::Maybe<RuntimeSymbolId> fromOrdinal(uint32_t ordinal) noexcept {
    if (ordinal == 0) { return zc::none; }
    return RuntimeSymbolId(ordinal);
  }

  ZC_NODISCARD constexpr bool isValid() const noexcept { return value != 0; }
  ZC_NODISCARD constexpr uint32_t ordinal() const noexcept { return value; }

  constexpr bool operator==(RuntimeSymbolId other) const noexcept { return value == other.value; }
  constexpr bool operator!=(RuntimeSymbolId other) const noexcept { return !(*this == other); }

private:
  explicit constexpr RuntimeSymbolId(uint32_t ordinal) noexcept : value(ordinal) {}

  uint32_t value = 0;
};

/// \brief Module-local branded identity of one source-location record.
class LirSourceLocationId final {
public:
  constexpr LirSourceLocationId() noexcept = default;

  /// \brief Builds a valid identity from a store's one-based interning order.
  /// \param ordinal One-based source-location ordinal.
  /// \return The identity, or none for zero.
  ZC_NODISCARD static zc::Maybe<LirSourceLocationId> fromOrdinal(uint32_t ordinal) noexcept {
    if (ordinal == 0) { return zc::none; }
    return LirSourceLocationId(ordinal);
  }

  ZC_NODISCARD constexpr bool isValid() const noexcept { return value != 0; }
  ZC_NODISCARD constexpr uint32_t ordinal() const noexcept { return value; }

  constexpr bool operator==(LirSourceLocationId other) const noexcept {
    return value == other.value;
  }
  constexpr bool operator!=(LirSourceLocationId other) const noexcept { return !(*this == other); }

private:
  explicit constexpr LirSourceLocationId(uint32_t ordinal) noexcept : value(ordinal) {}

  uint32_t value = 0;
};

}  // namespace zomlang::compiler::lir
