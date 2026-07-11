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

#include "zomlang/compiler/type/type-interner.h"

#include "zomlang/compiler/type/array-type.h"
#include "zomlang/compiler/type/associated-type.h"
#include "zomlang/compiler/type/error-type.h"
#include "zomlang/compiler/type/existential-type.h"
#include "zomlang/compiler/type/function-type.h"
#include "zomlang/compiler/type/interface-type.h"
#include "zomlang/compiler/type/intersection-type.h"
#include "zomlang/compiler/type/named-type.h"
#include "zomlang/compiler/type/object-type.h"
#include "zomlang/compiler/type/primitive-type.h"
#include "zomlang/compiler/type/raw-pointer-type.h"
#include "zomlang/compiler/type/reference-type.h"
#include "zomlang/compiler/type/tuple-type.h"
#include "zomlang/compiler/type/type-var.h"
#include "zomlang/compiler/type/union-type.h"

namespace zomlang {
namespace compiler {
namespace type {

namespace {

void sortStrings(zc::Vector<zc::String>& keys) {
  for (size_t i = 1; i < keys.size(); ++i) {
    size_t j = i;
    while (j > 0 && keys[j] < keys[j - 1]) {
      auto tmp = zc::mv(keys[j - 1]);
      keys[j - 1] = zc::mv(keys[j]);
      keys[j] = zc::mv(tmp);
      --j;
    }
  }
}

void addUnique(zc::Vector<zc::String>& keys, zc::String key) {
  for (size_t i = 0; i < keys.size(); ++i) {
    if (keys[i] == key) { return; }
  }
  keys.add(zc::mv(key));
}

zc::String joinKeys(zc::StringPtr prefix, zc::StringPtr separator, zc::Vector<zc::String>& keys) {
  zc::String result = zc::str(prefix, "(");
  for (size_t i = 0; i < keys.size(); ++i) {
    if (i > 0) { result = zc::str(result, separator); }
    result = zc::str(result, keys[i]);
  }
  return zc::str(result, ")");
}

zc::String canonicalKey(const Type& type);

void collectUnionKeys(const Type& type, zc::Vector<zc::String>& keys) {
  if (isUnion(type)) {
    auto& unionType = static_cast<const UnionType&>(type);
    for (size_t i = 0; i < unionType.getAlternativeCount(); ++i) {
      collectUnionKeys(unionType.getAlternative(i), keys);
    }
    return;
  }

  if (isNever(type)) { return; }

  addUnique(keys, canonicalKey(type));
}

void collectIntersectionKeys(const Type& type, zc::Vector<zc::String>& keys, bool& hasNever) {
  if (isIntersection(type)) {
    auto& intersectionType = static_cast<const IntersectionType&>(type);
    for (size_t i = 0; i < intersectionType.getConjunctCount(); ++i) {
      collectIntersectionKeys(intersectionType.getConjunct(i), keys, hasNever);
    }
    return;
  }

  if (isNever(type)) {
    hasNever = true;
    return;
  }

  addUnique(keys, canonicalKey(type));
}

zc::String canonicalKey(const Type& type) {
  if (isUnion(type)) {
    zc::Vector<zc::String> keys;
    collectUnionKeys(type, keys);
    if (keys.empty()) { return zc::str("never"); }
    sortStrings(keys);
    if (keys.size() == 1) { return zc::str(keys[0]); }
    return joinKeys("union"_zc, " | "_zc, keys);
  }

  if (isIntersection(type)) {
    zc::Vector<zc::String> keys;
    bool hasNever = false;
    collectIntersectionKeys(type, keys, hasNever);
    if (hasNever) { return zc::str("never"); }
    if (keys.empty()) { return zc::str("any"); }
    sortStrings(keys);
    if (keys.size() == 1) { return zc::str(keys[0]); }
    return joinKeys("intersection"_zc, " & "_zc, keys);
  }

  switch (type.getKind()) {
    case TypeKind::Primitive: {
      auto& primitive = static_cast<const PrimitiveType&>(type);
      return zc::str(primitive.getName());
    }
    case TypeKind::Function: {
      auto& fn = static_cast<const FunctionType&>(type);
      zc::Vector<zc::String> params;
      for (size_t i = 0; i < fn.getParamCount(); ++i) {
        params.add(canonicalKey(fn.getParamType(i)));
      }

      auto result = joinKeys("fn"_zc, ", "_zc, params);
      result = zc::str(result, " -> ", canonicalKey(fn.getReturnType()));
      auto raises = fn.getRaisesType();
      ZC_IF_SOME(raisesType, raises) {
        result = zc::str(result, " raises ", canonicalKey(raisesType));
      }
      if (fn.isVariadic()) { result = zc::str(result, " variadic"); }
      return result;
    }
    case TypeKind::Tuple: {
      auto& tuple = static_cast<const TupleType&>(type);
      zc::Vector<zc::String> elements;
      for (size_t i = 0; i < tuple.getElementCount(); ++i) {
        elements.add(canonicalKey(tuple.getElementType(i)));
      }
      return joinKeys("tuple"_zc, ", "_zc, elements);
    }
    case TypeKind::Object: {
      auto& object = static_cast<const ObjectType&>(type);
      zc::Vector<zc::String> members;
      auto entries = object.getMembers();
      for (size_t i = 0; i < entries.size(); ++i) {
        ZC_IF_SOME(memberType, entries[i].type) {
          addUnique(members, zc::str(entries[i].name, ": ", canonicalKey(memberType)));
        }
      }
      sortStrings(members);
      return joinKeys("object"_zc, ", "_zc, members);
    }
    case TypeKind::Array: {
      auto& array = static_cast<const ArrayType&>(type);
      return zc::str("array(", canonicalKey(array.getElementType()), ")");
    }
    case TypeKind::Named: {
      auto& named = static_cast<const NamedType&>(type);
      zc::Vector<zc::String> args;
      for (size_t i = 0; i < named.getTypeArgCount(); ++i) {
        args.add(canonicalKey(named.getTypeArg(i)));
      }
      auto result = zc::str("named(", named.getName());
      if (!args.empty()) { result = zc::str(result, joinKeys("<"_zc, ", "_zc, args), ">"); }
      return zc::str(result, ")");
    }
    case TypeKind::TypeVar: {
      auto& var = static_cast<const TypeVar&>(type);
      return zc::str("var(", var.getName(), "#", var.getId(), ")");
    }
    case TypeKind::Error:
      return zc::str("error");
    case TypeKind::Interface: {
      auto& iface = static_cast<const InterfaceType&>(type);
      return zc::str("interface(", iface.getName(), ")");
    }
    case TypeKind::Reference: {
      auto& ref = static_cast<const ReferenceType&>(type);
      auto mutability = ref.getMutability() == Mutability::Mutable ? "mut"_zc : "const"_zc;
      return zc::str("ref(", mutability, ", ", canonicalKey(ref.getPointeeType()), ")");
    }
    case TypeKind::RawPointer: {
      auto& ptr = static_cast<const RawPointerType&>(type);
      auto mutability = ptr.getMutability() == Mutability::Mutable ? "mut"_zc : "const"_zc;
      return zc::str("rawptr(", mutability, ", ", canonicalKey(ptr.getPointeeType()), ")");
    }
    case TypeKind::Existential: {
      auto& existential = static_cast<const ExistentialType&>(type);
      auto result = zc::str("dyn(", canonicalKey(existential.getInterfaceType()));
      for (size_t i = 0; i < existential.getAssocBindingCount(); ++i) {
        result = zc::str(result, ";", existential.getAssocBindingName(i), "=",
                         canonicalKey(existential.getAssocBindingType(i)));
      }
      for (size_t i = 0; i < existential.getMarkerCount(); ++i) {
        result = zc::str(result, "+", existential.getMarkerName(i));
      }
      return zc::str(result, ")");
    }
    case TypeKind::Associated: {
      auto& associated = static_cast<const AssociatedType&>(type);
      return zc::str("assoc(", canonicalKey(associated.getParentType()), "::", associated.getName(),
                     ")");
    }
    case TypeKind::Union:
    case TypeKind::Intersection:
      ZC_UNREACHABLE;
  }

  ZC_UNREACHABLE;
}

}  // namespace

struct TypeInterner::Impl {
  zc::HashMap<zc::String, TypeId> idsByKey;
  zc::Vector<zc::String> keysById;
};

TypeInterner::TypeInterner() : impl(zc::heap<Impl>()) {}

TypeInterner::~TypeInterner() noexcept(false) = default;

TypeInterner::TypeInterner(TypeInterner&& other) noexcept = default;

TypeInterner& TypeInterner::operator=(TypeInterner&& other) noexcept = default;

TypeId TypeInterner::intern(const Type& type) {
  auto key = canonicalKey(type);
  auto existing = impl->idsByKey.find(key);
  ZC_IF_SOME(id, existing) { return id; }

  TypeId id(static_cast<uint32_t>(impl->keysById.size() + 1));
  impl->idsByKey.insert(zc::str(key), id);
  impl->keysById.add(zc::mv(key));
  return id;
}

TypeId TypeInterner::internUnion(const Type& first, const Type& second) {
  zc::Vector<zc::String> keys;
  collectUnionKeys(first, keys);
  collectUnionKeys(second, keys);
  sortStrings(keys);

  zc::String key;
  if (keys.empty()) {
    key = zc::str("never");
  } else if (keys.size() == 1) {
    key = zc::str(keys[0]);
  } else {
    key = joinKeys("union"_zc, " | "_zc, keys);
  }

  auto existing = impl->idsByKey.find(key);
  ZC_IF_SOME(id, existing) { return id; }

  TypeId id(static_cast<uint32_t>(impl->keysById.size() + 1));
  impl->idsByKey.insert(zc::str(key), id);
  impl->keysById.add(zc::mv(key));
  return id;
}

bool TypeInterner::contains(TypeId id) const {
  return id.isValid() && id.value <= impl->keysById.size();
}

zc::StringPtr TypeInterner::getCanonicalKey(TypeId id) const {
  ZC_IREQUIRE(contains(id), "TypeInterner::getCanonicalKey: TypeId is not interned");
  return impl->keysById[id.value - 1];
}

size_t TypeInterner::size() const { return impl->keysById.size(); }

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
