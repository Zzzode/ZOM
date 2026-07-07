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

/// \brief ReferenceType - Represents safe reference types.
///
/// References are non-nullable, safe pointers to values.
/// - `&T` - immutable shared reference
/// - `&mut T` - mutable exclusive reference
///
/// Subtyping: `&mut T ⊂ &T` (mutable references can be used as immutable)
///
/// References are invariant in their pointee type to prevent soundness issues.
class ReferenceType final : public Type {
public:
  /// \brief Construct a reference type.
  ReferenceType(zc::Own<Type> pointee, Mutability mutability);

  ~ReferenceType() noexcept(false);

  ZC_DISALLOW_COPY(ReferenceType);

  // Move semantics
  ReferenceType(ReferenceType&& other) noexcept;
  ReferenceType& operator=(ReferenceType&& other) noexcept;

  /// \brief Get the pointee type.
  const Type& getPointeeType() const;

  /// \brief Get the mutability qualifier.
  Mutability getMutability() const;

  /// \brief Check if this is a mutable reference.
  bool isMutable() const;

  // Type overrides
  TypeKind getKind() const override { return TypeKind::Reference; }
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
