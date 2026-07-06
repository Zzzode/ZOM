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

#include "zomlang/compiler/type/associated-type.h"

namespace zomlang {
namespace compiler {
namespace type {

struct AssociatedType::Impl {
  zc::Own<Type> parent;
  zc::StringPtr name;

  Impl(zc::Own<Type> p, zc::StringPtr n) : parent(zc::mv(p)), name(n) {}
};

AssociatedType::AssociatedType(zc::Own<Type> parent, zc::StringPtr name)
    : Type(TypeKind::Associated), impl(zc::heap<Impl>(zc::mv(parent), name)) {}

AssociatedType::~AssociatedType() noexcept(false) = default;

AssociatedType::AssociatedType(AssociatedType&& other) noexcept = default;

AssociatedType& AssociatedType::operator=(AssociatedType&& other) noexcept = default;

const Type& AssociatedType::getParentType() const { return *impl->parent; }

zc::StringPtr AssociatedType::getName() const { return impl->name; }

zc::String AssociatedType::toString() const {
  return zc::str(impl->parent->toString(), "::", impl->name);
}

bool AssociatedType::equals(const Type& other) const {
  if (this == &other) { return true; }
  if (other.getKind() != TypeKind::Associated) { return false; }

  auto& otherAssoc = static_cast<const AssociatedType&>(other);

  if (impl->name != otherAssoc.impl->name) { return false; }

  return impl->parent->equals(*otherAssoc.impl->parent);
}

bool AssociatedType::isSubtypeOf(const Type& other) const {
  if (Type::isSubtypeOf(other)) { return true; }

  // Associated types are nominally compared
  if (other.getKind() == TypeKind::Associated) {
    auto& otherAssoc = static_cast<const AssociatedType&>(other);

    // Same parent and same name means same type
    if (impl->name == otherAssoc.impl->name && impl->parent->equals(*otherAssoc.impl->parent)) {
      return true;
    }

    // If parent is subtype, associated type might be too (depends on variance)
    if (impl->parent->isSubtypeOf(*otherAssoc.impl->parent) &&
        impl->name == otherAssoc.impl->name) {
      // Conservative: associated types are invariant by default
      return impl->parent->equals(*otherAssoc.impl->parent);
    }
  }

  return false;
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
