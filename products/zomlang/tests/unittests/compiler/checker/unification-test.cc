// Copyright (c) 2025 Zode.Z. All rights reserved
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

#include "zomlang/compiler/type/unification.h"

#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/type/array-type.h"
#include "zomlang/compiler/type/error-type.h"
#include "zomlang/compiler/type/existential-type.h"
#include "zomlang/compiler/type/function-type.h"
#include "zomlang/compiler/type/interface-type.h"
#include "zomlang/compiler/type/named-type.h"
#include "zomlang/compiler/type/primitive-type.h"
#include "zomlang/compiler/type/raw-pointer-type.h"
#include "zomlang/compiler/type/reference-type.h"
#include "zomlang/compiler/type/tuple-type.h"
#include "zomlang/compiler/type/type-env.h"
#include "zomlang/compiler/type/type-var.h"
#include "zomlang/compiler/type/union-type.h"

namespace zomlang {
namespace compiler {
namespace type {

// ============================================================================
// Primitive type unification
// ============================================================================

ZC_TEST("Unify.SamePrimitiveSucceeds") {
  TypeEnv env;
  UnificationEngine unifier(env);

  auto i32a = PrimitiveType::createI32();
  auto i32b = PrimitiveType::createI32();
  ZC_EXPECT(unifier.unify(*i32a, *i32b));
}

ZC_TEST("Unify.DifferentPrimitiveFails") {
  TypeEnv env;
  UnificationEngine unifier(env);

  auto i32 = PrimitiveType::createI32();
  auto str = PrimitiveType::createStr();
  ZC_EXPECT(!unifier.unify(*i32, *str));
}

ZC_TEST("Unify.BoolUnifiesWithBool") {
  TypeEnv env;
  UnificationEngine unifier(env);

  auto b1 = PrimitiveType::createBool();
  auto b2 = PrimitiveType::createBool();
  ZC_EXPECT(unifier.unify(*b1, *b2));
}

ZC_TEST("Unify.BoolDoesNotUnifyWithI32") {
  TypeEnv env;
  UnificationEngine unifier(env);

  auto b = PrimitiveType::createBool();
  auto i = PrimitiveType::createI32();
  ZC_EXPECT(!unifier.unify(*b, *i));
}

ZC_TEST("Unify.UnitUnifiesWithUnit") {
  TypeEnv env;
  UnificationEngine unifier(env);

  auto u1 = PrimitiveType::createUnit();
  auto u2 = PrimitiveType::createUnit();
  ZC_EXPECT(unifier.unify(*u1, *u2));
}

ZC_TEST("Unify.NullUnifiesOnlyWithNull") {
  TypeEnv env;
  UnificationEngine unifier(env);

  auto nullA = PrimitiveType::createNull();
  auto nullB = PrimitiveType::createNull();
  auto i32 = PrimitiveType::createI32();

  ZC_EXPECT(unifier.unify(*nullA, *nullB));
  ZC_EXPECT(!unifier.unify(*nullA, *i32));
}

ZC_TEST("Unify.NeverDoesNotUnifyWithI32") {
  TypeEnv env;
  UnificationEngine unifier(env);

  auto never = PrimitiveType::createNever();
  auto i32 = PrimitiveType::createI32();
  ZC_EXPECT(!unifier.unify(*never, *i32));
}

ZC_TEST("Unify.NeverDoesNotUnifyWithStr") {
  TypeEnv env;
  UnificationEngine unifier(env);

  auto never = PrimitiveType::createNever();
  auto str = PrimitiveType::createStr();
  ZC_EXPECT(!unifier.unify(*never, *str));
}

ZC_TEST("Unify.AnyDoesNotUnifyWithI32") {
  TypeEnv env;
  UnificationEngine unifier(env);

  auto any = PrimitiveType::createAny();
  auto i32 = PrimitiveType::createI32();
  ZC_EXPECT(!unifier.unify(*any, *i32));
}

// ============================================================================
// Type variable unification
// ============================================================================

ZC_TEST("Unify.TypeVarUnifiesWithI32") {
  TypeEnv env;
  UnificationEngine unifier(env);

  auto& tv = env.freshTypeVar();
  auto i32 = PrimitiveType::createI32();
  ZC_EXPECT(unifier.unify(tv, *i32));

  // After unification, the type var should be bound to i32
  auto& resolved = env.resolve(tv);
  ZC_EXPECT(isPrimitive(resolved));
}

ZC_TEST("Unify.I32UnifiesWithTypeVar") {
  TypeEnv env;
  UnificationEngine unifier(env);

  auto i32 = PrimitiveType::createI32();
  auto& tv = env.freshTypeVar();
  ZC_EXPECT(unifier.unify(*i32, tv));

  auto& resolved = env.resolve(tv);
  ZC_EXPECT(isPrimitive(resolved));
}

ZC_TEST("Unify.TwoTypeVarsUnify") {
  TypeEnv env;
  UnificationEngine unifier(env);

  auto& tv1 = env.freshTypeVar();
  auto& tv2 = env.freshTypeVar();
  ZC_EXPECT(unifier.unify(tv1, tv2));

  // Both should point to the same representative
  auto& r1 = env.find(tv1);
  auto& r2 = env.find(tv2);
  ZC_EXPECT(&r1 == &r2);
}

ZC_TEST("Unify.TypeVarUnifiesWithTypeVarThenBothResolve") {
  TypeEnv env;
  UnificationEngine unifier(env);

  auto& tv1 = env.freshTypeVar();
  auto& tv2 = env.freshTypeVar();
  unifier.unify(tv1, tv2);

  auto i32 = PrimitiveType::createI32();
  unifier.unify(tv1, *i32);

  // Both tv1 and tv2 should now resolve to i32
  auto& r1 = env.resolve(tv1);
  auto& r2 = env.resolve(tv2);
  ZC_EXPECT(isPrimitive(r1));
  ZC_EXPECT(isPrimitive(r2));
}

ZC_TEST("Unify.TypeVarOccursCheckPreventsInfinite") {
  TypeEnv env;
  UnificationEngine unifier(env);

  auto& tv = env.freshTypeVar();

  // Build a function type that references the type var
  zc::Vector<zc::Own<Type>> params;
  params.add(zc::Own<Type>(&tv, zc::NullDisposer::instance));  // non-owning reference
  auto ret = PrimitiveType::createUnit();
  // This would create an infinite type if occurs check works
  // Actually, let's test occurs check more directly
  ZC_EXPECT(env.occursIn(tv, tv));
}

ZC_TEST("Unify.TypeVarWithSelfReferentialFunctionFailsOccursCheck") {
  TypeEnv env;
  UnificationEngine unifier(env);

  auto& tv = env.freshTypeVar("T"_zc);
  zc::Vector<zc::Own<Type>> params;
  params.add(zc::Own<Type>(&tv, zc::NullDisposer::instance));
  FunctionType selfReferential(zc::mv(params), PrimitiveType::createUnit());

  auto result = unifier.tryUnify(tv, selfReferential);
  ZC_EXPECT(!result.success);
  ZC_EXPECT(result.failureKind == UnificationEngine::UnifyResult::FailureKind::InfiniteType);
  ZC_EXPECT(result.errorMsg.size() > 0);
  ZC_EXPECT(isTypeVar(env.find(tv)));
}

// ============================================================================
// Function type unification
// ============================================================================

ZC_TEST("Unify.FunctionTypesSameSignature") {
  TypeEnv env;
  UnificationEngine unifier(env);

  zc::Vector<zc::Own<Type>> params1;
  params1.add(PrimitiveType::createI32());
  auto ret1 = PrimitiveType::createBool();
  FunctionType fn1(zc::mv(params1), zc::mv(ret1));

  zc::Vector<zc::Own<Type>> params2;
  params2.add(PrimitiveType::createI32());
  auto ret2 = PrimitiveType::createBool();
  FunctionType fn2(zc::mv(params2), zc::mv(ret2));

  ZC_EXPECT(unifier.unify(fn1, fn2));
}

ZC_TEST("Unify.FunctionTypesDifferentParamFails") {
  TypeEnv env;
  UnificationEngine unifier(env);

  zc::Vector<zc::Own<Type>> params1;
  params1.add(PrimitiveType::createI32());
  auto ret1 = PrimitiveType::createBool();
  FunctionType fn1(zc::mv(params1), zc::mv(ret1));

  zc::Vector<zc::Own<Type>> params2;
  params2.add(PrimitiveType::createStr());
  auto ret2 = PrimitiveType::createBool();
  FunctionType fn2(zc::mv(params2), zc::mv(ret2));

  ZC_EXPECT(!unifier.unify(fn1, fn2));
}

ZC_TEST("Unify.FunctionTypesDifferentReturnFails") {
  TypeEnv env;
  UnificationEngine unifier(env);

  zc::Vector<zc::Own<Type>> params1;
  params1.add(PrimitiveType::createI32());
  auto ret1 = PrimitiveType::createBool();
  FunctionType fn1(zc::mv(params1), zc::mv(ret1));

  zc::Vector<zc::Own<Type>> params2;
  params2.add(PrimitiveType::createI32());
  auto ret2 = PrimitiveType::createStr();
  FunctionType fn2(zc::mv(params2), zc::mv(ret2));

  ZC_EXPECT(!unifier.unify(fn1, fn2));
}

ZC_TEST("Unify.FunctionTypesDifferentArityFails") {
  TypeEnv env;
  UnificationEngine unifier(env);

  zc::Vector<zc::Own<Type>> params1;
  params1.add(PrimitiveType::createI32());
  auto ret1 = PrimitiveType::createBool();
  FunctionType fn1(zc::mv(params1), zc::mv(ret1));

  zc::Vector<zc::Own<Type>> params2;
  params2.add(PrimitiveType::createI32());
  params2.add(PrimitiveType::createStr());
  auto ret2 = PrimitiveType::createBool();
  FunctionType fn2(zc::mv(params2), zc::mv(ret2));

  ZC_EXPECT(!unifier.unify(fn1, fn2));
}

ZC_TEST("Unify.FunctionTypesWithTypeVars") {
  TypeEnv env;
  UnificationEngine unifier(env);

  auto& tv = env.freshTypeVar();

  zc::Vector<zc::Own<Type>> params1;
  params1.add(zc::Own<Type>(&tv, zc::NullDisposer::instance));
  auto ret1 = PrimitiveType::createBool();
  FunctionType fn1(zc::mv(params1), zc::mv(ret1));

  zc::Vector<zc::Own<Type>> params2;
  params2.add(PrimitiveType::createI32());
  auto ret2 = PrimitiveType::createBool();
  FunctionType fn2(zc::mv(params2), zc::mv(ret2));

  ZC_EXPECT(unifier.unify(fn1, fn2));

  // The type var should now resolve to i32
  auto& resolved = env.resolve(tv);
  ZC_EXPECT(isPrimitive(resolved));
}

// ============================================================================
// Tuple type unification
// ============================================================================

ZC_TEST("Unify.TupleTypesSameElements") {
  TypeEnv env;
  UnificationEngine unifier(env);

  zc::Vector<zc::Own<Type>> elems1;
  elems1.add(PrimitiveType::createI32());
  elems1.add(PrimitiveType::createStr());
  TupleType t1(zc::mv(elems1));

  zc::Vector<zc::Own<Type>> elems2;
  elems2.add(PrimitiveType::createI32());
  elems2.add(PrimitiveType::createStr());
  TupleType t2(zc::mv(elems2));

  ZC_EXPECT(unifier.unify(t1, t2));
}

ZC_TEST("Unify.TupleTypesDifferentElementsFails") {
  TypeEnv env;
  UnificationEngine unifier(env);

  zc::Vector<zc::Own<Type>> elems1;
  elems1.add(PrimitiveType::createI32());
  TupleType t1(zc::mv(elems1));

  zc::Vector<zc::Own<Type>> elems2;
  elems2.add(PrimitiveType::createStr());
  TupleType t2(zc::mv(elems2));

  ZC_EXPECT(!unifier.unify(t1, t2));
}

ZC_TEST("Unify.TupleTypesDifferentLengthFails") {
  TypeEnv env;
  UnificationEngine unifier(env);

  zc::Vector<zc::Own<Type>> elems1;
  elems1.add(PrimitiveType::createI32());
  TupleType t1(zc::mv(elems1));

  zc::Vector<zc::Own<Type>> elems2;
  elems2.add(PrimitiveType::createI32());
  elems2.add(PrimitiveType::createStr());
  TupleType t2(zc::mv(elems2));

  ZC_EXPECT(!unifier.unify(t1, t2));
}

ZC_TEST("Unify.EmptyTuplesUnify") {
  TypeEnv env;
  UnificationEngine unifier(env);

  zc::Vector<zc::Own<Type>> empty;
  TupleType t1(zc::mv(empty));
  zc::Vector<zc::Own<Type>> empty2;
  TupleType t2(zc::mv(empty2));

  ZC_EXPECT(unifier.unify(t1, t2));
}

// ============================================================================
// Array type unification
// ============================================================================

ZC_TEST("Unify.ArrayTypesSameElem") {
  TypeEnv env;
  UnificationEngine unifier(env);

  ArrayType a1(PrimitiveType::createI32());
  ArrayType a2(PrimitiveType::createI32());
  ZC_EXPECT(unifier.unify(a1, a2));
}

ZC_TEST("Unify.ArrayTypesDifferentElemFails") {
  TypeEnv env;
  UnificationEngine unifier(env);

  ArrayType a1(PrimitiveType::createI32());
  ArrayType a2(PrimitiveType::createStr());
  ZC_EXPECT(!unifier.unify(a1, a2));
}

// ============================================================================
// Named type unification
// ============================================================================

ZC_TEST("Unify.NamedTypesSameName") {
  TypeEnv env;
  UnificationEngine unifier(env);

  NamedType n1("MyClass"_zc);
  NamedType n2("MyClass"_zc);
  ZC_EXPECT(unifier.unify(n1, n2));
}

ZC_TEST("Unify.NamedTypesDifferentNameFails") {
  TypeEnv env;
  UnificationEngine unifier(env);

  NamedType n1("Foo"_zc);
  NamedType n2("Bar"_zc);
  ZC_EXPECT(!unifier.unify(n1, n2));
}

// ============================================================================
// Reference type unification
// ============================================================================

ZC_TEST("Unify.ReferenceTypesSamePointeeConst") {
  TypeEnv env;
  UnificationEngine unifier(env);

  ReferenceType r1(PrimitiveType::createI32(), Mutability::Const);
  ReferenceType r2(PrimitiveType::createI32(), Mutability::Const);
  ZC_EXPECT(unifier.unify(r1, r2));
}

ZC_TEST("Unify.ReferenceTypesDifferentPointeeFails") {
  TypeEnv env;
  UnificationEngine unifier(env);

  ReferenceType r1(PrimitiveType::createI32(), Mutability::Const);
  ReferenceType r2(PrimitiveType::createStr(), Mutability::Const);
  ZC_EXPECT(!unifier.unify(r1, r2));
}

ZC_TEST("Unify.ReferenceTypesDifferentMutabilityFails") {
  TypeEnv env;
  UnificationEngine unifier(env);

  ReferenceType r1(PrimitiveType::createI32(), Mutability::Mutable);
  ReferenceType r2(PrimitiveType::createI32(), Mutability::Const);
  ZC_EXPECT(!unifier.unify(r1, r2));
}

// ============================================================================
// Raw pointer type unification
// ============================================================================

ZC_TEST("Unify.RawPointerTypesSamePointee") {
  TypeEnv env;
  UnificationEngine unifier(env);

  RawPointerType p1(PrimitiveType::createI32(), Mutability::Const);
  RawPointerType p2(PrimitiveType::createI32(), Mutability::Const);
  ZC_EXPECT(unifier.unify(p1, p2));
}

ZC_TEST("Unify.RawPointerTypesDifferentPointeeFails") {
  TypeEnv env;
  UnificationEngine unifier(env);

  RawPointerType p1(PrimitiveType::createI32(), Mutability::Const);
  RawPointerType p2(PrimitiveType::createStr(), Mutability::Const);
  ZC_EXPECT(!unifier.unify(p1, p2));
}

// ============================================================================
// Error type unification
// ============================================================================

ZC_TEST("Unify.ErrorTypeUnifiesWithI32") {
  TypeEnv env;
  UnificationEngine unifier(env);

  ErrorType err;
  auto i32 = PrimitiveType::createI32();
  ZC_EXPECT(unifier.unify(err, *i32));
}

ZC_TEST("Unify.I32UnifiesWithErrorType") {
  TypeEnv env;
  UnificationEngine unifier(env);

  auto i32 = PrimitiveType::createI32();
  ErrorType err;
  ZC_EXPECT(unifier.unify(*i32, err));
}

ZC_TEST("Unify.ErrorTypeUnifiesWithErrorType") {
  TypeEnv env;
  UnificationEngine unifier(env);

  ErrorType e1;
  ErrorType e2;
  ZC_EXPECT(unifier.unify(e1, e2));
}

ZC_TEST("Unify.ErrorTypeUnifiesWithFunction") {
  TypeEnv env;
  UnificationEngine unifier(env);

  ErrorType err;
  zc::Vector<zc::Own<Type>> params;
  auto ret = PrimitiveType::createUnit();
  FunctionType fn(zc::mv(params), zc::mv(ret));
  ZC_EXPECT(unifier.unify(err, fn));
}

// ============================================================================
// tryUnify with diagnostic output
// ============================================================================

ZC_TEST("Unify.TryUnifySuccessReturnsNoError") {
  TypeEnv env;
  UnificationEngine unifier(env);

  auto i32a = PrimitiveType::createI32();
  auto i32b = PrimitiveType::createI32();
  auto result = unifier.tryUnify(*i32a, *i32b);
  ZC_EXPECT(result.success);
  ZC_EXPECT(result.errorMsg.size() == 0);
}

ZC_TEST("Unify.TryUnifyFailureReturnsError") {
  TypeEnv env;
  UnificationEngine unifier(env);

  auto i32 = PrimitiveType::createI32();
  auto str = PrimitiveType::createStr();
  auto result = unifier.tryUnify(*i32, *str);
  ZC_EXPECT(!result.success);
  ZC_EXPECT(result.failureKind == UnificationEngine::UnifyResult::FailureKind::CannotUnify);
  ZC_EXPECT(result.errorMsg.size() > 0);
}

ZC_TEST("Unify.TryUnifyBoolConversion") {
  TypeEnv env;
  UnificationEngine unifier(env);

  auto i32a = PrimitiveType::createI32();
  auto i32b = PrimitiveType::createI32();
  auto result = unifier.tryUnify(*i32a, *i32b);
  ZC_EXPECT(static_cast<bool>(result));
}

// ============================================================================
// Subtype checking via unification
// ============================================================================

ZC_TEST("Unify.IsSubtypeNeverOfI32") {
  TypeEnv env;
  UnificationEngine unifier(env);

  auto never = PrimitiveType::createNever();
  auto i32 = PrimitiveType::createI32();
  ZC_EXPECT(unifier.isSubtype(*never, *i32));
}

ZC_TEST("Unify.IsSubtypeI32OfAny") {
  TypeEnv env;
  UnificationEngine unifier(env);

  auto i32 = PrimitiveType::createI32();
  auto any = PrimitiveType::createAny();
  ZC_EXPECT(unifier.isSubtype(*i32, *any));
}

ZC_TEST("Unify.IsSubtypeI32NotOfStr") {
  TypeEnv env;
  UnificationEngine unifier(env);

  auto i32 = PrimitiveType::createI32();
  auto str = PrimitiveType::createStr();
  ZC_EXPECT(!unifier.isSubtype(*i32, *str));
}

ZC_TEST("Unify.IsSubtypeReflexive") {
  TypeEnv env;
  UnificationEngine unifier(env);

  auto i32 = PrimitiveType::createI32();
  ZC_EXPECT(unifier.isSubtype(*i32, *i32));
}

ZC_TEST("Unify.IsSubtypeMutRefOfConstRef") {
  TypeEnv env;
  UnificationEngine unifier(env);

  ReferenceType mutRef(PrimitiveType::createI32(), Mutability::Mutable);
  ReferenceType constRef(PrimitiveType::createI32(), Mutability::Const);
  ZC_EXPECT(unifier.isSubtype(mutRef, constRef));
}

ZC_TEST("Unify.IsSubtypeConstRefNotOfMutRef") {
  TypeEnv env;
  UnificationEngine unifier(env);

  ReferenceType constRef(PrimitiveType::createI32(), Mutability::Const);
  ReferenceType mutRef(PrimitiveType::createI32(), Mutability::Mutable);
  ZC_EXPECT(!unifier.isSubtype(constRef, mutRef));
}

// ============================================================================
// Complex unification scenarios
// ============================================================================

ZC_TEST("Unify.NestedFunctionTypes") {
  TypeEnv env;
  UnificationEngine unifier(env);

  // fn(fn(i32) -> bool) -> str
  zc::Vector<zc::Own<Type>> innerParams;
  innerParams.add(PrimitiveType::createI32());
  auto innerRet = PrimitiveType::createBool();
  auto innerFn = zc::heap<FunctionType>(zc::mv(innerParams), zc::mv(innerRet));

  zc::Vector<zc::Own<Type>> outerParams;
  outerParams.add(zc::mv(innerFn));
  auto outerRet = PrimitiveType::createStr();
  FunctionType outerFn1(zc::mv(outerParams), zc::mv(outerRet));

  // Same structure
  zc::Vector<zc::Own<Type>> innerParams2;
  innerParams2.add(PrimitiveType::createI32());
  auto innerRet2 = PrimitiveType::createBool();
  auto innerFn2 = zc::heap<FunctionType>(zc::mv(innerParams2), zc::mv(innerRet2));

  zc::Vector<zc::Own<Type>> outerParams2;
  outerParams2.add(zc::mv(innerFn2));
  auto outerRet2 = PrimitiveType::createStr();
  FunctionType outerFn2(zc::mv(outerParams2), zc::mv(outerRet2));

  ZC_EXPECT(unifier.unify(outerFn1, outerFn2));
}

ZC_TEST("Unify.TypeVarInFunctionParam") {
  TypeEnv env;
  UnificationEngine unifier(env);

  auto& tv = env.freshTypeVar();

  // fn(T) -> bool
  zc::Vector<zc::Own<Type>> params1;
  params1.add(zc::Own<Type>(&tv, zc::NullDisposer::instance));
  auto ret1 = PrimitiveType::createBool();
  FunctionType fn1(zc::mv(params1), zc::mv(ret1));

  // fn(i32) -> bool
  zc::Vector<zc::Own<Type>> params2;
  params2.add(PrimitiveType::createI32());
  auto ret2 = PrimitiveType::createBool();
  FunctionType fn2(zc::mv(params2), zc::mv(ret2));

  ZC_EXPECT(unifier.unify(fn1, fn2));
  auto& resolved = env.resolve(tv);
  ZC_EXPECT(isPrimitive(resolved));
}

ZC_TEST("Unify.UnionTypeWithI32Fails") {
  TypeEnv env;
  UnificationEngine unifier(env);

  zc::Vector<zc::Own<Type>> alts;
  alts.add(PrimitiveType::createI32());
  alts.add(PrimitiveType::createStr());
  UnionType unionTy(zc::mv(alts));

  auto i32 = PrimitiveType::createI32();
  ZC_EXPECT(!unifier.unify(*i32, unionTy));
}

ZC_TEST("Unify.UnionTypesIgnoreAlternativeOrder") {
  TypeEnv env;
  UnificationEngine unifier(env);

  zc::Vector<zc::Own<Type>> leftAlts;
  leftAlts.add(PrimitiveType::createI32());
  leftAlts.add(PrimitiveType::createStr());
  UnionType left(zc::mv(leftAlts));

  zc::Vector<zc::Own<Type>> rightAlts;
  rightAlts.add(PrimitiveType::createStr());
  rightAlts.add(PrimitiveType::createI32());
  UnionType right(zc::mv(rightAlts));

  ZC_EXPECT(unifier.unify(left, right));
}

ZC_TEST("Unify.UnionTypesRejectDifferentAlternatives") {
  TypeEnv env;
  UnificationEngine unifier(env);

  zc::Vector<zc::Own<Type>> leftAlts;
  leftAlts.add(PrimitiveType::createI32());
  leftAlts.add(PrimitiveType::createStr());
  UnionType left(zc::mv(leftAlts));

  zc::Vector<zc::Own<Type>> rightAlts;
  rightAlts.add(PrimitiveType::createI32());
  rightAlts.add(PrimitiveType::createBool());
  UnionType right(zc::mv(rightAlts));

  ZC_EXPECT(!unifier.unify(left, right));
}

ZC_TEST("Unify.IdenticalExistentialsUnify") {
  TypeEnv env;
  UnificationEngine unifier(env);

  ExistentialType left(zc::heap<InterfaceType>("Drawable"_zc));
  ExistentialType right(zc::heap<InterfaceType>("Drawable"_zc));

  ZC_EXPECT(unifier.unify(left, right));
}

ZC_TEST("Unify.ExistentialMarkersMustMatch") {
  TypeEnv env;
  UnificationEngine unifier(env);
  zc::Vector<zc::StringPtr> firstMarkers;
  firstMarkers.add("Sendable"_zc);
  firstMarkers.add("Shared"_zc);
  zc::Vector<zc::StringPtr> secondMarkers;
  secondMarkers.add("Shared"_zc);
  secondMarkers.add("Sendable"_zc);
  zc::Vector<zc::StringPtr> sendableMarker;
  sendableMarker.add("Sendable"_zc);

  ExistentialType left(zc::heap<InterfaceType>("Drawable"_zc), firstMarkers.asPtr());
  ExistentialType same(zc::heap<InterfaceType>("Drawable"_zc), secondMarkers.asPtr());
  ExistentialType missingMarker(zc::heap<InterfaceType>("Drawable"_zc), sendableMarker.asPtr());
  ExistentialType plain(zc::heap<InterfaceType>("Drawable"_zc));

  ZC_EXPECT(unifier.unify(left, same));
  ZC_EXPECT(!unifier.unify(left, missingMarker));
  ZC_EXPECT(!unifier.unify(left, plain));
}

ZC_TEST("Unify.DifferentExistentialsFail") {
  TypeEnv env;
  UnificationEngine unifier(env);

  ExistentialType left(zc::heap<InterfaceType>("Drawable"_zc));
  ExistentialType right(zc::heap<InterfaceType>("Serializable"_zc));

  ZC_EXPECT(!unifier.unify(left, right));
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
