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

/// \brief TupleType - Represents tuple types.
///
/// Tuples are ordered, fixed-size collections of potentially heterogeneous types.
/// Example: `(i32, str, bool)`
///
/// A tuple with zero elements is equivalent to `unit`.
/// A tuple with one element is just the element type itself (singleton tuple).
class TupleType final : public Type {
public:
  /// \brief Construct a tuple type from element types.
  explicit TupleType(zc::Vector<zc::Own<Type>> elements);

  ~TupleType() noexcept(false) override;

  ZC_DISALLOW_COPY(TupleType);

  // Move semantics
  TupleType(TupleType&& other) noexcept;
  TupleType& operator=(TupleType&& other) noexcept;

  /// \brief Get the number of elements.
  size_t getElementCount() const;

  /// \brief Get the element type at the given index.
  const Type& getElementType(size_t index) const;

  /// \brief Check if this is an empty tuple (unit type).
  bool isEmpty() const;

  // Type overrides
  TypeKind getKind() const override { return TypeKind::Tuple; }
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
