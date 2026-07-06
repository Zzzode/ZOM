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

#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"

namespace zomlang {
namespace compiler {
namespace type {

class Type;
class TypeVar;
class TypeEnv;

/// \brief GenericParam - Represents a single generic parameter declaration.
///
/// Each generic parameter has:
/// - A name (e.g., "T" in `fn identity<T>(x: T) -> T`)
/// - Optional upper bounds (e.g., "T: Hashable" means T must implement Hashable)
/// - Optional lower bounds
struct GenericParam {
  zc::StringPtr name;
  zc::Vector<zc::Own<Type>> upperBounds;  // T :> Bound (T must be subtype of Bound)
  zc::Vector<zc::Own<Type>> lowerBounds;  // T <: Bound (Bound must be subtype of T)

  GenericParam(zc::StringPtr n) : name(n) {}
};

/// \brief TypeScheme - Represents a type scheme (∀α.τ) for let-polymorphism.
///
/// A type scheme quantifies over type variables, allowing the same
/// definition to be used at different types in different contexts.
/// This is the core of Hindley-Milner let-polymorphism.
///
/// For example:
///   fn identity<T>(x: T) -> T { return x }
///
/// Has type scheme:
///   ∀T. (T) -> T
///
/// At each call site, the type scheme is *instantiated* by creating
/// fresh type variables for the quantified parameters:
///   identity("hello")  →  (?X) -> ?X  where ?X is fresh, then ?X = str
///   identity(42)       →  (?Y) -> ?Y  where ?Y is fresh, then ?Y = i32
///
/// Without instantiation, calling identity("hello") would bind T=str,
/// and then identity(42) would fail because T is already str.
class TypeScheme {
public:
  /// \brief Construct a type scheme with quantified generic parameters.
  ///
  /// \param params The generic parameters being quantified over.
  /// \param body   The type body (may reference the generic parameters).
  TypeScheme(zc::Vector<zc::Own<GenericParam>> params, zc::Own<Type> body);

  ~TypeScheme() noexcept(false);

  ZC_DISALLOW_COPY(TypeScheme);

  // Move semantics
  TypeScheme(TypeScheme&& other) noexcept;
  TypeScheme& operator=(TypeScheme&& other) noexcept;

  /// \brief Get the number of quantified generic parameters.
  size_t getParamCount() const;

  /// \brief Get a generic parameter by index.
  const GenericParam& getParam(size_t index) const;

  /// \brief Get the type body (the monotype under the quantifier).
  const Type& getBody() const;

  /// \brief Check if this type scheme is monomorphic (no quantified params).
  ///
  /// A monomorphic type scheme has ∀∅.τ, which is just τ.
  bool isMonomorphic() const;

  /// \brief Produce a human-readable string representation.
  ///
  /// Example: "∀T. (T) -> T"
  zc::String toString() const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
