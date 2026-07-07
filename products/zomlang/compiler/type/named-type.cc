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

#include "zomlang/compiler/type/named-type.h"

#include "zc/core/arena.h"
#include "zomlang/compiler/symbol/type-symbol.h"
#include "zomlang/compiler/type/primitive-type.h"

namespace zomlang {
namespace compiler {
namespace type {

struct NamedType::Impl {
  zc::Arena nameArena;
  zc::StringPtr name;
  zc::Maybe<const symbol::TypeSymbol&> symbol;
  zc::Vector<zc::Own<Type>> typeArgs;

  explicit Impl(zc::StringPtr n) : name(nameArena.copyString(n)), symbol(zc::none) {}
  Impl(zc::StringPtr n, const symbol::TypeSymbol& s) : name(nameArena.copyString(n)), symbol(s) {}
};

NamedType::NamedType(zc::StringPtr name) : impl(zc::heap<Impl>(name)) {}

NamedType::NamedType(zc::StringPtr name, const symbol::TypeSymbol& symbol)
    : impl(zc::heap<Impl>(name, symbol)) {}

NamedType::~NamedType() noexcept(false) = default;

NamedType::NamedType(NamedType&& other) noexcept = default;

NamedType& NamedType::operator=(NamedType&& other) noexcept = default;

zc::StringPtr NamedType::getName() const { return impl->name; }

zc::Maybe<const symbol::TypeSymbol&> NamedType::getSymbol() const { return impl->symbol; }

void NamedType::setSymbol(const symbol::TypeSymbol& symbol) { impl->symbol = symbol; }

size_t NamedType::getTypeArgCount() const { return impl->typeArgs.size(); }

const Type& NamedType::getTypeArg(size_t index) const { return *impl->typeArgs[index]; }

void NamedType::addTypeArg(zc::Own<Type> arg) { impl->typeArgs.add(zc::mv(arg)); }

zc::ArrayPtr<const zc::Own<Type>> NamedType::getTypeArgs() const { return impl->typeArgs.asPtr(); }

zc::String NamedType::toString() const {
  zc::String result = zc::heapString(impl->name);

  if (impl->typeArgs.size() > 0) {
    result = zc::str(result, "<");
    for (size_t i = 0; i < impl->typeArgs.size(); ++i) {
      if (i > 0) { result = zc::str(result, ", "); }
      result = zc::str(result, impl->typeArgs[i]->toString());
    }
    result = zc::str(result, ">");
  }

  return result;
}

bool NamedType::equals(const Type& other) const {
  if (this == &other) { return true; }
  if (other.getKind() != TypeKind::Named) { return false; }

  auto& otherNamed = static_cast<const NamedType&>(other);

  // Nominal equality: same name
  if (impl->name != otherNamed.impl->name) { return false; }

  // Type arguments must match
  if (impl->typeArgs.size() != otherNamed.impl->typeArgs.size()) { return false; }

  for (size_t i = 0; i < impl->typeArgs.size(); ++i) {
    if (!impl->typeArgs[i]->equals(*otherNamed.impl->typeArgs[i])) { return false; }
  }

  return true;
}

bool NamedType::isSubtypeOf(const Type& other) const {
  if (hasBasicSubtypeRelation(other)) { return true; }

  if (other.getKind() == TypeKind::Named) {
    auto& otherNamed = static_cast<const NamedType&>(other);

    // If both have resolved symbols, use the symbol hierarchy
    ZC_IF_SOME(thisSym, impl->symbol) {
      ZC_IF_SOME(otherSym, otherNamed.impl->symbol) {
        if (thisSym.isSubtypeOf(otherSym)) { return true; }
      }
    }

    // Same name implies same type (nominal)
    if (impl->name == otherNamed.impl->name) {
      // Check type arguments (invariance for generics by default)
      if (impl->typeArgs.size() != otherNamed.impl->typeArgs.size()) { return false; }
      for (size_t i = 0; i < impl->typeArgs.size(); ++i) {
        if (!impl->typeArgs[i]->equals(*otherNamed.impl->typeArgs[i])) { return false; }
      }
      return true;
    }

    return false;
  }

  // Named type can be subtype of interface type if it implements the interface
  if (other.getKind() == TypeKind::Interface) {
    // This would require checking if the named type's class implements the interface
    // For now, rely on symbol hierarchy if available
    ZC_IF_SOME(thisSym, impl->symbol) {
      // If this symbol implements the interface, it would be checked via
      // the symbol's isSubtypeOf with the interface's symbol.
      // Without a resolved interface symbol, we can't determine this.
      (void)thisSym;
    }
  }

  return false;
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
