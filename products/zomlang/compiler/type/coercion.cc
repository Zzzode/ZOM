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

#include "zomlang/compiler/type/coercion.h"

#include "zomlang/compiler/type/raw-pointer-type.h"
#include "zomlang/compiler/type/reference-type.h"
#include "zomlang/compiler/type/union-type.h"

namespace zomlang {
namespace compiler {
namespace type {

namespace {

bool unionContainsCoercionTarget(const UnionType& target, const Type& source) {
  for (size_t i = 0; i < target.getAlternativeCount(); ++i) {
    if (source.equals(target.getAlternative(i))) { return true; }
  }
  return false;
}

}  // namespace

bool CoercionResolver::canCoerce(const Type& source, const Type& target) const {
  return check(source, target).success;
}

CoercionResult CoercionResolver::check(const Type& source, const Type& target) const {
  if (&source == &target || source.equals(target)) {
    return CoercionResult{true, CoercionKind::Identity};
  }

  if (isNever(source)) { return CoercionResult{true, CoercionKind::NeverToAny}; }

  if (isAny(target)) { return CoercionResult{true, CoercionKind::ToAny}; }

  if (isReference(source) && isReference(target)) {
    auto& sourceRef = static_cast<const ReferenceType&>(source);
    auto& targetRef = static_cast<const ReferenceType&>(target);
    if (sourceRef.getMutability() == Mutability::Mutable &&
        targetRef.getMutability() == Mutability::Const &&
        sourceRef.getPointeeType().equals(targetRef.getPointeeType())) {
      return CoercionResult{true, CoercionKind::MutRefToSharedRef};
    }
  }

  if (isRawPointer(source) && isRawPointer(target)) {
    auto& sourcePtr = static_cast<const RawPointerType&>(source);
    auto& targetPtr = static_cast<const RawPointerType&>(target);
    if (sourcePtr.getMutability() == Mutability::Mutable &&
        targetPtr.getMutability() == Mutability::Const &&
        sourcePtr.getPointeeType().equals(targetPtr.getPointeeType())) {
      return CoercionResult{true, CoercionKind::MutRawToConstRaw};
    }
  }

  if (isUnion(target)) {
    auto& targetUnion = static_cast<const UnionType&>(target);
    if (isNull(source) && targetUnion.isNullable()) {
      return CoercionResult{true, CoercionKind::NullToNullableUnion};
    }
    if (unionContainsCoercionTarget(targetUnion, source)) {
      return CoercionResult{true, CoercionKind::UnionInjection};
    }
  }

  if (isExistential(source) && isExistential(target) && source.isSubtypeOf(target)) {
    return CoercionResult{true, CoercionKind::DynUpcast};
  }

  return CoercionResult{false, CoercionKind::Identity};
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
