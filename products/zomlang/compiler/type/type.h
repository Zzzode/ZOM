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

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"

namespace zomlang {
namespace compiler {

namespace type {

/// \brief Enumeration of all type forms in the ZOM type system.
///
/// Follows RFC 0005 Type System specification with 15 type forms.
enum class TypeKind {
  Primitive,     ///< Primitive types (i8, i32, f64, bool, str, etc.)
  Function,      ///< Function types (params) -> return raises E
  Tuple,         ///< Tuple types (T1, T2, ...)
  Object,        ///< Object types { name: T, ... }
  Array,         ///< Array types T[]
  Named,         ///< Named type references (class, interface, type alias)
  TypeVar,       ///< Type variables for generic inference
  Error,         ///< Error recovery placeholder type
  Interface,     ///< Interface types
  Union,         ///< Union types A | B
  Intersection,  ///< Intersection types A & B
  Reference,     ///< Reference types &T, &mut T
  RawPointer,    ///< Raw pointer types *const T, *mut T
  Existential,   ///< Existential types dyn Interface
  Associated     ///< Associated types T::Item
};

/// \brief Primitive type enumeration.
enum class PrimitiveKind {
  I8,     ///< 8-bit signed integer
  I16,    ///< 16-bit signed integer
  I32,    ///< 32-bit signed integer
  I64,    ///< 64-bit signed integer
  U8,     ///< 8-bit unsigned integer
  U16,    ///< 16-bit unsigned integer
  U32,    ///< 32-bit unsigned integer
  U64,    ///< 64-bit unsigned integer
  F32,    ///< 32-bit floating point
  F64,    ///< 64-bit floating point
  Bool,   ///< Boolean
  Str,    ///< String
  Char,   ///< Character
  Null,   ///< Null value
  Unit,   ///< Unit / void
  Never,  ///< Never / bottom type
  Any     ///< Any / top type
};

/// \brief Mutability qualifier for reference and pointer types.
enum class Mutability {
  Const,   ///< Immutable access
  Mutable  ///< Mutable access
};

// Forward declarations
class Type;
class PrimitiveType;
class FunctionType;
class TupleType;
class ObjectType;
class ArrayType;
class NamedType;
class TypeVar;
class ErrorType;
class InterfaceType;
class UnionType;
class IntersectionType;
class ReferenceType;
class RawPointerType;
class ExistentialType;
class AssociatedType;

/// \brief Type - Abstract base class for all type forms in ZOM.
///
/// The Type class represents the core of the ZOM type representation system.
/// Each concrete subclass represents one of the 15 type forms defined in
/// RFC 0005. Types use PIMPL for encapsulation and support:
///
/// - Structural equality via equals()
/// - Subtype checking via isSubtypeOf()
/// - String representation via toString()
///
/// Subtype rules (RFC 0005):
/// - never <: T
/// - T <: any
/// - &mut T <: &T
/// - *mut T <: *const T
/// - T <: T | E at coercion sites
/// - null <: T | null
class Type {
public:
  virtual ~Type() noexcept(false);

  ZC_DISALLOW_COPY(Type);

  // Move semantics
  Type(Type&& other) noexcept;
  Type& operator=(Type&& other) noexcept;

  /// \brief Get the type kind discriminator.
  virtual TypeKind getKind() const = 0;

  /// \brief Type classification convenience methods.
  bool isPrimitive() const;
  bool isError() const;
  bool isNever() const;
  bool isUnit() const;
  bool isNull() const;
  bool isAny() const;
  bool isFunction() const;
  bool isTuple() const;
  bool isObject() const;
  bool isArray() const;
  bool isNamed() const;
  bool isTypeVar() const;
  bool isInterface() const;
  bool isUnion() const;
  bool isIntersection() const;
  bool isReference() const;
  bool isRawPointer() const;
  bool isExistential() const;
  bool isAssociated() const;

  /// \brief Check if this is a numeric primitive type.
  bool isNumeric() const;

  /// \brief Check if this is an integer primitive type.
  bool isInteger() const;

  /// \brief Check if this is a floating-point primitive type.
  bool isFloatingPoint() const;

  /// \brief Check if this is a signed integer type.
  bool isSignedInteger() const;

  /// \brief Check if this is an unsigned integer type.
  bool isUnsignedInteger() const;

  /// \brief Produce a human-readable string representation of this type.
  virtual zc::String toString() const = 0;

  /// \brief Structural equality comparison.
  ///
  /// Two types are equal if they have the same kind and structurally equivalent
  /// components. For named types, equality is nominal (same name).
  virtual bool equals(const Type& other) const = 0;

  /// \brief Subtype relationship check.
  ///
  /// Returns true if this type is a subtype of `other` according to the
  /// subtyping rules defined in RFC 0005.
  virtual bool isSubtypeOf(const Type& other) const = 0;

  /// \brief Check if this type is assignable to the other type.
  ///
  /// Assignment compatibility extends subtyping with additional rules
  /// like numeric widening.
  bool isAssignableTo(const Type& other) const;

  /// \brief Shared subtype rules used by concrete type implementations.
  bool hasBasicSubtypeRelation(const Type& other) const;

protected:
  Type() noexcept;
};

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
