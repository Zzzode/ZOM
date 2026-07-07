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

#include "zomlang/compiler/type/union-type.h"

#include "zomlang/compiler/type/primitive-type.h"

namespace zomlang {
namespace compiler {
namespace type {

struct UnionType::Impl {
  zc::Vector<zc::Own<Type>> alternatives;

  explicit Impl(zc::Vector<zc::Own<Type>> alts) : alternatives(zc::mv(alts)) {}
};

UnionType::UnionType(zc::Vector<zc::Own<Type>> alternatives)
    : impl(zc::heap<Impl>(zc::mv(alternatives))) {}

UnionType::~UnionType() noexcept(false) = default;

UnionType::UnionType(UnionType&& other) noexcept = default;

UnionType& UnionType::operator=(UnionType&& other) noexcept = default;

size_t UnionType::getAlternativeCount() const { return impl->alternatives.size(); }

const Type& UnionType::getAlternative(size_t index) const { return *impl->alternatives[index]; }

bool UnionType::contains(const Type& type) const {
  for (size_t i = 0; i < impl->alternatives.size(); ++i) {
    if (impl->alternatives[i]->equals(type)) { return true; }
  }
  return false;
}

bool UnionType::isNullable() const {
  for (size_t i = 0; i < impl->alternatives.size(); ++i) {
    if (isNull(*impl->alternatives[i])) { return true; }
  }
  return false;
}

zc::String UnionType::toString() const {
  zc::String result;

  for (size_t i = 0; i < impl->alternatives.size(); ++i) {
    if (i > 0) { result = zc::str(result, " | "); }
    result = zc::str(result, impl->alternatives[i]->toString());
  }

  return result;
}

bool UnionType::equals(const Type& other) const {
  if (this == &other) { return true; }
  if (other.getKind() != TypeKind::Union) { return false; }

  auto& otherUnion = static_cast<const UnionType&>(other);

  if (impl->alternatives.size() != otherUnion.impl->alternatives.size()) { return false; }

  // Set equality: every alternative in this must be in other and vice versa
  for (size_t i = 0; i < impl->alternatives.size(); ++i) {
    if (!otherUnion.contains(*impl->alternatives[i])) { return false; }
  }

  return true;
}

bool UnionType::isSubtypeOf(const Type& other) const {
  if (hasBasicSubtypeRelation(*this, other)) { return true; }

  // T ⊂ T|E: each alternative is subtype of the union
  // For union subtyping: A|B ⊂ C iff A ⊂ C and B ⊂ C
  if (other.getKind() == TypeKind::Union) {
    auto& otherUnion = static_cast<const UnionType&>(other);

    // Each alternative in this must be contained in or subtype of some
    // alternative in other
    for (size_t i = 0; i < impl->alternatives.size(); ++i) {
      bool found = false;
      for (size_t j = 0; j < otherUnion.impl->alternatives.size(); ++j) {
        if (impl->alternatives[i]->isSubtypeOf(*otherUnion.impl->alternatives[j])) {
          found = true;
          break;
        }
      }
      if (!found) { return false; }
    }
    return true;
  }

  // A|B ⊂ C iff A ⊂ C and B ⊂ C
  for (size_t i = 0; i < impl->alternatives.size(); ++i) {
    if (!impl->alternatives[i]->isSubtypeOf(other)) { return false; }
  }

  return true;
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
