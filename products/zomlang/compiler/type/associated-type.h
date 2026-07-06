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

#include "zc/core/string.h"
#include "zomlang/compiler/type/type.h"

namespace zomlang {
namespace compiler {
namespace type {

/// \brief AssociatedType - Represents associated types (T::Item).
///
/// Associated types are type members of interfaces/traits, accessed via
/// the `::` syntax. They allow interfaces to define types that are
/// determined by the implementing type.
///
/// Example:
/// ```
/// interface Iterator {
///   type Item;
///   fn next(self: &mut Self) -> Option<Self::Item>;
/// }
/// ```
///
/// `T::Item` refers to the associated type `Item` of type `T`.
class AssociatedType final : public Type {
public:
  /// \brief Construct an associated type.
  /// \param parent The parent type (e.g., T in T::Item)
  /// \param name The associated type name (e.g., "Item" in T::Item)
  AssociatedType(zc::Own<Type> parent, zc::StringPtr name);

  ~AssociatedType() noexcept(false) override;

  ZC_DISALLOW_COPY(AssociatedType);

  // Move semantics
  AssociatedType(AssociatedType&& other) noexcept;
  AssociatedType& operator=(AssociatedType&& other) noexcept;

  /// \brief Get the parent type.
  const Type& getParentType() const;

  /// \brief Get the associated type name.
  zc::StringPtr getName() const;

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
