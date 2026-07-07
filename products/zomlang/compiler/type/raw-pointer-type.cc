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

#include "zomlang/compiler/type/raw-pointer-type.h"

namespace zomlang {
namespace compiler {
namespace type {

struct RawPointerType::Impl {
  zc::Own<Type> pointee;
  Mutability mutability;

  Impl(zc::Own<Type> p, Mutability m) : pointee(zc::mv(p)), mutability(m) {}
};

RawPointerType::RawPointerType(zc::Own<Type> pointee, Mutability mutability)
    : impl(zc::heap<Impl>(zc::mv(pointee), mutability)) {}

RawPointerType::~RawPointerType() noexcept(false) = default;

RawPointerType::RawPointerType(RawPointerType&& other) noexcept = default;

RawPointerType& RawPointerType::operator=(RawPointerType&& other) noexcept = default;

const Type& RawPointerType::getPointeeType() const { return *impl->pointee; }

Mutability RawPointerType::getMutability() const { return impl->mutability; }

bool RawPointerType::isMutable() const { return impl->mutability == Mutability::Mutable; }

zc::String RawPointerType::toString() const {
  if (impl->mutability == Mutability::Mutable) {
    return zc::str("*mut ", impl->pointee->toString());
  }
  return zc::str("*const ", impl->pointee->toString());
}

bool RawPointerType::equals(const Type& other) const {
  if (this == &other) { return true; }
  if (other.getKind() != TypeKind::RawPointer) { return false; }

  auto& otherPtr = static_cast<const RawPointerType&>(other);

  if (impl->mutability != otherPtr.impl->mutability) { return false; }

  return impl->pointee->equals(*otherPtr.impl->pointee);
}

bool RawPointerType::isSubtypeOf(const Type& other) const {
  if (hasBasicSubtypeRelation(other)) { return true; }

  if (other.getKind() != TypeKind::RawPointer) { return false; }

  auto& otherPtr = static_cast<const RawPointerType&>(other);

  // *mut T ⊂ *const T
  if (impl->mutability == Mutability::Mutable && otherPtr.impl->mutability == Mutability::Const) {
    // Pointee types must be equal (invariance)
    if (impl->pointee->equals(*otherPtr.impl->pointee)) { return true; }
  }

  // Same mutability: invariant in pointee
  if (impl->mutability == otherPtr.impl->mutability) {
    return impl->pointee->equals(*otherPtr.impl->pointee);
  }

  return false;
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
