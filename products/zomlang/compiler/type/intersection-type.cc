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

#include "zomlang/compiler/type/intersection-type.h"

namespace zomlang {
namespace compiler {
namespace type {

struct IntersectionType::Impl {
  zc::Vector<zc::Own<Type>> conjuncts;

  explicit Impl(zc::Vector<zc::Own<Type>> conjs) : conjuncts(zc::mv(conjs)) {}
};

IntersectionType::IntersectionType(zc::Vector<zc::Own<Type>> conjuncts)
    : impl(zc::heap<Impl>(zc::mv(conjuncts))) {}

IntersectionType::~IntersectionType() noexcept(false) = default;

IntersectionType::IntersectionType(IntersectionType&& other) noexcept = default;

IntersectionType& IntersectionType::operator=(IntersectionType&& other) noexcept = default;

size_t IntersectionType::getConjunctCount() const { return impl->conjuncts.size(); }

const Type& IntersectionType::getConjunct(size_t index) const { return *impl->conjuncts[index]; }

zc::String IntersectionType::toString() const {
  zc::String result;

  for (size_t i = 0; i < impl->conjuncts.size(); ++i) {
    if (i > 0) { result = zc::str(result, " & "); }
    result = zc::str(result, impl->conjuncts[i]->toString());
  }

  return result;
}

bool IntersectionType::equals(const Type& other) const {
  if (this == &other) { return true; }
  if (other.getKind() != TypeKind::Intersection) { return false; }

  auto& otherInter = static_cast<const IntersectionType&>(other);

  if (impl->conjuncts.size() != otherInter.impl->conjuncts.size()) { return false; }

  // Set equality
  for (size_t i = 0; i < impl->conjuncts.size(); ++i) {
    bool found = false;
    for (size_t j = 0; j < otherInter.impl->conjuncts.size(); ++j) {
      if (impl->conjuncts[i]->equals(*otherInter.impl->conjuncts[j])) {
        found = true;
        break;
      }
    }
    if (!found) { return false; }
  }

  return true;
}

bool IntersectionType::isSubtypeOf(const Type& other) const {
  if (hasBasicSubtypeRelation(*this, other)) { return true; }

  // A & B ⊂ A and A & B ⊂ B (projection)
  // The intersection is a subtype of each of its conjuncts
  for (size_t i = 0; i < impl->conjuncts.size(); ++i) {
    if (impl->conjuncts[i]->equals(other)) { return true; }
    if (impl->conjuncts[i]->isSubtypeOf(other)) { return true; }
  }

  // C ⊂ A & B iff C ⊂ A and C ⊂ B
  if (other.getKind() == TypeKind::Intersection) {
    auto& otherInter = static_cast<const IntersectionType&>(other);

    for (size_t i = 0; i < otherInter.impl->conjuncts.size(); ++i) {
      // This intersection must be subtype of each conjunct of the other
      bool thisSubtypeOfConjunct = false;
      for (size_t j = 0; j < impl->conjuncts.size(); ++j) {
        if (impl->conjuncts[j]->isSubtypeOf(*otherInter.impl->conjuncts[i])) {
          thisSubtypeOfConjunct = true;
          break;
        }
      }
      if (!thisSubtypeOfConjunct) { return false; }
    }
    return true;
  }

  return false;
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
