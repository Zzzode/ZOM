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

#include "zomlang/compiler/type/function-type.h"

#include "zc/core/vector.h"

namespace zomlang {
namespace compiler {
namespace type {

struct FunctionType::Impl {
  zc::Vector<zc::Own<Type>> params;
  zc::Own<Type> returnType;
  zc::Own<Type> raises;
  zc::Vector<zc::Own<GenericParam>> genericParams;
  bool variadic = false;

  Impl(zc::Vector<zc::Own<Type>> p, zc::Own<Type> ret)
      : params(zc::mv(p)), returnType(zc::mv(ret)) {}
};

FunctionType::FunctionType(zc::Vector<zc::Own<Type>> params, zc::Own<Type> returnType)
    : impl(zc::heap<Impl>(zc::mv(params), zc::mv(returnType))) {}

FunctionType::~FunctionType() noexcept(false) = default;

FunctionType::FunctionType(FunctionType&& other) noexcept = default;

FunctionType& FunctionType::operator=(FunctionType&& other) noexcept = default;

size_t FunctionType::getParamCount() const { return impl->params.size(); }

const Type& FunctionType::getParamType(size_t index) const { return *impl->params[index]; }

const Type& FunctionType::getReturnType() const { return *impl->returnType; }

zc::Maybe<const Type&> FunctionType::getRaisesType() const {
  if (impl->raises) { return *impl->raises; }
  return zc::none;
}

void FunctionType::setRaisesType(zc::Own<Type> raises) { impl->raises = zc::mv(raises); }

bool FunctionType::isVariadic() const { return impl->variadic; }

void FunctionType::setVariadic(bool variadic) { impl->variadic = variadic; }

// =========================================================================
// Generic parameter support
// =========================================================================

size_t FunctionType::getGenericParamCount() const { return impl->genericParams.size(); }

const GenericParam& FunctionType::getGenericParam(size_t index) const {
  return *impl->genericParams[index];
}

void FunctionType::addGenericParam(zc::Own<GenericParam> param) {
  impl->genericParams.add(zc::mv(param));
}

bool FunctionType::isGeneric() const { return impl->genericParams.size() > 0; }

zc::String FunctionType::toString() const {
  zc::String result = zc::heapString("fn");

  // Show generic parameters if present
  if (isGeneric()) {
    result = zc::str(result, "<");
    for (size_t i = 0; i < impl->genericParams.size(); ++i) {
      if (i > 0) { result = zc::str(result, ", "); }
      result = zc::str(result, impl->genericParams[i]->name);
    }
    result = zc::str(result, ">");
  }

  result = zc::str(result, "(");

  for (size_t i = 0; i < impl->params.size(); ++i) {
    if (i > 0) { result = zc::str(result, ", "); }
    result = zc::str(result, impl->params[i]->toString());
  }

  if (impl->variadic) {
    if (impl->params.size() > 0) { result = zc::str(result, ", "); }
    result = zc::str(result, "...");
  }

  result = zc::str(result, ") -> ", impl->returnType->toString());

  if (impl->raises) { result = zc::str(result, " raises ", impl->raises->toString()); }

  return result;
}

bool FunctionType::equals(const Type& other) const {
  if (this == &other) { return true; }
  if (other.getKind() != TypeKind::Function) { return false; }

  auto& otherFn = static_cast<const FunctionType&>(other);

  // Check parameter count
  if (impl->params.size() != otherFn.impl->params.size()) { return false; }

  // Check parameter types (contravariant in function subtyping, but for equality
  // we need exact match)
  for (size_t i = 0; i < impl->params.size(); ++i) {
    if (!impl->params[i]->equals(*otherFn.impl->params[i])) { return false; }
  }

  // Check return type
  if (!impl->returnType->equals(*otherFn.impl->returnType)) { return false; }

  // Check raises type
  bool thisHasRaises = impl->raises != nullptr;
  bool otherHasRaises = otherFn.impl->raises != nullptr;
  if (thisHasRaises != otherHasRaises) { return false; }
  if (thisHasRaises && !impl->raises->equals(*otherFn.impl->raises)) { return false; }

  // Check variadic
  if (impl->variadic != otherFn.impl->variadic) { return false; }

  return true;
}

bool FunctionType::isSubtypeOf(const Type& other) const {
  if (hasBasicSubtypeRelation(*this, other)) { return true; }

  if (other.getKind() != TypeKind::Function) { return false; }

  auto& otherFn = static_cast<const FunctionType&>(other);

  // Function subtyping:
  // - Parameter types are contravariant
  // - Return type is covariant
  // - Raises type is covariant (subtype raises fewer/more specific exceptions)

  // Parameter count must match (unless variadic)
  if (impl->params.size() != otherFn.impl->params.size()) {
    // Allow if this is non-variadic and other is variadic with more params,
    // or vice versa in some cases
    if (!impl->variadic && !otherFn.impl->variadic) { return false; }
  }

  size_t minParams = impl->params.size() < otherFn.impl->params.size()
                         ? impl->params.size()
                         : otherFn.impl->params.size();

  // Contravariant parameter check: other's param type must be subtype of this's
  for (size_t i = 0; i < minParams; ++i) {
    if (!otherFn.impl->params[i]->isSubtypeOf(*impl->params[i])) { return false; }
  }

  // Covariant return check: this's return must be subtype of other's return
  if (!impl->returnType->isSubtypeOf(*otherFn.impl->returnType)) { return false; }

  // Covariant raises check
  bool thisHasRaises = impl->raises != nullptr;
  bool otherHasRaises = otherFn.impl->raises != nullptr;
  if (thisHasRaises && otherHasRaises) {
    if (!impl->raises->isSubtypeOf(*otherFn.impl->raises)) { return false; }
  } else if (thisHasRaises && !otherHasRaises) {
    // This raises but other doesn't - not a subtype
    return false;
  }
  // If other raises but this doesn't, that's fine (covariant)

  return true;
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
