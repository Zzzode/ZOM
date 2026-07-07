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

#include "zomlang/compiler/type/array-type.h"

namespace zomlang {
namespace compiler {
namespace type {

struct ArrayType::Impl {
  zc::Own<Type> elementType;

  explicit Impl(zc::Own<Type> elem) : elementType(zc::mv(elem)) {}
};

ArrayType::ArrayType(zc::Own<Type> elementType) : impl(zc::heap<Impl>(zc::mv(elementType))) {}

ArrayType::~ArrayType() noexcept(false) = default;

ArrayType::ArrayType(ArrayType&& other) noexcept = default;

ArrayType& ArrayType::operator=(ArrayType&& other) noexcept = default;

const Type& ArrayType::getElementType() const { return *impl->elementType; }

zc::String ArrayType::toString() const { return zc::str(impl->elementType->toString(), "[]"); }

bool ArrayType::equals(const Type& other) const {
  if (this == &other) { return true; }
  if (other.getKind() != TypeKind::Array) { return false; }

  auto& otherArr = static_cast<const ArrayType&>(other);
  return impl->elementType->equals(*otherArr.impl->elementType);
}

bool ArrayType::isSubtypeOf(const Type& other) const {
  if (hasBasicSubtypeRelation(other)) { return true; }

  if (other.getKind() != TypeKind::Array) { return false; }

  auto& otherArr = static_cast<const ArrayType&>(other);

  // Covariance: T[] ⊂ U[] if T ⊂ U
  return impl->elementType->isSubtypeOf(*otherArr.impl->elementType);
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
