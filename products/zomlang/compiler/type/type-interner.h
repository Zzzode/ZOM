// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include <cstdint>

#include "zc/core/map.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/type/type.h"

namespace zomlang {
namespace compiler {
namespace type {

/// \brief Stable interned type identifier.
class TypeId final {
public:
  constexpr TypeId() : value(0) {}
  explicit constexpr TypeId(uint32_t value) : value(value) {}

  constexpr bool isValid() const { return value != 0; }
  constexpr bool operator==(TypeId other) const { return value == other.value; }
  constexpr bool operator!=(TypeId other) const { return value != other.value; }

  uint32_t value;
};

/// \brief Canonical string-key type interner for RFC 0005 TypeId identity.
class TypeInterner final {
public:
  TypeInterner();
  ~TypeInterner() noexcept(false);

  ZC_DISALLOW_COPY(TypeInterner);
  TypeInterner(TypeInterner&& other) noexcept;
  TypeInterner& operator=(TypeInterner&& other) noexcept;

  /// \brief Intern a type and return its canonical id.
  TypeId intern(const Type& type);

  /// \brief Intern the canonical union of two type trees without constructing an owning union.
  /// \param first First type tree to include.
  /// \param second Second type tree to include.
  /// \return Canonical id of the flattened, deduplicated union.
  TypeId internUnion(const Type& first, const Type& second);

  /// \brief Return whether an identifier belongs to this interner.
  /// \param id Identifier to validate.
  /// \return True when the identifier is valid and in range.
  bool contains(TypeId id) const;

  /// \brief Return the canonical key for an interned type.
  zc::StringPtr getCanonicalKey(TypeId id) const;

  /// \brief Number of unique canonical types interned.
  size_t size() const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
