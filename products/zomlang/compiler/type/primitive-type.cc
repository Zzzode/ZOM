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

#include "zomlang/compiler/type/primitive-type.h"

#include "zomlang/compiler/type/union-type.h"

namespace zomlang {
namespace compiler {
namespace type {

struct PrimitiveType::Impl {
  PrimitiveKind kind;

  explicit Impl(PrimitiveKind k) : kind(k) {}
};

PrimitiveType::PrimitiveType(PrimitiveKind kind)
    : Type(TypeKind::Primitive), impl(zc::heap<Impl>(kind)) {}

PrimitiveType::~PrimitiveType() noexcept(false) = default;

PrimitiveType::PrimitiveType(PrimitiveType&& other) noexcept = default;

PrimitiveType& PrimitiveType::operator=(PrimitiveType&& other) noexcept = default;

PrimitiveKind PrimitiveType::getPrimitiveKind() const { return impl->kind; }

zc::StringPtr PrimitiveType::getName() const {
  switch (impl->kind) {
    case PrimitiveKind::I8:
      return "i8"_zc;
    case PrimitiveKind::I16:
      return "i16"_zc;
    case PrimitiveKind::I32:
      return "i32"_zc;
    case PrimitiveKind::I64:
      return "i64"_zc;
    case PrimitiveKind::U8:
      return "u8"_zc;
    case PrimitiveKind::U16:
      return "u16"_zc;
    case PrimitiveKind::U32:
      return "u32"_zc;
    case PrimitiveKind::U64:
      return "u64"_zc;
    case PrimitiveKind::F32:
      return "f32"_zc;
    case PrimitiveKind::F64:
      return "f64"_zc;
    case PrimitiveKind::Bool:
      return "bool"_zc;
    case PrimitiveKind::Str:
      return "str"_zc;
    case PrimitiveKind::Char:
      return "char"_zc;
    case PrimitiveKind::Null:
      return "null"_zc;
    case PrimitiveKind::Unit:
      return "unit"_zc;
    case PrimitiveKind::Never:
      return "never"_zc;
    case PrimitiveKind::Any:
      return "any"_zc;
  }
  return "<unknown>"_zc;
}

size_t PrimitiveType::getByteSize() const {
  switch (impl->kind) {
    case PrimitiveKind::I8:
    case PrimitiveKind::U8:
    case PrimitiveKind::Bool:
    case PrimitiveKind::Char:
      return 1;
    case PrimitiveKind::I16:
    case PrimitiveKind::U16:
      return 2;
    case PrimitiveKind::I32:
    case PrimitiveKind::U32:
    case PrimitiveKind::F32:
      return 4;
    case PrimitiveKind::I64:
    case PrimitiveKind::U64:
    case PrimitiveKind::F64:
      return 8;
    case PrimitiveKind::Str:
      // String is a fat pointer (ptr + len)
      return sizeof(void*) * 2;
    case PrimitiveKind::Null:
    case PrimitiveKind::Unit:
    case PrimitiveKind::Never:
    case PrimitiveKind::Any:
      return 0;
  }
  return 0;
}

bool PrimitiveType::isIntegerType() const {
  auto k = impl->kind;
  return k >= PrimitiveKind::I8 && k <= PrimitiveKind::U64;
}

bool PrimitiveType::isFloatingPointType() const {
  return impl->kind == PrimitiveKind::F32 || impl->kind == PrimitiveKind::F64;
}

bool PrimitiveType::isSigned() const {
  auto k = impl->kind;
  return k >= PrimitiveKind::I8 && k <= PrimitiveKind::I64;
}

zc::String PrimitiveType::toString() const { return zc::heapString(getName()); }

bool PrimitiveType::equals(const Type& other) const {
  if (this == &other) { return true; }
  if (other.getKind() != TypeKind::Primitive) { return false; }
  auto& otherPrim = static_cast<const PrimitiveType&>(other);
  return impl->kind == otherPrim.impl->kind;
}

bool PrimitiveType::isSubtypeOf(const Type& other) const {
  // Check base rules first (identity, never, any)
  if (Type::isSubtypeOf(other)) { return true; }

  if (impl->kind == PrimitiveKind::Null && other.isUnion()) {
    auto& unionTy = static_cast<const UnionType&>(other);
    return unionTy.isNullable();
  }

  // Unit is a subtype of itself only (handled by identity)
  // Same-type primitives are handled by equals() in Type::isSubtypeOf

  return false;
}

// Factory methods
zc::Own<PrimitiveType> PrimitiveType::createI8() {
  return zc::heap<PrimitiveType>(PrimitiveKind::I8);
}

zc::Own<PrimitiveType> PrimitiveType::createI16() {
  return zc::heap<PrimitiveType>(PrimitiveKind::I16);
}

zc::Own<PrimitiveType> PrimitiveType::createI32() {
  return zc::heap<PrimitiveType>(PrimitiveKind::I32);
}

zc::Own<PrimitiveType> PrimitiveType::createI64() {
  return zc::heap<PrimitiveType>(PrimitiveKind::I64);
}

zc::Own<PrimitiveType> PrimitiveType::createU8() {
  return zc::heap<PrimitiveType>(PrimitiveKind::U8);
}

zc::Own<PrimitiveType> PrimitiveType::createU16() {
  return zc::heap<PrimitiveType>(PrimitiveKind::U16);
}

zc::Own<PrimitiveType> PrimitiveType::createU32() {
  return zc::heap<PrimitiveType>(PrimitiveKind::U32);
}

zc::Own<PrimitiveType> PrimitiveType::createU64() {
  return zc::heap<PrimitiveType>(PrimitiveKind::U64);
}

zc::Own<PrimitiveType> PrimitiveType::createF32() {
  return zc::heap<PrimitiveType>(PrimitiveKind::F32);
}

zc::Own<PrimitiveType> PrimitiveType::createF64() {
  return zc::heap<PrimitiveType>(PrimitiveKind::F64);
}

zc::Own<PrimitiveType> PrimitiveType::createBool() {
  return zc::heap<PrimitiveType>(PrimitiveKind::Bool);
}

zc::Own<PrimitiveType> PrimitiveType::createStr() {
  return zc::heap<PrimitiveType>(PrimitiveKind::Str);
}

zc::Own<PrimitiveType> PrimitiveType::createChar() {
  return zc::heap<PrimitiveType>(PrimitiveKind::Char);
}

zc::Own<PrimitiveType> PrimitiveType::createNull() {
  return zc::heap<PrimitiveType>(PrimitiveKind::Null);
}

zc::Own<PrimitiveType> PrimitiveType::createUnit() {
  return zc::heap<PrimitiveType>(PrimitiveKind::Unit);
}

zc::Own<PrimitiveType> PrimitiveType::createNever() {
  return zc::heap<PrimitiveType>(PrimitiveKind::Never);
}

zc::Own<PrimitiveType> PrimitiveType::createAny() {
  return zc::heap<PrimitiveType>(PrimitiveKind::Any);
}

zc::Own<PrimitiveType> PrimitiveType::create(PrimitiveKind kind) {
  return zc::heap<PrimitiveType>(kind);
}

zc::Maybe<PrimitiveKind> PrimitiveType::findByName(zc::StringPtr name) {
  if (name == "i8"_zc) return PrimitiveKind::I8;
  if (name == "i16"_zc) return PrimitiveKind::I16;
  if (name == "i32"_zc) return PrimitiveKind::I32;
  if (name == "i64"_zc) return PrimitiveKind::I64;
  if (name == "u8"_zc) return PrimitiveKind::U8;
  if (name == "u16"_zc) return PrimitiveKind::U16;
  if (name == "u32"_zc) return PrimitiveKind::U32;
  if (name == "u64"_zc) return PrimitiveKind::U64;
  if (name == "f32"_zc) return PrimitiveKind::F32;
  if (name == "f64"_zc) return PrimitiveKind::F64;
  if (name == "bool"_zc) return PrimitiveKind::Bool;
  if (name == "str"_zc) return PrimitiveKind::Str;
  if (name == "char"_zc) return PrimitiveKind::Char;
  if (name == "null"_zc) return PrimitiveKind::Null;
  if (name == "unit"_zc) return PrimitiveKind::Unit;
  if (name == "never"_zc) return PrimitiveKind::Never;
  if (name == "any"_zc) return PrimitiveKind::Any;
  return zc::none;
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
