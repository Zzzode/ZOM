// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/type/type-algebra.h"

#include "zc/core/common.h"
#include "zc/core/vector.h"
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

zc::Own<Type> cloneType(const Type& type) {
  switch (type.getKind()) {
    case TypeKind::Error:
      return zc::heap<ErrorType>();

    case TypeKind::Primitive: {
      auto& primitive = static_cast<const PrimitiveType&>(type);
      return zc::heap<PrimitiveType>(primitive.getPrimitiveKind());
    }

    case TypeKind::Named: {
      auto& named = static_cast<const NamedType&>(type);
      auto result = zc::heap<NamedType>(named.getName());
      ZC_IF_SOME(symbol, named.getSymbol()) { result->setSymbol(symbol); }
      for (size_t i = 0; i < named.getTypeArgCount(); ++i) {
        result->addTypeArg(cloneType(named.getTypeArg(i)));
      }
      return zc::mv(result);
    }

    case TypeKind::Object: {
      auto& object = static_cast<const ObjectType&>(type);
      auto result = zc::heap<ObjectType>();
      auto members = object.getMembers();
      for (size_t i = 0; i < members.size(); ++i) {
        ZC_IF_SOME(memberType, members[i].type) {
          result->addMember(members[i].name, cloneType(memberType));
        }
      }
      return zc::mv(result);
    }

    case TypeKind::Function: {
      auto& function = static_cast<const FunctionType&>(type);
      zc::Vector<zc::Own<Type>> params;
      for (size_t i = 0; i < function.getParamCount(); ++i) {
        params.add(cloneType(function.getParamType(i)));
      }
      auto result = zc::heap<FunctionType>(zc::mv(params), cloneType(function.getReturnType()));
      result->setVariadic(function.isVariadic());
      for (size_t i = 0; i < function.getGenericParamCount(); ++i) {
        auto& generic = function.getGenericParam(i);
        auto clone = zc::heap<GenericParam>(generic.name);
        for (size_t j = 0; j < generic.upperBounds.size(); ++j) {
          clone->upperBounds.add(cloneType(*generic.upperBounds[j]));
        }
        for (size_t j = 0; j < generic.lowerBounds.size(); ++j) {
          clone->lowerBounds.add(cloneType(*generic.lowerBounds[j]));
        }
        result->addGenericParam(zc::mv(clone));
      }
      auto raises = function.getRaisesType();
      ZC_IF_SOME(raisesType, raises) { result->setRaisesType(cloneType(raisesType)); }
      return zc::mv(result);
    }

    case TypeKind::Tuple: {
      auto& tuple = static_cast<const TupleType&>(type);
      zc::Vector<zc::Own<Type>> elements;
      for (size_t i = 0; i < tuple.getElementCount(); ++i) {
        elements.add(cloneType(tuple.getElementType(i)));
      }
      return zc::heap<TupleType>(zc::mv(elements));
    }

    case TypeKind::Array: {
      auto& array = static_cast<const ArrayType&>(type);
      return zc::heap<ArrayType>(cloneType(array.getElementType()));
    }

    case TypeKind::TypeVar: {
      auto& typeVar = static_cast<const TypeVar&>(type);
      auto result = zc::heap<TypeVar>(typeVar.getName(), typeVar.getId());
      for (size_t i = 0; i < typeVar.getUpperBoundCount(); ++i) {
        result->addUpperBound(cloneType(typeVar.getUpperBound(i)));
      }
      for (size_t i = 0; i < typeVar.getLowerBoundCount(); ++i) {
        result->addLowerBound(cloneType(typeVar.getLowerBound(i)));
      }
      return zc::mv(result);
    }

    case TypeKind::Interface: {
      auto& iface = static_cast<const InterfaceType&>(type);
      auto result = zc::heap<InterfaceType>(iface.getName());
      for (size_t i = 0; i < iface.getParentInterfaceCount(); ++i) {
        result->addParentInterface(cloneType(iface.getParentInterface(i)));
      }
      return zc::mv(result);
    }

    case TypeKind::Union: {
      auto& unionType = static_cast<const UnionType&>(type);
      zc::Vector<zc::Own<Type>> alternatives;
      for (size_t i = 0; i < unionType.getAlternativeCount(); ++i) {
        alternatives.add(cloneType(unionType.getAlternative(i)));
      }
      return zc::heap<UnionType>(zc::mv(alternatives));
    }

    case TypeKind::Intersection: {
      auto& intersection = static_cast<const IntersectionType&>(type);
      zc::Vector<zc::Own<Type>> conjuncts;
      for (size_t i = 0; i < intersection.getConjunctCount(); ++i) {
        conjuncts.add(cloneType(intersection.getConjunct(i)));
      }
      return zc::heap<IntersectionType>(zc::mv(conjuncts));
    }

    case TypeKind::Reference: {
      auto& reference = static_cast<const ReferenceType&>(type);
      return zc::heap<ReferenceType>(cloneType(reference.getPointeeType()),
                                     reference.getMutability());
    }

    case TypeKind::RawPointer: {
      auto& pointer = static_cast<const RawPointerType&>(type);
      return zc::heap<RawPointerType>(cloneType(pointer.getPointeeType()), pointer.getMutability());
    }

    case TypeKind::Existential: {
      auto& existential = static_cast<const ExistentialType&>(type);
      zc::Vector<zc::StringPtr> markers;
      for (size_t i = 0; i < existential.getMarkerCount(); ++i) {
        markers.add(existential.getMarkerName(i));
      }
      return zc::heap<ExistentialType>(cloneType(existential.getInterfaceType()), markers.asPtr());
    }

    case TypeKind::Associated: {
      auto& associated = static_cast<const AssociatedType&>(type);
      return zc::heap<AssociatedType>(cloneType(associated.getParentType()), associated.getName());
    }
  }

  ZC_UNREACHABLE;
}

zc::Maybe<const Type&> findTypeVarByName(const Type& type, zc::StringPtr name) {
  if (isTypeVar(type)) {
    auto& var = static_cast<const TypeVar&>(type);
    if (var.getName() == name) { return var; }
    return zc::none;
  }

  if (isFunction(type)) {
    auto& function = static_cast<const FunctionType&>(type);
    for (size_t i = 0; i < function.getParamCount(); ++i) {
      auto found = findTypeVarByName(function.getParamType(i), name);
      if (found != zc::none) return found;
    }
    auto ret = findTypeVarByName(function.getReturnType(), name);
    if (ret != zc::none) return ret;
    ZC_IF_SOME(raises, function.getRaisesType()) { return findTypeVarByName(raises, name); }
    return zc::none;
  }

  if (isTuple(type)) {
    auto& tuple = static_cast<const TupleType&>(type);
    for (size_t i = 0; i < tuple.getElementCount(); ++i) {
      auto found = findTypeVarByName(tuple.getElementType(i), name);
      if (found != zc::none) return found;
    }
    return zc::none;
  }

  if (isArray(type)) {
    auto& array = static_cast<const ArrayType&>(type);
    return findTypeVarByName(array.getElementType(), name);
  }

  if (isReference(type)) {
    auto& reference = static_cast<const ReferenceType&>(type);
    return findTypeVarByName(reference.getPointeeType(), name);
  }

  if (isRawPointer(type)) {
    auto& pointer = static_cast<const RawPointerType&>(type);
    return findTypeVarByName(pointer.getPointeeType(), name);
  }

  if (isUnion(type)) {
    auto& unionType = static_cast<const UnionType&>(type);
    for (size_t i = 0; i < unionType.getAlternativeCount(); ++i) {
      auto found = findTypeVarByName(unionType.getAlternative(i), name);
      if (found != zc::none) return found;
    }
    return zc::none;
  }

  if (isObject(type)) {
    auto& object = static_cast<const ObjectType&>(type);
    auto members = object.getMembers();
    for (size_t i = 0; i < members.size(); ++i) {
      ZC_IF_SOME(memberType, members[i].type) {
        auto found = findTypeVarByName(memberType, name);
        if (found != zc::none) return found;
      }
    }
    return zc::none;
  }

  if (isInterface(type)) {
    auto& iface = static_cast<const InterfaceType&>(type);
    for (size_t i = 0; i < iface.getParentInterfaceCount(); ++i) {
      auto found = findTypeVarByName(iface.getParentInterface(i), name);
      if (found != zc::none) return found;
    }
    return zc::none;
  }

  if (isIntersection(type)) {
    auto& intersection = static_cast<const IntersectionType&>(type);
    for (size_t i = 0; i < intersection.getConjunctCount(); ++i) {
      auto found = findTypeVarByName(intersection.getConjunct(i), name);
      if (found != zc::none) return found;
    }
    return zc::none;
  }

  if (isExistential(type)) {
    auto& existential = static_cast<const ExistentialType&>(type);
    return findTypeVarByName(existential.getInterfaceType(), name);
  }

  if (isAssociated(type)) {
    auto& associated = static_cast<const AssociatedType&>(type);
    return findTypeVarByName(associated.getParentType(), name);
  }

  return zc::none;
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
