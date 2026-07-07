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

/// \brief ExistentialType - Represents existential types (dyn Interface).
///
/// Existential types enable dynamic dispatch through type erasure,
/// similar to Rust's `dyn Trait` or Go's interface values.
///
/// Example: `dyn Drawable` - a value of some unknown type that implements
/// the `Drawable` interface.
///
/// An existential type packages:
/// - A reference to the interface being satisfied
/// - An implicit "witness" type (the concrete type, erased at runtime)
///
/// Existential types are always used behind a reference or pointer for
/// soundness (the concrete type has unknown size).
class ExistentialType final : public Type {
public:
  /// \brief Construct an existential type for the given interface.
  explicit ExistentialType(zc::Own<Type> interfaceType);

  ~ExistentialType() noexcept(false);

  ZC_DISALLOW_COPY(ExistentialType);

  // Move semantics
  ExistentialType(ExistentialType&& other) noexcept;
  ExistentialType& operator=(ExistentialType&& other) noexcept;

  /// \brief Get the interface type that the existential satisfies.
  const Type& getInterfaceType() const;

  // Type overrides
  TypeKind getKind() const override { return TypeKind::Existential; }
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
