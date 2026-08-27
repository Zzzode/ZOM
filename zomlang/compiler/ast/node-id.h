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

namespace zomlang {
namespace compiler {
namespace ast {

/// \brief Stable syntax-node handle owned by an ast::Tree.
///
/// NodeId{0} is empty. Valid tree nodes start at one so NodeId::value can index
/// side tables directly after subtracting one.
struct NodeId final {
  uint32_t value = 0;

  constexpr NodeId() noexcept = default;
  constexpr explicit NodeId(uint32_t raw) noexcept : value(raw) {}

  constexpr explicit operator bool() const noexcept { return value != 0; }
  constexpr bool operator==(NodeId other) const noexcept { return value == other.value; }
  constexpr bool operator!=(NodeId other) const noexcept { return value != other.value; }
  constexpr bool operator<(NodeId other) const noexcept { return value < other.value; }
};

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
