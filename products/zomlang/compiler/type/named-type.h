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
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/symbol/symbol-id.h"
#include "zomlang/compiler/type/type.h"

namespace zomlang {
namespace compiler {

namespace symbol {
class TypeSymbol;
}  // namespace symbol

namespace type {

/// \brief NamedType - Represents named type references.
///
/// Named types reference user-defined types by name:
/// - Class types (e.g., `String`, `MyClass`)
/// - Interface types (e.g., `Drawable`)
/// - Type aliases (e.g., `UserId = i64`)
/// - Enum types
///
/// Named types support nominal subtyping via their TypeSymbol.
/// They may also carry generic type arguments (e.g., `Vec<i32>`).
class NamedType final : public Type {
public:
  /// \brief Construct a named type from a type symbol.
  explicit NamedType(zc::StringPtr name);

  /// \brief Construct a named type with a resolved symbol reference.
  NamedType(zc::StringPtr name, const symbol::TypeSymbol& symbol);

  ~NamedType() noexcept(false);

  ZC_DISALLOW_COPY(NamedType);

  // Move semantics
  NamedType(NamedType&& other) noexcept;
  NamedType& operator=(NamedType&& other) noexcept;

  /// \brief Get the type name.
  zc::StringPtr getName() const;

  /// \brief Get the resolved type symbol, if available.
  zc::Maybe<const symbol::TypeSymbol&> getSymbol() const;

  /// \brief Set the resolved type symbol.
  void setSymbol(const symbol::TypeSymbol& symbol);

  /// \brief Get the number of generic type arguments.
  size_t getTypeArgCount() const;

  /// \brief Get a type argument by index.
  const Type& getTypeArg(size_t index) const;

  /// \brief Add a type argument.
  void addTypeArg(zc::Own<Type> arg);

  /// \brief Get all type arguments.
  zc::ArrayPtr<const zc::Own<Type>> getTypeArgs() const;

  // Type overrides
  TypeKind getKind() const override { return TypeKind::Named; }
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
