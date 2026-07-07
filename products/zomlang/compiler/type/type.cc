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

#include "zomlang/compiler/type/type.h"

#include "zomlang/compiler/type/primitive-type.h"

namespace zomlang {
namespace compiler {
namespace type {

bool isPrimitive(const Type& type) { return type.getKind() == TypeKind::Primitive; }

bool isError(const Type& type) { return type.getKind() == TypeKind::Error; }

bool isNever(const Type& type) {
  return isPrimitive(type) &&
         static_cast<const PrimitiveType&>(type).getPrimitiveKind() == PrimitiveKind::Never;
}

bool isUnit(const Type& type) {
  return isPrimitive(type) &&
         static_cast<const PrimitiveType&>(type).getPrimitiveKind() == PrimitiveKind::Unit;
}

bool isNull(const Type& type) {
  return isPrimitive(type) &&
         static_cast<const PrimitiveType&>(type).getPrimitiveKind() == PrimitiveKind::Null;
}

bool isAny(const Type& type) {
  return isPrimitive(type) &&
         static_cast<const PrimitiveType&>(type).getPrimitiveKind() == PrimitiveKind::Any;
}

bool isFunction(const Type& type) { return type.getKind() == TypeKind::Function; }

bool isTuple(const Type& type) { return type.getKind() == TypeKind::Tuple; }

bool isObject(const Type& type) { return type.getKind() == TypeKind::Object; }

bool isArray(const Type& type) { return type.getKind() == TypeKind::Array; }

bool isNamed(const Type& type) { return type.getKind() == TypeKind::Named; }

bool isTypeVar(const Type& type) { return type.getKind() == TypeKind::TypeVar; }

bool isInterface(const Type& type) { return type.getKind() == TypeKind::Interface; }

bool isUnion(const Type& type) { return type.getKind() == TypeKind::Union; }

bool isIntersection(const Type& type) { return type.getKind() == TypeKind::Intersection; }

bool isReference(const Type& type) { return type.getKind() == TypeKind::Reference; }

bool isRawPointer(const Type& type) { return type.getKind() == TypeKind::RawPointer; }

bool isExistential(const Type& type) { return type.getKind() == TypeKind::Existential; }

bool isAssociated(const Type& type) { return type.getKind() == TypeKind::Associated; }

bool isNumeric(const Type& type) {
  if (!isPrimitive(type)) return false;
  auto k = static_cast<const PrimitiveType&>(type).getPrimitiveKind();
  return k >= PrimitiveKind::I8 && k <= PrimitiveKind::F64;
}

bool isInteger(const Type& type) {
  if (!isPrimitive(type)) return false;
  auto k = static_cast<const PrimitiveType&>(type).getPrimitiveKind();
  return k >= PrimitiveKind::I8 && k <= PrimitiveKind::U64;
}

bool isFloatingPoint(const Type& type) {
  if (!isPrimitive(type)) return false;
  auto k = static_cast<const PrimitiveType&>(type).getPrimitiveKind();
  return k == PrimitiveKind::F32 || k == PrimitiveKind::F64;
}

bool isSignedInteger(const Type& type) {
  if (!isPrimitive(type)) return false;
  auto k = static_cast<const PrimitiveType&>(type).getPrimitiveKind();
  return k >= PrimitiveKind::I8 && k <= PrimitiveKind::I64;
}

bool isUnsignedInteger(const Type& type) {
  if (!isPrimitive(type)) return false;
  auto k = static_cast<const PrimitiveType&>(type).getPrimitiveKind();
  return k >= PrimitiveKind::U8 && k <= PrimitiveKind::U64;
}

bool hasBasicSubtypeRelation(const Type& type, const Type& other) {
  // Identity: T ⊂ T
  if (&type == &other) { return true; }

  // never ⊂ all
  if (isNever(type)) { return true; }

  // all ⊂ any
  if (isAny(other)) { return true; }

  // Error type is compatible with everything (both directions)
  // If this is error, it's compatible with anything (handled by ErrorType::isSubtypeOf override)
  // If other is error, anything is compatible with it
  if (isError(other)) { return true; }

  // Structural equality implies subtyping
  if (type.equals(other)) { return true; }

  return false;
}

bool isAssignableTo(const Type& type, const Type& other) {
  // Subtyping implies assignability
  if (type.isSubtypeOf(other)) { return true; }

  // Numeric widening
  if (isNumeric(type) && isNumeric(other)) {
    auto thisK = static_cast<const PrimitiveType&>(type).getPrimitiveKind();
    auto otherK = static_cast<const PrimitiveType&>(other).getPrimitiveKind();

    // Signed integer widening
    if (thisK == PrimitiveKind::I8 &&
        (otherK == PrimitiveKind::I16 || otherK == PrimitiveKind::I32 ||
         otherK == PrimitiveKind::I64 || otherK == PrimitiveKind::F32 ||
         otherK == PrimitiveKind::F64)) {
      return true;
    }
    if (thisK == PrimitiveKind::I16 &&
        (otherK == PrimitiveKind::I32 || otherK == PrimitiveKind::I64 ||
         otherK == PrimitiveKind::F32 || otherK == PrimitiveKind::F64)) {
      return true;
    }
    if (thisK == PrimitiveKind::I32 &&
        (otherK == PrimitiveKind::I64 || otherK == PrimitiveKind::F64)) {
      return true;
    }

    // Unsigned integer widening
    if (thisK == PrimitiveKind::U8 &&
        (otherK == PrimitiveKind::U16 || otherK == PrimitiveKind::U32 ||
         otherK == PrimitiveKind::U64 || otherK == PrimitiveKind::F32 ||
         otherK == PrimitiveKind::F64)) {
      return true;
    }
    if (thisK == PrimitiveKind::U16 &&
        (otherK == PrimitiveKind::U32 || otherK == PrimitiveKind::U64 ||
         otherK == PrimitiveKind::F32 || otherK == PrimitiveKind::F64)) {
      return true;
    }
    if (thisK == PrimitiveKind::U32 &&
        (otherK == PrimitiveKind::U64 || otherK == PrimitiveKind::F64)) {
      return true;
    }

    // Float widening
    if (thisK == PrimitiveKind::F32 && otherK == PrimitiveKind::F64) { return true; }

    // Int to float (lossy but allowed)
    if (isInteger(type) && otherK == PrimitiveKind::F32) { return true; }
    if (isInteger(type) && otherK == PrimitiveKind::F64) { return true; }
  }

  return false;
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
