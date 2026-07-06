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

#include "zc/core/map.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/type/type.h"

namespace zomlang {
namespace compiler {
namespace type {

/// \brief InterfaceType - Represents interface types (structural or nominal).
///
/// Interfaces define a set of method signatures that types can implement.
/// Example:
/// ```
/// interface Drawable {
///   fn draw(self: &Self) -> unit;
///   fn bounds(self: &Self) -> Rect;
/// }
/// ```
///
/// Interface types support:
/// - Interface inheritance (extends)
/// - Method signature specification
/// - Structural subtyping (duck typing) when enabled
class InterfaceType final : public Type {
public:
  /// \brief Method signature within an interface.
  struct MethodSignature {
    zc::StringPtr name;
    zc::Own<Type> type;  // FunctionType
  };

  /// \brief Construct an interface type with a name.
  explicit InterfaceType(zc::StringPtr name);

  ~InterfaceType() noexcept(false) override;

  ZC_DISALLOW_COPY(InterfaceType);

  // Move semantics
  InterfaceType(InterfaceType&& other) noexcept;
  InterfaceType& operator=(InterfaceType&& other) noexcept;

  /// \brief Get the interface name.
  zc::StringPtr getName() const;

  /// \brief Add a method signature to the interface.
  void addMethod(zc::StringPtr name, zc::Own<Type> methodType);

  /// \brief Get a method signature by name.
  zc::Maybe<const Type&> getMethod(zc::StringPtr name) const;

  /// \brief Get the number of methods.
  size_t getMethodCount() const;

  /// \brief Check if the interface has a specific method.
  bool hasMethod(zc::StringPtr name) const;

  /// \brief Add a parent interface (extends).
  void addParentInterface(zc::Own<Type> parent);

  /// \brief Get the number of parent interfaces.
  size_t getParentInterfaceCount() const;

  /// \brief Get a parent interface by index.
  const Type& getParentInterface(size_t index) const;

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
