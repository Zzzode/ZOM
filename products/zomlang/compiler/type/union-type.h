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

#include "zc/core/array.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/type/type.h"

namespace zomlang {
namespace compiler {
namespace type {

/// \brief UnionType - Represents union types (A | B).
///
/// A union type `A | B` represents a value that can be either type A or type B.
/// This is the join (least upper bound) in the type lattice.
///
/// Examples:
/// - `i32 | str` - value is either an integer or a string
/// - `null | T` - nullable type (T or null)
/// - `i32 | f32 | str` - multi-way union
///
/// Union types are automatically flattened: `(A | B) | C` becomes `A | B | C`.
class UnionType final : public Type {
public:
  /// \brief Construct a union type from alternatives.
  explicit UnionType(zc::Vector<zc::Own<Type>> alternatives);

  ~UnionType() noexcept(false) override;

  ZC_DISALLOW_COPY(UnionType);

  // Move semantics
  UnionType(UnionType&& other) noexcept;
  UnionType& operator=(UnionType&& other) noexcept;

  /// \brief Get the number of alternatives.
  size_t getAlternativeCount() const;

  /// \brief Get an alternative by index.
  const Type& getAlternative(size_t index) const;

  /// \brief Check if this union contains a specific type.
  bool contains(const Type& type) const;

  /// \brief Check if this is a nullable type (contains null).
  bool isNullable() const;

  // Type overrides
  TypeKind getKind() const override { return TypeKind::Union; }
  zc::String toString() const override;
  bool equals(const Type& other) const override;
  bool isSubtypeOf(const Type& other) const override;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
