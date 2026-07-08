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

#include "zc/core/arena.h"
#include "zc/core/vector.h"

namespace zomlang {
namespace compiler {
namespace type {

struct ExistentialType::Impl {
  zc::Arena markerArena;
  zc::Own<Type> interfaceType;
  zc::Vector<zc::StringPtr> markerNames;

  explicit Impl(zc::Own<Type> iface) : interfaceType(zc::mv(iface)) {}

  Impl(zc::Own<Type> iface, zc::ArrayPtr<const zc::StringPtr> markers)
      : interfaceType(zc::mv(iface)) {
    for (auto marker : markers) { addMarker(marker); }
  }

  void addMarker(zc::StringPtr marker) {
    for (size_t i = 0; i < markerNames.size(); ++i) {
      if (markerNames[i] == marker) { return; }
      if (marker < markerNames[i]) {
        markerNames.add(markerArena.copyString(marker));
        for (size_t j = markerNames.size() - 1; j > i; --j) {
          auto tmp = markerNames[j - 1];
          markerNames[j - 1] = markerNames[j];
          markerNames[j] = tmp;
        }
        return;
      }
    }
    markerNames.add(markerArena.copyString(marker));
  }
};

ExistentialType::ExistentialType(zc::Own<Type> interfaceType)
    : impl(zc::heap<Impl>(zc::mv(interfaceType))) {}

ExistentialType::ExistentialType(zc::Own<Type> interfaceType,
                                 zc::ArrayPtr<const zc::StringPtr> markerNames)
    : impl(zc::heap<Impl>(zc::mv(interfaceType), markerNames)) {}

ExistentialType::~ExistentialType() noexcept(false) = default;

ExistentialType::ExistentialType(ExistentialType&& other) noexcept = default;

ExistentialType& ExistentialType::operator=(ExistentialType&& other) noexcept = default;

const Type& ExistentialType::getInterfaceType() const { return *impl->interfaceType; }

size_t ExistentialType::getMarkerCount() const { return impl->markerNames.size(); }

zc::StringPtr ExistentialType::getMarkerName(size_t index) const {
  return impl->markerNames[index];
}

zc::String ExistentialType::toString() const {
  auto result = zc::str("dyn ", impl->interfaceType->toString());
  for (size_t i = 0; i < impl->markerNames.size(); ++i) {
    result = zc::str(result, " + ", impl->markerNames[i]);
  }
  return result;
}

bool ExistentialType::equals(const Type& other) const {
  if (this == &other) { return true; }
  if (other.getKind() != TypeKind::Existential) { return false; }

  auto& otherEx = static_cast<const ExistentialType&>(other);
  if (!impl->interfaceType->equals(*otherEx.impl->interfaceType)) { return false; }
  if (impl->markerNames.size() != otherEx.impl->markerNames.size()) { return false; }
  for (size_t i = 0; i < impl->markerNames.size(); ++i) {
    if (impl->markerNames[i] != otherEx.impl->markerNames[i]) { return false; }
  }
  return true;
}

bool ExistentialType::isSubtypeOf(const Type& other) const {
  if (hasBasicSubtypeRelation(*this, other)) { return true; }

  // dyn Interface is subtype of Interface (it satisfies the interface)
  if (impl->interfaceType->equals(other)) { return true; }
  if (impl->interfaceType->isSubtypeOf(other)) { return true; }

  // dyn A ⊂ dyn B if A ⊂ B
  if (other.getKind() == TypeKind::Existential) {
    auto& otherEx = static_cast<const ExistentialType&>(other);
    if (!impl->interfaceType->isSubtypeOf(*otherEx.impl->interfaceType)) { return false; }
    for (size_t i = 0; i < otherEx.impl->markerNames.size(); ++i) {
      bool found = false;
      for (size_t j = 0; j < impl->markerNames.size(); ++j) {
        if (impl->markerNames[j] == otherEx.impl->markerNames[i]) {
          found = true;
          break;
        }
      }
      if (!found) { return false; }
    }
    return true;
  }

  return false;
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
