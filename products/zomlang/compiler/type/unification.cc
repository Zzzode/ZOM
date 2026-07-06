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

#include "zomlang/compiler/type/unification.h"

#include "zomlang/compiler/type/array-type.h"
#include "zomlang/compiler/type/existential-type.h"
#include "zomlang/compiler/type/function-type.h"
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

UnificationEngine::UnifyResult success() {
  return UnificationEngine::UnifyResult{true, zc::String(),
                                        UnificationEngine::UnifyResult::FailureKind::None};
}

UnificationEngine::UnifyResult failure(
    zc::String errorMsg, UnificationEngine::UnifyResult::FailureKind kind =
                             UnificationEngine::UnifyResult::FailureKind::CannotUnify) {
  return UnificationEngine::UnifyResult{false, zc::mv(errorMsg), kind};
}

}  // namespace

// ===========================================================================
// Construction
// ===========================================================================

UnificationEngine::UnificationEngine(TypeEnv& env) : env_(env) {}

// ===========================================================================
// Public API
// ===========================================================================

bool UnificationEngine::unify(const Type& a, const Type& b) { return tryUnify(a, b).success; }

UnificationEngine::UnifyResult UnificationEngine::tryUnify(const Type& a, const Type& b) {
  // Resolve both types through the union-find structure.
  const Type& repA = env_.find(a);
  const Type& repB = env_.find(b);

  // Rule 0: Identity (same object).
  if (&repA == &repB) { return success(); }

  // Rule 12: ErrorType unifies with everything (error recovery).
  if (repA.isError() || repB.isError()) { return success(); }

  // Rule 1: Type variable binding.
  // If either representative is an unbound TypeVar, bind it via the
  // union-find structure.
  if (repA.isTypeVar() || repB.isTypeVar()) {
    if (repA.isTypeVar()) {
      auto& varA = static_cast<const TypeVar&>(repA);
      if (!unifyVarWith(varA, repB)) {
        auto kind = env_.occursIn(varA, repB) ? UnifyResult::FailureKind::InfiniteType
                                              : UnifyResult::FailureKind::CannotUnify;
        return failure(buildError(repA, repB, "type variable cannot be bound"), kind);
      }
    } else {
      auto& varB = static_cast<const TypeVar&>(repB);
      if (!unifyVarWith(varB, repA)) {
        auto kind = env_.occursIn(varB, repA) ? UnifyResult::FailureKind::InfiniteType
                                              : UnifyResult::FailureKind::CannotUnify;
        return failure(buildError(repA, repB, "type variable cannot be bound"), kind);
      }
    }
    return success();
  }

  // Both are concrete types: check kind compatibility.
  if (repA.getKind() != repB.getKind()) {
    return failure(buildError(repA, repB, "incompatible type kinds"));
  }

  // Dispatch to kind-specific structural unification.
  switch (repA.getKind()) {
    case TypeKind::Primitive: {
      auto& pa = static_cast<const PrimitiveType&>(repA);
      auto& pb = static_cast<const PrimitiveType&>(repB);
      if (unifyPrimitives(pa, pb)) { return success(); }
      return failure(buildError(repA, repB, "primitive types do not match"));
    }

    case TypeKind::Function: {
      auto& fa = static_cast<const FunctionType&>(repA);
      auto& fb = static_cast<const FunctionType&>(repB);
      if (unifyFunctions(fa, fb)) { return success(); }
      return failure(buildError(repA, repB, "function types do not match"));
    }

    case TypeKind::Tuple: {
      auto& ta = static_cast<const TupleType&>(repA);
      auto& tb = static_cast<const TupleType&>(repB);
      if (unifyTuples(ta, tb)) { return success(); }
      return failure(buildError(repA, repB, "tuple types do not match"));
    }

    case TypeKind::Object: {
      auto& oa = static_cast<const ObjectType&>(repA);
      auto& ob = static_cast<const ObjectType&>(repB);
      if (unifyObjects(oa, ob)) { return success(); }
      return failure(buildError(repA, repB, "object types do not match"));
    }

    case TypeKind::Array: {
      auto& aa = static_cast<const ArrayType&>(repA);
      auto& ab = static_cast<const ArrayType&>(repB);
      if (unifyArrays(aa, ab)) { return success(); }
      return failure(buildError(repA, repB, "array element types do not match"));
    }

    case TypeKind::Named: {
      auto& na = static_cast<const NamedType&>(repA);
      auto& nb = static_cast<const NamedType&>(repB);
      if (unifyNamed(na, nb)) { return success(); }
      return failure(buildError(repA, repB, "named types do not match"));
    }

    case TypeKind::Union: {
      auto& ua = static_cast<const UnionType&>(repA);
      auto& ub = static_cast<const UnionType&>(repB);
      if (unifyUnions(ua, ub)) { return success(); }
      return failure(buildError(repA, repB, "union types do not match"));
    }

    case TypeKind::Reference: {
      auto& ra = static_cast<const ReferenceType&>(repA);
      auto& rb = static_cast<const ReferenceType&>(repB);
      if (unifyReferences(ra, rb)) { return success(); }
      return failure(buildError(repA, repB, "reference types do not match"));
    }

    case TypeKind::RawPointer: {
      auto& pa = static_cast<const RawPointerType&>(repA);
      auto& pb = static_cast<const RawPointerType&>(repB);
      if (unifyRawPointers(pa, pb)) { return success(); }
      return failure(buildError(repA, repB, "raw pointer types do not match"));
    }

    case TypeKind::Existential: {
      auto& ea = static_cast<const ExistentialType&>(repA);
      auto& eb = static_cast<const ExistentialType&>(repB);
      if (unifyExistentials(ea, eb)) { return success(); }
      return failure(buildError(repA, repB, "existential types do not match"));
    }

    case TypeKind::Interface:
    case TypeKind::Intersection:
    case TypeKind::Associated:
      // These types are compared by structural equality for unification.
      if (repA.equals(repB)) { return success(); }
      return failure(buildError(repA, repB, "types do not match"));

    case TypeKind::TypeVar:
    case TypeKind::Error:
      // Already handled above.
      break;
  }

  return failure(buildError(repA, repB, "cannot unify"));
}

bool UnificationEngine::isSubtype(const Type& sub, const Type& super) {
  const Type& resolvedSub = env_.find(sub);
  const Type& resolvedSuper = env_.find(super);

  return resolvedSub.isSubtypeOf(resolvedSuper);
}

// ===========================================================================
// Rule 1: Type variable binding
// ===========================================================================

bool UnificationEngine::unifyVarWith(const TypeVar& var, const Type& other) {
  // Occurs check: prevent construction of infinite types (e.g., T = T -> T).
  if (env_.occursIn(var, other)) { return false; }

  // Validate upper bounds: other must be a subtype of each upper bound.
  for (size_t i = 0; i < var.getUpperBoundCount(); ++i) {
    const Type& bound = var.getUpperBound(i);
    // Resolve the bound through the env in case it contains type variables.
    const Type& resolvedBound = env_.find(bound);
    if (!other.isSubtypeOf(resolvedBound) && !other.isAssignableTo(resolvedBound)) { return false; }
  }

  // Validate lower bounds: each lower bound must be a subtype of other.
  for (size_t i = 0; i < var.getLowerBoundCount(); ++i) {
    const Type& bound = var.getLowerBound(i);
    const Type& resolvedBound = env_.find(bound);
    if (!resolvedBound.isSubtypeOf(other) && !resolvedBound.isAssignableTo(other)) { return false; }
  }

  // Bind via the union-find structure. The unite() method handles:
  // - Both TypeVars: union by rank
  // - TypeVar + concrete: binds the variable root to the concrete type
  env_.unite(var, other);

  return true;
}

// ===========================================================================
// Rule 2: Primitive types
// ===========================================================================

bool UnificationEngine::unifyPrimitives(const PrimitiveType& a, const PrimitiveType& b) {
  return a.getPrimitiveKind() == b.getPrimitiveKind();
}

// ===========================================================================
// Rule 3: Function types
// ===========================================================================

bool UnificationEngine::unifyFunctions(const FunctionType& a, const FunctionType& b) {
  size_t paramsA = a.getParamCount();
  size_t paramsB = b.getParamCount();
  bool aVariadic = a.isVariadic();
  bool bVariadic = b.isVariadic();

  // Parameter count must match, unless one is variadic and can absorb extras.
  if (paramsA != paramsB) {
    if (!aVariadic && !bVariadic) { return false; }

    // Variadic function can accept extra trailing parameters.
    size_t minParams = paramsA < paramsB ? paramsA : paramsB;

    // Unify the common parameters.
    for (size_t i = 0; i < minParams; ++i) {
      const Type& paramA = a.getParamType(i);
      const Type& paramB = b.getParamType(i);
      if (!unify(paramA, paramB)) { return false; }
    }

    // If both are variadic, all params must be unifiable (can't have
    // different fixed-parameter counts).
    if (aVariadic && bVariadic && paramsA != paramsB) { return false; }

    // Non-variadic function cannot have MORE parameters than the variadic one.
    if (aVariadic && paramsA > paramsB) { return false; }
    if (bVariadic && paramsB > paramsA) { return false; }
  } else {
    // Same parameter count: unify all.
    for (size_t i = 0; i < paramsA; ++i) {
      const Type& paramA = a.getParamType(i);
      const Type& paramB = b.getParamType(i);
      if (!unify(paramA, paramB)) { return false; }
    }
  }

  // Unify return types.
  const Type& retA = a.getReturnType();
  const Type& retB = b.getReturnType();
  if (!unify(retA, retB)) { return false; }

  // Unify raises types.
  auto raisesA = a.getRaisesType();
  auto raisesB = b.getRaisesType();

  bool hasRaisesA = raisesA != zc::none;
  bool hasRaisesB = raisesB != zc::none;

  if (hasRaisesA && hasRaisesB) {
    ZC_IF_SOME(ra, raisesA) {
      ZC_IF_SOME(rb, raisesB) {
        if (!unify(ra, rb)) { return false; }
      }
    }
  } else if (hasRaisesA != hasRaisesB) {
    // One raises, the other does not: not unifiable.
    return false;
  }

  return true;
}

// ===========================================================================
// Rule 4: Tuple types
// ===========================================================================

bool UnificationEngine::unifyTuples(const TupleType& a, const TupleType& b) {
  if (a.getElementCount() != b.getElementCount()) { return false; }

  for (size_t i = 0; i < a.getElementCount(); ++i) {
    const Type& elemA = a.getElementType(i);
    const Type& elemB = b.getElementType(i);
    if (!unify(elemA, elemB)) { return false; }
  }

  return true;
}

// ===========================================================================
// Rule 5: Object types
// ===========================================================================

bool UnificationEngine::unifyObjects(const ObjectType& a, const ObjectType& b) {
  // Equality unification requires the exact same member set.
  if (a.getMemberCount() != b.getMemberCount()) { return false; }

  auto membersA = a.getMembers();

  for (size_t i = 0; i < membersA.size(); ++i) {
    auto name = membersA[i].name;
    auto memberB = b.getMember(name);
    if (memberB == zc::none) { return false; }
    ZC_IF_SOME(typeB, memberB) {
      ZC_IF_SOME(typeA, membersA[i].type) {
        if (!unify(typeA, typeB)) { return false; }
      }
    }
  }

  return true;
}

// ===========================================================================
// Rule 6: Array types
// ===========================================================================

bool UnificationEngine::unifyArrays(const ArrayType& a, const ArrayType& b) {
  const Type& elemA = a.getElementType();
  const Type& elemB = b.getElementType();
  return unify(elemA, elemB);
}

// ===========================================================================
// Rule 7: Named types
// ===========================================================================

bool UnificationEngine::unifyNamed(const NamedType& a, const NamedType& b) {
  // Nominal equality: same name.
  if (a.getName() != b.getName()) { return false; }

  // Unify type arguments.
  if (a.getTypeArgCount() != b.getTypeArgCount()) { return false; }

  for (size_t i = 0; i < a.getTypeArgCount(); ++i) {
    const Type& argA = a.getTypeArg(i);
    const Type& argB = b.getTypeArg(i);
    if (!unify(argA, argB)) { return false; }
  }

  return true;
}

// ===========================================================================
// Rule 8: Union types
// ===========================================================================

bool UnificationEngine::unifyUnions(const UnionType& a, const UnionType& b) {
  size_t countA = a.getAlternativeCount();
  size_t countB = b.getAlternativeCount();

  if (countA != countB) { return false; }

  // Track which alternatives have been consumed.
  zc::Vector<bool> matchedB;
  matchedB.reserve(countB);
  for (size_t i = 0; i < countB; ++i) { matchedB.add(false); }

  // Forward direction: each alt in A must match some alt in B.
  for (size_t i = 0; i < countA; ++i) {
    const Type& altA = a.getAlternative(i);
    bool found = false;

    for (size_t j = 0; j < countB; ++j) {
      if (matchedB[j]) continue;

      const Type& altB = b.getAlternative(j);

      if (altA.equals(altB) || unify(altA, altB)) {
        matchedB[j] = true;
        found = true;
        break;
      }
    }

    if (!found) return false;
  }

  return true;
}

// ===========================================================================
// Rule 9: Reference types
// ===========================================================================

bool UnificationEngine::unifyReferences(const ReferenceType& a, const ReferenceType& b) {
  Mutability mutA = a.getMutability();
  Mutability mutB = b.getMutability();

  if (mutA != mutB) { return false; }

  const Type& pointeeA = a.getPointeeType();
  const Type& pointeeB = b.getPointeeType();
  return unify(pointeeA, pointeeB);
}

// ===========================================================================
// Rule 10: Raw pointer types
// ===========================================================================

bool UnificationEngine::unifyRawPointers(const RawPointerType& a, const RawPointerType& b) {
  Mutability mutA = a.getMutability();
  Mutability mutB = b.getMutability();

  if (mutA != mutB) { return false; }

  const Type& pointeeA = a.getPointeeType();
  const Type& pointeeB = b.getPointeeType();
  return unify(pointeeA, pointeeB);
}

// ===========================================================================
// Rule 11: Existential types
// ===========================================================================

bool UnificationEngine::unifyExistentials(const ExistentialType& a, const ExistentialType& b) {
  const Type& ifaceA = a.getInterfaceType();
  const Type& ifaceB = b.getInterfaceType();
  return unify(ifaceA, ifaceB);
}

// ===========================================================================
// Error message building
// ===========================================================================

zc::String UnificationEngine::buildError(const Type& a, const Type& b, const char* reason) const {
  return zc::str("cannot unify '", a.toString(), "' with '", b.toString(), "': ", reason);
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
