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

struct Type::Impl {
  TypeKind kind;

  explicit Impl(TypeKind k) : kind(k) {}
};

Type::Type(TypeKind kind) : impl(zc::heap<Impl>(kind)) {}

Type::~Type() noexcept(false) = default;

Type::Type(Type&& other) noexcept = default;

Type& Type::operator=(Type&& other) noexcept = default;

TypeKind Type::getKind() const { return impl->kind; }

bool Type::isPrimitive() const { return getKind() == TypeKind::Primitive; }

bool Type::isError() const { return getKind() == TypeKind::Error; }

bool Type::isNever() const {
  if (auto* prim = dynamic_cast<const PrimitiveType*>(this)) {
    return prim->getPrimitiveKind() == PrimitiveKind::Never;
  }
  return false;
}

bool Type::isUnit() const {
  if (auto* prim = dynamic_cast<const PrimitiveType*>(this)) {
    return prim->getPrimitiveKind() == PrimitiveKind::Unit;
  }
  return false;
}

bool Type::isNull() const {
  if (auto* prim = dynamic_cast<const PrimitiveType*>(this)) {
    return prim->getPrimitiveKind() == PrimitiveKind::Null;
  }
  return false;
}

bool Type::isAny() const {
  if (auto* prim = dynamic_cast<const PrimitiveType*>(this)) {
    return prim->getPrimitiveKind() == PrimitiveKind::Any;
  }
  return false;
}

bool Type::isFunction() const { return getKind() == TypeKind::Function; }

bool Type::isTuple() const { return getKind() == TypeKind::Tuple; }

bool Type::isObject() const { return getKind() == TypeKind::Object; }

bool Type::isArray() const { return getKind() == TypeKind::Array; }

bool Type::isNamed() const { return getKind() == TypeKind::Named; }

bool Type::isTypeVar() const { return getKind() == TypeKind::TypeVar; }

bool Type::isInterface() const { return getKind() == TypeKind::Interface; }

bool Type::isUnion() const { return getKind() == TypeKind::Union; }

bool Type::isIntersection() const { return getKind() == TypeKind::Intersection; }

bool Type::isReference() const { return getKind() == TypeKind::Reference; }

bool Type::isRawPointer() const { return getKind() == TypeKind::RawPointer; }

bool Type::isExistential() const { return getKind() == TypeKind::Existential; }

bool Type::isAssociated() const { return getKind() == TypeKind::Associated; }

bool Type::isNumeric() const {
  if (auto* prim = dynamic_cast<const PrimitiveType*>(this)) {
    auto k = prim->getPrimitiveKind();
    return k >= PrimitiveKind::I8 && k <= PrimitiveKind::F64;
  }
  return false;
}

bool Type::isInteger() const {
  if (auto* prim = dynamic_cast<const PrimitiveType*>(this)) {
    auto k = prim->getPrimitiveKind();
    return k >= PrimitiveKind::I8 && k <= PrimitiveKind::U64;
  }
  return false;
}

bool Type::isFloatingPoint() const {
  if (auto* prim = dynamic_cast<const PrimitiveType*>(this)) {
    auto k = prim->getPrimitiveKind();
    return k == PrimitiveKind::F32 || k == PrimitiveKind::F64;
  }
  return false;
}

bool Type::isSignedInteger() const {
  if (auto* prim = dynamic_cast<const PrimitiveType*>(this)) {
    auto k = prim->getPrimitiveKind();
    return k >= PrimitiveKind::I8 && k <= PrimitiveKind::I64;
  }
  return false;
}

bool Type::isUnsignedInteger() const {
  if (auto* prim = dynamic_cast<const PrimitiveType*>(this)) {
    auto k = prim->getPrimitiveKind();
    return k >= PrimitiveKind::U8 && k <= PrimitiveKind::U64;
  }
  return false;
}

bool Type::isSubtypeOf(const Type& other) const {
  // Identity: T ⊂ T
  if (this == &other) { return true; }

  // never ⊂ all
  if (isNever()) { return true; }

  // all ⊂ any
  if (other.isAny()) { return true; }

  // Error type is compatible with everything (both directions)
  // If this is error, it's compatible with anything (handled by ErrorType::isSubtypeOf override)
  // If other is error, anything is compatible with it
  if (other.isError()) { return true; }

  // Structural equality implies subtyping
  if (equals(other)) { return true; }

  return false;
}

bool Type::isAssignableTo(const Type& other) const {
  // Subtyping implies assignability
  if (isSubtypeOf(other)) { return true; }

  // Numeric widening
  if (isNumeric() && other.isNumeric()) {
    auto* thisPrim = dynamic_cast<const PrimitiveType*>(this);
    auto* otherPrim = dynamic_cast<const PrimitiveType*>(&other);
    if (thisPrim && otherPrim) {
      // Integer widening: smaller to larger
      auto thisK = thisPrim->getPrimitiveKind();
      auto otherK = otherPrim->getPrimitiveKind();

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
      if (isInteger() && otherK == PrimitiveKind::F32) { return true; }
      if (isInteger() && otherK == PrimitiveKind::F64) { return true; }
    }
  }

  return false;
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
