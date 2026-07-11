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

#include "zomlang/compiler/type/interface-type.h"

#include "zc/core/array.h"

namespace zomlang {
namespace compiler {
namespace type {

struct InterfaceType::Impl {
  zc::StringPtr name;
  zc::HashMap<zc::StringPtr, zc::Own<Type>> methods;
  zc::Vector<zc::StringPtr> methodOrder;
  zc::Vector<zc::Own<Type>> parentInterfaces;

  explicit Impl(zc::StringPtr n) : name(n) {}
};

InterfaceType::InterfaceType(zc::StringPtr name) : impl(zc::heap<Impl>(name)) {}

InterfaceType::~InterfaceType() noexcept(false) = default;

InterfaceType::InterfaceType(InterfaceType&& other) noexcept = default;

InterfaceType& InterfaceType::operator=(InterfaceType&& other) noexcept = default;

zc::StringPtr InterfaceType::getName() const { return impl->name; }

void InterfaceType::addMethod(zc::StringPtr name, zc::Own<Type> methodType) {
  impl->methodOrder.add(name);
  impl->methods.insert(name, zc::mv(methodType));
}

zc::Maybe<const Type&> InterfaceType::getMethod(zc::StringPtr name) const {
  auto found = impl->methods.find(name);
  ZC_IF_SOME(type, found) { return *type; }
  return zc::none;
}

size_t InterfaceType::getMethodCount() const { return impl->methods.size(); }

bool InterfaceType::hasMethod(zc::StringPtr name) const {
  auto found = impl->methods.find(name);
  return found != zc::none;
}

void InterfaceType::addParentInterface(zc::Own<Type> parent) {
  impl->parentInterfaces.add(zc::mv(parent));
}

size_t InterfaceType::getParentInterfaceCount() const { return impl->parentInterfaces.size(); }

const Type& InterfaceType::getParentInterface(size_t index) const {
  return *impl->parentInterfaces[index];
}

zc::String InterfaceType::toString() const {
  zc::String result = zc::str("interface ", impl->name);

  if (impl->parentInterfaces.size() > 0) {
    result = zc::str(result, " : ");
    for (size_t i = 0; i < impl->parentInterfaces.size(); ++i) {
      if (i > 0) { result = zc::str(result, ", "); }
      result = zc::str(result, impl->parentInterfaces[i]->toString());
    }
  }

  result = zc::str(result, " { ");

  for (size_t i = 0; i < impl->methodOrder.size(); ++i) {
    auto name = impl->methodOrder[i];
    auto found = impl->methods.find(name);
    ZC_IF_SOME(type, found) {
      if (i > 0) { result = zc::str(result, "; "); }
      result = zc::str(result, name, ": ", type->toString());
    }
  }

  result = zc::str(result, " }");
  return result;
}

bool InterfaceType::equals(const Type& other) const {
  if (this == &other) { return true; }
  if (other.getKind() != TypeKind::Interface) { return false; }

  auto& otherIface = static_cast<const InterfaceType&>(other);

  // Nominal equality for interfaces
  if (impl->name != otherIface.impl->name) { return false; }

  // Method count must match
  if (impl->methods.size() != otherIface.impl->methods.size()) { return false; }

  return true;
}

bool InterfaceType::isSubtypeOf(const Type& other) const {
  if (hasBasicSubtypeRelation(*this, other)) { return true; }

  if (other.getKind() == TypeKind::Interface) {
    auto& otherIface = static_cast<const InterfaceType&>(other);

    // Same interface
    if (impl->name == otherIface.impl->name) { return true; }

    // Check parent interfaces (transitive)
    for (size_t i = 0; i < impl->parentInterfaces.size(); ++i) {
      if (impl->parentInterfaces[i]->isSubtypeOf(other)) { return true; }
    }

    // Structural subtyping: this must have all methods of other
    for (size_t i = 0; i < otherIface.impl->methodOrder.size(); ++i) {
      auto name = otherIface.impl->methodOrder[i];
      auto thisMethod = impl->methods.find(name);
      auto otherMethod = otherIface.impl->methods.find(name);

      if (thisMethod == zc::none) { return false; }

      ZC_IF_SOME(thisType, thisMethod) {
        ZC_IF_SOME(otherType, otherMethod) {
          if (!thisType->isSubtypeOf(*otherType)) { return false; }
        }
      }
    }

    return true;
  }

  return false;
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
