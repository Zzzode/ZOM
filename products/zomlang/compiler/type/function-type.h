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

#include "zc/core/vector.h"
#include "zomlang/compiler/type/type-scheme.h"
#include "zomlang/compiler/type/type.h"

namespace zomlang {
namespace compiler {
namespace type {

/// \brief FunctionType - Represents function types.
///
/// A function type consists of:
/// - Parameter types (ordered list)
/// - Return type
/// - Optional raises type (exception type)
/// - Variadic flag
/// - Optional generic parameters (for polymorphic functions)
///
/// Example: `fn(i32, str) -> bool raises Error`
/// Example generic: `fn identity<T>(x: T) -> T`
class FunctionType final : public Type {
public:
  /// \brief Construct a function type with parameter and return types.
  FunctionType(zc::Vector<zc::Own<Type>> params, zc::Own<Type> returnType);

  ~FunctionType() noexcept(false) override;

  ZC_DISALLOW_COPY(FunctionType);

  // Move semantics
  FunctionType(FunctionType&& other) noexcept;
  FunctionType& operator=(FunctionType&& other) noexcept;

  /// \brief Get the number of parameters.
  size_t getParamCount() const;

  /// \brief Get the parameter type at the given index.
  const Type& getParamType(size_t index) const;

  /// \brief Get the return type.
  const Type& getReturnType() const;

  /// \brief Get the raises type, if any.
  zc::Maybe<const Type&> getRaisesType() const;

  /// \brief Set the raises type.
  void setRaisesType(zc::Own<Type> raises);

  /// \brief Check if this function is variadic.
  bool isVariadic() const;

  /// \brief Set variadic flag.
  void setVariadic(bool variadic);

  // =========================================================================
  // Generic parameter support (for let-polymorphism)
  // =========================================================================

  /// \brief Get the number of generic parameters.
  size_t getGenericParamCount() const;

  /// \brief Get a generic parameter by index.
  const GenericParam& getGenericParam(size_t index) const;

  /// \brief Add a generic parameter to this function type.
  void addGenericParam(zc::Own<GenericParam> param);

  /// \brief Check if this function type is generic (has type parameters).
  bool isGeneric() const;

  // Type overrides
  TypeKind getKind() const override { return TypeKind::Function; }
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
