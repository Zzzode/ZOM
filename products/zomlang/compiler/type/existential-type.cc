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

#include "zomlang/compiler/type/existential-type.h"

namespace zomlang {
namespace compiler {
namespace type {

struct ExistentialType::Impl {
  zc::Own<Type> interfaceType;

  explicit Impl(zc::Own<Type> iface) : interfaceType(zc::mv(iface)) {}
};

ExistentialType::ExistentialType(zc::Own<Type> interfaceType)
    : impl(zc::heap<Impl>(zc::mv(interfaceType))) {}

ExistentialType::~ExistentialType() noexcept(false) = default;

ExistentialType::ExistentialType(ExistentialType&& other) noexcept = default;

ExistentialType& ExistentialType::operator=(ExistentialType&& other) noexcept = default;

const Type& ExistentialType::getInterfaceType() const { return *impl->interfaceType; }

zc::String ExistentialType::toString() const {
  return zc::str("dyn ", impl->interfaceType->toString());
}

bool ExistentialType::equals(const Type& other) const {
  if (this == &other) { return true; }
  if (other.getKind() != TypeKind::Existential) { return false; }

  auto& otherEx = static_cast<const ExistentialType&>(other);
  return impl->interfaceType->equals(*otherEx.impl->interfaceType);
}

bool ExistentialType::isSubtypeOf(const Type& other) const {
  if (hasBasicSubtypeRelation(*this, other)) { return true; }

  // dyn Interface is subtype of Interface (it satisfies the interface)
  if (impl->interfaceType->equals(other)) { return true; }
  if (impl->interfaceType->isSubtypeOf(other)) { return true; }

  // dyn A ⊂ dyn B if A ⊂ B
  if (other.getKind() == TypeKind::Existential) {
    auto& otherEx = static_cast<const ExistentialType&>(other);
    return impl->interfaceType->isSubtypeOf(*otherEx.impl->interfaceType);
  }

  return false;
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
