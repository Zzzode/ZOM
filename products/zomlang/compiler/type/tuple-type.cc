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

#include "zomlang/compiler/type/tuple-type.h"

#include "zomlang/compiler/type/primitive-type.h"

namespace zomlang {
namespace compiler {
namespace type {

struct TupleType::Impl {
  zc::Vector<zc::Own<Type>> elements;

  explicit Impl(zc::Vector<zc::Own<Type>> elems) : elements(zc::mv(elems)) {}
};

TupleType::TupleType(zc::Vector<zc::Own<Type>> elements) : impl(zc::heap<Impl>(zc::mv(elements))) {}

TupleType::~TupleType() noexcept(false) = default;

TupleType::TupleType(TupleType&& other) noexcept = default;

TupleType& TupleType::operator=(TupleType&& other) noexcept = default;

size_t TupleType::getElementCount() const { return impl->elements.size(); }

const Type& TupleType::getElementType(size_t index) const { return *impl->elements[index]; }

bool TupleType::isEmpty() const { return impl->elements.size() == 0; }

zc::String TupleType::toString() const {
  zc::String result = zc::heapString("(");

  for (size_t i = 0; i < impl->elements.size(); ++i) {
    if (i > 0) { result = zc::str(result, ", "); }
    result = zc::str(result, impl->elements[i]->toString());
  }

  // Single-element tuple needs trailing comma
  if (impl->elements.size() == 1) { result = zc::str(result, ","); }

  result = zc::str(result, ")");
  return result;
}

bool TupleType::equals(const Type& other) const {
  if (this == &other) { return true; }
  if (other.getKind() != TypeKind::Tuple) { return false; }

  auto& otherTuple = static_cast<const TupleType&>(other);

  if (impl->elements.size() != otherTuple.impl->elements.size()) { return false; }

  for (size_t i = 0; i < impl->elements.size(); ++i) {
    if (!impl->elements[i]->equals(*otherTuple.impl->elements[i])) { return false; }
  }

  return true;
}

bool TupleType::isSubtypeOf(const Type& other) const {
  if (hasBasicSubtypeRelation(other)) { return true; }

  // Empty tuple is equivalent to unit
  if (isEmpty() && other.isUnit()) { return true; }

  if (other.getKind() != TypeKind::Tuple) { return false; }

  auto& otherTuple = static_cast<const TupleType&>(other);

  // Tuple subtyping is covariant and requires same arity
  if (impl->elements.size() != otherTuple.impl->elements.size()) { return false; }

  for (size_t i = 0; i < impl->elements.size(); ++i) {
    if (!impl->elements[i]->isSubtypeOf(*otherTuple.impl->elements[i])) { return false; }
  }

  return true;
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
