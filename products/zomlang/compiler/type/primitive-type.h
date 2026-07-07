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

/// \brief PrimitiveType - Represents built-in primitive types.
///
/// Primitive types are the fundamental building blocks of the type system:
/// - Integer types: i8, i16, i32, i64, u8, u16, u32, u64
/// - Floating-point types: f32, f64
/// - Boolean: bool
/// - String: str
/// - Character: char
/// - Special: null, unit, never, any
///
/// Primitive types are singletons - there is only one instance of each.
/// Use the static factory methods to obtain instances.
class PrimitiveType final : public Type {
public:
  ~PrimitiveType() noexcept(false) override;

  ZC_DISALLOW_COPY(PrimitiveType);

  // Move semantics
  PrimitiveType(PrimitiveType&& other) noexcept;
  PrimitiveType& operator=(PrimitiveType&& other) noexcept;

  /// \brief Get the specific primitive kind.
  PrimitiveKind getPrimitiveKind() const;

  /// \brief Get the name of this primitive type (e.g., "i32", "bool").
  zc::StringPtr getName() const;

  /// \brief Get the size in bytes, or 0 for unsized types (never, any, unit, null).
  size_t getByteSize() const;

  /// \brief Check if this is an integer type.
  bool isIntegerType() const;

  /// \brief Check if this is a floating-point type.
  bool isFloatingPointType() const;

  /// \brief Check if this is a signed type.
  bool isSigned() const;

  // Type overrides
  TypeKind getKind() const override { return TypeKind::Primitive; }
  zc::String toString() const override;
  bool equals(const Type& other) const override;
  bool isSubtypeOf(const Type& other) const override;

  /// \name Singleton factory methods
  /// Each returns the canonical instance of the given primitive type.
  /// @{
  static zc::Own<PrimitiveType> createI8();
  static zc::Own<PrimitiveType> createI16();
  static zc::Own<PrimitiveType> createI32();
  static zc::Own<PrimitiveType> createI64();
  static zc::Own<PrimitiveType> createU8();
  static zc::Own<PrimitiveType> createU16();
  static zc::Own<PrimitiveType> createU32();
  static zc::Own<PrimitiveType> createU64();
  static zc::Own<PrimitiveType> createF32();
  static zc::Own<PrimitiveType> createF64();
  static zc::Own<PrimitiveType> createBool();
  static zc::Own<PrimitiveType> createStr();
  static zc::Own<PrimitiveType> createChar();
  static zc::Own<PrimitiveType> createNull();
  static zc::Own<PrimitiveType> createUnit();
  static zc::Own<PrimitiveType> createNever();
  static zc::Own<PrimitiveType> createAny();
  /// @}

  /// \brief Create a primitive type from its kind.
  static zc::Own<PrimitiveType> create(PrimitiveKind kind);

  /// \brief Construct a primitive type. Prefer using the static factory methods.
  explicit PrimitiveType(PrimitiveKind kind);

  /// \brief Look up a primitive type by name. Returns null if not found.
  static zc::Maybe<PrimitiveKind> findByName(zc::StringPtr name);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
