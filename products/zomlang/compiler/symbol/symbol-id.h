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

#include <cstddef>
#include <cstdint>

namespace zomlang {
namespace compiler {
namespace symbol {

/// \brief Symbol ID system aligned with 64-bit flags
class SymbolId {
public:
  using IdType = uint64_t;

  /// \brief Create a new symbol ID
  /// \param index Unique index within symbol table
  /// \return SymbolId instance
  static SymbolId create(uint64_t index) { return SymbolId(index); }

  /// \brief Create symbol ID from raw value
  /// \param raw The raw 64-bit value
  /// \return SymbolId instance
  static SymbolId fromRaw(IdType raw) { return SymbolId(raw); }

  /// \brief Default constructor for invalid ID
  SymbolId() : raw(0u) {}

  /// \brief Get the raw 64-bit value
  /// \return The raw 64-bit identifier
  IdType getRaw() const { return raw; }

  /// \brief Get the symbol index
  uint64_t index() const { return raw; }

  /// \brief Check if this is a valid symbol ID
  /// \return true if valid, false otherwise
  bool isValid() const { return raw != 0; }

  /// \brief Equality comparison
  /// \param other The other SymbolId to compare with
  /// \return true if equal, false otherwise
  bool operator==(const SymbolId& other) const { return raw == other.raw; }
  bool operator!=(const SymbolId& other) const { return raw != other.raw; }

  /// \brief Ordering for use in containers
  /// \param other The other SymbolId to compare with
  /// \return true if this is less than other
  bool operator<(const SymbolId& other) const { return raw < other.raw; }

  /// \brief Hash function for use in hash maps
  struct Hash {
    size_t operator()(const SymbolId& id) const noexcept { return static_cast<size_t>(id.raw); }
  };

private:
  explicit SymbolId(IdType raw) : raw(raw) {}

  IdType raw;
};

}  // namespace symbol
}  // namespace compiler
}  // namespace zomlang
