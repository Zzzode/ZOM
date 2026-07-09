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

#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/type/type.h"

namespace zomlang {
namespace compiler {
namespace type {

/// \brief TypeVar - Represents type variables for generic type inference.
///
/// Type variables are used in:
/// - Generic function/type parameters (e.g., `T` in `fn identity<T>(x: T) -> T`)
/// - Type inference during constraint solving
/// - Existential quantification
///
/// Each TypeVar has a unique name and optional bounds. During type inference,
/// type variables may be resolved to concrete types.
class TypeVar final : public Type {
public:
  /// \brief Construct a type variable with a name and unique ID.
  TypeVar(zc::StringPtr name, uint64_t id);

  ~TypeVar() noexcept(false);

  ZC_DISALLOW_COPY(TypeVar);

  // Move semantics
  TypeVar(TypeVar&& other) noexcept;
  TypeVar& operator=(TypeVar&& other) noexcept;

  /// \brief Get the type variable name.
  zc::StringPtr getName() const;

  /// \brief Get the unique ID for this type variable.
  uint64_t getId() const;

  /// \brief Add an upper bound (T :> Bound means T must be subtype of Bound).
  void addUpperBound(zc::Own<Type> bound);

  /// \brief Add a lower bound (T <: Bound means Bound must be subtype of T).
  void addLowerBound(zc::Own<Type> bound);

  /// \brief Get the number of upper bounds.
  size_t getUpperBoundCount() const;

  /// \brief Get an upper bound by index.
  const Type& getUpperBound(size_t index) const;

  /// \brief Get the number of lower bounds.
  size_t getLowerBoundCount() const;

  /// \brief Get a lower bound by index.
  const Type& getLowerBound(size_t index) const;

  // Type overrides
  TypeKind getKind() const override { return TypeKind::TypeVar; }
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
