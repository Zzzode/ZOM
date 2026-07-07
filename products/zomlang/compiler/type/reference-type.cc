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

#include "zomlang/compiler/type/reference-type.h"

#include "zomlang/compiler/type/primitive-type.h"

namespace zomlang {
namespace compiler {
namespace type {

struct ReferenceType::Impl {
  zc::Own<Type> pointee;
  Mutability mutability;

  Impl(zc::Own<Type> p, Mutability m) : pointee(zc::mv(p)), mutability(m) {}
};

ReferenceType::ReferenceType(zc::Own<Type> pointee, Mutability mutability)
    : impl(zc::heap<Impl>(zc::mv(pointee), mutability)) {}

ReferenceType::~ReferenceType() noexcept(false) = default;

ReferenceType::ReferenceType(ReferenceType&& other) noexcept = default;

ReferenceType& ReferenceType::operator=(ReferenceType&& other) noexcept = default;

const Type& ReferenceType::getPointeeType() const { return *impl->pointee; }

Mutability ReferenceType::getMutability() const { return impl->mutability; }

bool ReferenceType::isMutable() const { return impl->mutability == Mutability::Mutable; }

zc::String ReferenceType::toString() const {
  if (impl->mutability == Mutability::Mutable) {
    return zc::str("&mut ", impl->pointee->toString());
  }
  return zc::str("&", impl->pointee->toString());
}

bool ReferenceType::equals(const Type& other) const {
  if (this == &other) { return true; }
  if (other.getKind() != TypeKind::Reference) { return false; }

  auto& otherRef = static_cast<const ReferenceType&>(other);

  if (impl->mutability != otherRef.impl->mutability) { return false; }

  return impl->pointee->equals(*otherRef.impl->pointee);
}

bool ReferenceType::isSubtypeOf(const Type& other) const {
  if (hasBasicSubtypeRelation(*this, other)) { return true; }

  if (isNull(other)) { return false; }

  if (other.getKind() != TypeKind::Reference) { return false; }

  auto& otherRef = static_cast<const ReferenceType&>(other);

  // &mut T <: &T (mutable reference is subtype of immutable reference).
  // Immutable references are not subtypes of mutable references.
  if (impl->mutability == Mutability::Mutable && otherRef.impl->mutability == Mutability::Const) {
    // &mut T ⊂ &T if T ⊂ T (which it always does)
    // But we need pointee types to match (invariance for soundness)
    if (impl->pointee->equals(*otherRef.impl->pointee)) { return true; }
  }

  // Same mutability: invariant in pointee type
  if (impl->mutability == otherRef.impl->mutability) {
    return impl->pointee->equals(*otherRef.impl->pointee);
  }

  return false;
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
