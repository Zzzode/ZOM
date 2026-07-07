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

/// \brief IntersectionType - Represents intersection types (A & B).
///
/// An intersection type `A & B` represents a value that satisfies both
/// type A and type B simultaneously. This is the meet (greatest lower bound)
/// in the type lattice.
///
/// Intersection types are commonly used for:
/// - Mixin composition: combining multiple interfaces
/// - Overloaded function types
/// - Refinement types
///
/// Example: `Drawable & Clickable` - something that is both drawable and clickable.
class IntersectionType final : public Type {
public:
  /// \brief Construct an intersection type from conjuncts.
  explicit IntersectionType(zc::Vector<zc::Own<Type>> conjuncts);

  ~IntersectionType() noexcept(false) override;

  ZC_DISALLOW_COPY(IntersectionType);

  // Move semantics
  IntersectionType(IntersectionType&& other) noexcept;
  IntersectionType& operator=(IntersectionType&& other) noexcept;

  /// \brief Get the number of conjuncts.
  size_t getConjunctCount() const;

  /// \brief Get a conjunct by index.
  const Type& getConjunct(size_t index) const;

  // Type overrides
  TypeKind getKind() const override { return TypeKind::Intersection; }
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
