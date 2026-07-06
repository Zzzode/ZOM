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

#include "zomlang/compiler/type/type-var.h"

namespace zomlang {
namespace compiler {
namespace type {

struct TypeVar::Impl {
  zc::StringPtr name;
  uint64_t id;
  zc::Vector<zc::Own<Type>> upperBounds;
  zc::Vector<zc::Own<Type>> lowerBounds;

  explicit Impl(zc::StringPtr n) : name(n), id(0) {}
  Impl(zc::StringPtr n, uint64_t i) : name(n), id(i) {}
};

TypeVar::TypeVar(zc::StringPtr name) : Type(TypeKind::TypeVar), impl(zc::heap<Impl>(name)) {}

TypeVar::TypeVar(zc::StringPtr name, uint64_t id)
    : Type(TypeKind::TypeVar), impl(zc::heap<Impl>(name, id)) {}

TypeVar::~TypeVar() noexcept(false) = default;

TypeVar::TypeVar(TypeVar&& other) noexcept = default;

TypeVar& TypeVar::operator=(TypeVar&& other) noexcept = default;

zc::StringPtr TypeVar::getName() const { return impl->name; }

uint64_t TypeVar::getId() const { return impl->id; }

void TypeVar::addUpperBound(zc::Own<Type> bound) { impl->upperBounds.add(zc::mv(bound)); }

void TypeVar::addLowerBound(zc::Own<Type> bound) { impl->lowerBounds.add(zc::mv(bound)); }

size_t TypeVar::getUpperBoundCount() const { return impl->upperBounds.size(); }

const Type& TypeVar::getUpperBound(size_t index) const { return *impl->upperBounds[index]; }

size_t TypeVar::getLowerBoundCount() const { return impl->lowerBounds.size(); }

const Type& TypeVar::getLowerBound(size_t index) const { return *impl->lowerBounds[index]; }

zc::String TypeVar::toString() const {
  if (impl->id != 0) { return zc::str(impl->name, "#", zc::str(impl->id)); }
  return zc::heapString(impl->name);
}

bool TypeVar::equals(const Type& other) const {
  if (this == &other) { return true; }
  if (other.getKind() != TypeKind::TypeVar) { return false; }

  auto& otherVar = static_cast<const TypeVar&>(other);

  // Type variables are equal if they have the same ID
  if (impl->id != 0 && otherVar.impl->id != 0) { return impl->id == otherVar.impl->id; }

  // Fall back to name comparison
  return impl->name == otherVar.impl->name;
}

bool TypeVar::isSubtypeOf(const Type& other) const {
  if (Type::isSubtypeOf(other)) { return true; }

  // A type variable is a subtype of its upper bounds
  for (size_t i = 0; i < impl->upperBounds.size(); ++i) {
    if (impl->upperBounds[i]->equals(other)) { return true; }
    if (impl->upperBounds[i]->isSubtypeOf(other)) { return true; }
  }

  // If other is also a type variable, check if this is a lower bound of other
  if (other.getKind() == TypeKind::TypeVar) {
    auto& otherVar = static_cast<const TypeVar&>(other);
    for (size_t i = 0; i < otherVar.impl->lowerBounds.size(); ++i) {
      if (otherVar.impl->lowerBounds[i]->equals(*this)) { return true; }
    }
  }

  return false;
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
