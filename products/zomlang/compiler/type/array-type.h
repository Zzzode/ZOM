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

#include "zomlang/compiler/type/type.h"

namespace zomlang {
namespace compiler {
namespace type {

/// \brief ArrayType - Represents homogeneous array types.
///
/// Arrays are dynamically-sized, homogeneous collections.
/// Example: `i32[]`, `str[]`
///
/// Array types are covariant in their element type for reading,
/// but invariant for writing (Liskov substitution principle).
/// For simplicity, we treat arrays as covariant in the type system
/// and rely on runtime checks for write safety.
class ArrayType final : public Type {
public:
  /// \brief Construct an array type with the given element type.
  explicit ArrayType(zc::Own<Type> elementType);

  ~ArrayType() noexcept(false) override;

  ZC_DISALLOW_COPY(ArrayType);

  // Move semantics
  ArrayType(ArrayType&& other) noexcept;
  ArrayType& operator=(ArrayType&& other) noexcept;

  /// \brief Get the element type.
  const Type& getElementType() const;

  // Type overrides
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
