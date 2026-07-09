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

#include "zomlang/compiler/type/type.h"

#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"
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
#include "zomlang/compiler/type/type-algebra.h"
#include "zomlang/compiler/type/type-var.h"
#include "zomlang/compiler/type/union-type.h"

namespace zomlang {
namespace compiler {
namespace type {

// ============================================================================
// PrimitiveType creation and properties
// ============================================================================

ZC_TEST("PrimitiveType.CreateI32") {
  auto ty = PrimitiveType::createI32();
  ZC_EXPECT(ty->getKind() == TypeKind::Primitive);
  ZC_EXPECT(ty->getPrimitiveKind() == PrimitiveKind::I32);
  ZC_EXPECT(isPrimitive(*ty));
  ZC_EXPECT(isInteger(*ty));
  ZC_EXPECT(isNumeric(*ty));
  ZC_EXPECT(isSignedInteger(*ty));
}

ZC_TEST("PrimitiveType.CreateI8") {
  auto ty = PrimitiveType::createI8();
  ZC_EXPECT(ty->getPrimitiveKind() == PrimitiveKind::I8);
  ZC_EXPECT(isInteger(*ty));
  ZC_EXPECT(isSignedInteger(*ty));
}

ZC_TEST("PrimitiveType.CreateI16") {
  auto ty = PrimitiveType::createI16();
  ZC_EXPECT(ty->getPrimitiveKind() == PrimitiveKind::I16);
  ZC_EXPECT(isInteger(*ty));
}

ZC_TEST("PrimitiveType.CreateI64") {
  auto ty = PrimitiveType::createI64();
  ZC_EXPECT(ty->getPrimitiveKind() == PrimitiveKind::I64);
  ZC_EXPECT(isInteger(*ty));
}

ZC_TEST("PrimitiveType.CreateU8") {
  auto ty = PrimitiveType::createU8();
  ZC_EXPECT(ty->getPrimitiveKind() == PrimitiveKind::U8);
  ZC_EXPECT(isInteger(*ty));
  ZC_EXPECT(isUnsignedInteger(*ty));
}

ZC_TEST("PrimitiveType.CreateU16") {
  auto ty = PrimitiveType::createU16();
  ZC_EXPECT(ty->getPrimitiveKind() == PrimitiveKind::U16);
  ZC_EXPECT(isUnsignedInteger(*ty));
}

ZC_TEST("PrimitiveType.CreateU32") {
  auto ty = PrimitiveType::createU32();
  ZC_EXPECT(ty->getPrimitiveKind() == PrimitiveKind::U32);
  ZC_EXPECT(isUnsignedInteger(*ty));
}

ZC_TEST("PrimitiveType.CreateU64") {
  auto ty = PrimitiveType::createU64();
  ZC_EXPECT(ty->getPrimitiveKind() == PrimitiveKind::U64);
  ZC_EXPECT(isUnsignedInteger(*ty));
}

ZC_TEST("PrimitiveType.CreateF32") {
  auto ty = PrimitiveType::createF32();
  ZC_EXPECT(ty->getPrimitiveKind() == PrimitiveKind::F32);
  ZC_EXPECT(isFloatingPoint(*ty));
  ZC_EXPECT(isNumeric(*ty));
}

ZC_TEST("PrimitiveType.CreateF64") {
  auto ty = PrimitiveType::createF64();
  ZC_EXPECT(ty->getPrimitiveKind() == PrimitiveKind::F64);
  ZC_EXPECT(isFloatingPoint(*ty));
  ZC_EXPECT(isNumeric(*ty));
}

ZC_TEST("PrimitiveType.CreateBool") {
  auto ty = PrimitiveType::createBool();
  ZC_EXPECT(ty->getPrimitiveKind() == PrimitiveKind::Bool);
  ZC_EXPECT(!isNumeric(*ty));
  ZC_EXPECT(!isInteger(*ty));
}

ZC_TEST("PrimitiveType.CreateStr") {
  auto ty = PrimitiveType::createStr();
  ZC_EXPECT(ty->getPrimitiveKind() == PrimitiveKind::Str);
  ZC_EXPECT(!isNumeric(*ty));
}

ZC_TEST("PrimitiveType.CreateChar") {
  auto ty = PrimitiveType::createChar();
  ZC_EXPECT(ty->getPrimitiveKind() == PrimitiveKind::Char);
}

ZC_TEST("PrimitiveType.CreateNull") {
  auto ty = PrimitiveType::createNull();
  ZC_EXPECT(ty->getPrimitiveKind() == PrimitiveKind::Null);
  ZC_EXPECT(isNull(*ty));
}

ZC_TEST("PrimitiveType.CreateUnit") {
  auto ty = PrimitiveType::createUnit();
  ZC_EXPECT(ty->getPrimitiveKind() == PrimitiveKind::Unit);
  ZC_EXPECT(isUnit(*ty));
}

ZC_TEST("PrimitiveType.CreateNever") {
  auto ty = PrimitiveType::createNever();
  ZC_EXPECT(ty->getPrimitiveKind() == PrimitiveKind::Never);
  ZC_EXPECT(isNever(*ty));
}

ZC_TEST("PrimitiveType.CreateAny") {
  auto ty = PrimitiveType::createAny();
  ZC_EXPECT(ty->getPrimitiveKind() == PrimitiveKind::Any);
  ZC_EXPECT(isAny(*ty));
}

// ============================================================================
// PrimitiveType equality
// ============================================================================

ZC_TEST("PrimitiveType.EqualitySameType") {
  auto a = PrimitiveType::createI32();
  auto b = PrimitiveType::createI32();
  ZC_EXPECT(a->equals(*b));
  ZC_EXPECT(b->equals(*a));
}

ZC_TEST("PrimitiveType.EqualityDifferentType") {
  auto i32 = PrimitiveType::createI32();
  auto i64 = PrimitiveType::createI64();
  ZC_EXPECT(!i32->equals(*i64));
}

ZC_TEST("PrimitiveType.EqualityBoolVsStr") {
  auto b = PrimitiveType::createBool();
  auto s = PrimitiveType::createStr();
  ZC_EXPECT(!b->equals(*s));
}

// ============================================================================
// PrimitiveType string representation
// ============================================================================

ZC_TEST("PrimitiveType.ToStringI32") {
  auto ty = PrimitiveType::createI32();
  auto s = ty->toString();
  ZC_EXPECT(s.contains("i32"));
}

ZC_TEST("PrimitiveType.ToStringBool") {
  auto ty = PrimitiveType::createBool();
  auto s = ty->toString();
  ZC_EXPECT(s.contains("bool"));
}

ZC_TEST("PrimitiveType.ToStringStr") {
  auto ty = PrimitiveType::createStr();
  auto s = ty->toString();
  ZC_EXPECT(s.contains("str"));
}

ZC_TEST("PrimitiveType.ToStringNever") {
  auto ty = PrimitiveType::createNever();
  auto s = ty->toString();
  ZC_EXPECT(s.contains("never"));
}

ZC_TEST("PrimitiveType.ToStringUnit") {
  auto ty = PrimitiveType::createUnit();
  auto s = ty->toString();
  ZC_EXPECT(s.contains("unit"));
}

// ============================================================================
// PrimitiveType subtyping
// ============================================================================

ZC_TEST("PrimitiveType.NeverIsSubtypeOfEverything") {
  auto never = PrimitiveType::createNever();
  auto i32 = PrimitiveType::createI32();
  auto str = PrimitiveType::createStr();
  auto boolTy = PrimitiveType::createBool();
  ZC_EXPECT(never->isSubtypeOf(*i32));
  ZC_EXPECT(never->isSubtypeOf(*str));
  ZC_EXPECT(never->isSubtypeOf(*boolTy));
}

ZC_TEST("PrimitiveType.EverythingIsSubtypeOfAny") {
  auto any = PrimitiveType::createAny();
  auto i32 = PrimitiveType::createI32();
  auto str = PrimitiveType::createStr();
  ZC_EXPECT(i32->isSubtypeOf(*any));
  ZC_EXPECT(str->isSubtypeOf(*any));
}

ZC_TEST("PrimitiveType.SameTypeIsSubtype") {
  auto i32 = PrimitiveType::createI32();
  ZC_EXPECT(i32->isSubtypeOf(*i32));
}

ZC_TEST("PrimitiveType.I32NotSubtypeOfStr") {
  auto i32 = PrimitiveType::createI32();
  auto str = PrimitiveType::createStr();
  ZC_EXPECT(!i32->isSubtypeOf(*str));
}

ZC_TEST("PrimitiveType.NullIsSubtypeOfNull") {
  auto null = PrimitiveType::createNull();
  ZC_EXPECT(null->isSubtypeOf(*null));
}

ZC_TEST("PrimitiveType.NullIsSubtypeOfExplicitNullableUnion") {
  auto null = PrimitiveType::createNull();
  zc::Vector<zc::Own<Type>> alts;
  alts.add(PrimitiveType::createI32());
  alts.add(PrimitiveType::createNull());
  UnionType nullableI32(zc::mv(alts));

  ZC_EXPECT(null->isSubtypeOf(nullableI32));
}

ZC_TEST("PrimitiveType.NullIsNotSubtypeOfBareReference") {
  auto null = PrimitiveType::createNull();
  ReferenceType ref(PrimitiveType::createI32(), Mutability::Const);

  ZC_EXPECT(!null->isSubtypeOf(ref));
}

// ============================================================================
// PrimitiveType factory by kind
// ============================================================================

ZC_TEST("PrimitiveType.CreateByKindI32") {
  auto ty = PrimitiveType::create(PrimitiveKind::I32);
  ZC_EXPECT(ty->getPrimitiveKind() == PrimitiveKind::I32);
}

ZC_TEST("PrimitiveType.CreateByKindBool") {
  auto ty = PrimitiveType::create(PrimitiveKind::Bool);
  ZC_EXPECT(ty->getPrimitiveKind() == PrimitiveKind::Bool);
}

ZC_TEST("PrimitiveType.CreateByKindNever") {
  auto ty = PrimitiveType::create(PrimitiveKind::Never);
  ZC_EXPECT(ty->getPrimitiveKind() == PrimitiveKind::Never);
}

// ============================================================================
// PrimitiveType name lookup
// ============================================================================

ZC_TEST("PrimitiveType.FindByNameI32") {
  auto result = PrimitiveType::findByName("i32"_zc);
  ZC_EXPECT(result != zc::none);
  ZC_IF_SOME(kind, result) { ZC_EXPECT(kind == PrimitiveKind::I32); }
}

ZC_TEST("PrimitiveType.FindByNameBool") {
  auto result = PrimitiveType::findByName("bool"_zc);
  ZC_EXPECT(result != zc::none);
  ZC_IF_SOME(kind, result) { ZC_EXPECT(kind == PrimitiveKind::Bool); }
}

ZC_TEST("PrimitiveType.FindByNameStr") {
  auto result = PrimitiveType::findByName("str"_zc);
  ZC_EXPECT(result != zc::none);
  ZC_IF_SOME(kind, result) { ZC_EXPECT(kind == PrimitiveKind::Str); }
}

ZC_TEST("PrimitiveType.FindByNameInvalid") {
  auto result = PrimitiveType::findByName("nonexistent"_zc);
  ZC_EXPECT(result == zc::none);
}

// ============================================================================
// PrimitiveType byte size
// ============================================================================

ZC_TEST("PrimitiveType.ByteSizeI32") {
  auto ty = PrimitiveType::createI32();
  ZC_EXPECT(ty->getByteSize() == 4);
}

ZC_TEST("PrimitiveType.ByteSizeI8") {
  auto ty = PrimitiveType::createI8();
  ZC_EXPECT(ty->getByteSize() == 1);
}

ZC_TEST("PrimitiveType.ByteSizeF64") {
  auto ty = PrimitiveType::createF64();
  ZC_EXPECT(ty->getByteSize() == 8);
}

ZC_TEST("PrimitiveType.ByteSizeNever") {
  auto ty = PrimitiveType::createNever();
  ZC_EXPECT(ty->getByteSize() == 0);
}

ZC_TEST("PrimitiveType.ByteSizeUnit") {
  auto ty = PrimitiveType::createUnit();
  ZC_EXPECT(ty->getByteSize() == 0);
}

// ============================================================================
// FunctionType
// ============================================================================

ZC_TEST("FunctionType.CreateSimple") {
  zc::Vector<zc::Own<Type>> params;
  params.add(PrimitiveType::createI32());
  auto ret = PrimitiveType::createBool();
  FunctionType fnTy(zc::mv(params), zc::mv(ret));

  ZC_EXPECT(fnTy.getKind() == TypeKind::Function);
  ZC_EXPECT(isFunction(fnTy));
  ZC_EXPECT(fnTy.getParamCount() == 1);
  ZC_EXPECT(isPrimitive(fnTy.getParamType(0)));
  ZC_EXPECT(isPrimitive(fnTy.getReturnType()));
}

ZC_TEST("FunctionType.CreateNoParams") {
  zc::Vector<zc::Own<Type>> params;
  auto ret = PrimitiveType::createUnit();
  FunctionType fnTy(zc::mv(params), zc::mv(ret));

  ZC_EXPECT(fnTy.getParamCount() == 0);
  ZC_EXPECT(isUnit(fnTy.getReturnType()));
}

ZC_TEST("FunctionType.CreateMultipleParams") {
  zc::Vector<zc::Own<Type>> params;
  params.add(PrimitiveType::createI32());
  params.add(PrimitiveType::createStr());
  params.add(PrimitiveType::createBool());
  auto ret = PrimitiveType::createF64();
  FunctionType fnTy(zc::mv(params), zc::mv(ret));

  ZC_EXPECT(fnTy.getParamCount() == 3);
  ZC_EXPECT(fnTy.getParamType(0).getKind() == TypeKind::Primitive);
  ZC_EXPECT(fnTy.getParamType(1).getKind() == TypeKind::Primitive);
  ZC_EXPECT(fnTy.getParamType(2).getKind() == TypeKind::Primitive);
}

ZC_TEST("FunctionType.Equality") {
  zc::Vector<zc::Own<Type>> params1;
  params1.add(PrimitiveType::createI32());
  auto ret1 = PrimitiveType::createBool();
  FunctionType fn1(zc::mv(params1), zc::mv(ret1));

  zc::Vector<zc::Own<Type>> params2;
  params2.add(PrimitiveType::createI32());
  auto ret2 = PrimitiveType::createBool();
  FunctionType fn2(zc::mv(params2), zc::mv(ret2));

  ZC_EXPECT(fn1.equals(fn2));
}

ZC_TEST("FunctionType.EqualityDifferentParams") {
  zc::Vector<zc::Own<Type>> params1;
  params1.add(PrimitiveType::createI32());
  auto ret1 = PrimitiveType::createBool();
  FunctionType fn1(zc::mv(params1), zc::mv(ret1));

  zc::Vector<zc::Own<Type>> params2;
  params2.add(PrimitiveType::createStr());
  auto ret2 = PrimitiveType::createBool();
  FunctionType fn2(zc::mv(params2), zc::mv(ret2));

  ZC_EXPECT(!fn1.equals(fn2));
}

ZC_TEST("FunctionType.EqualityDifferentReturn") {
  zc::Vector<zc::Own<Type>> params1;
  params1.add(PrimitiveType::createI32());
  auto ret1 = PrimitiveType::createBool();
  FunctionType fn1(zc::mv(params1), zc::mv(ret1));

  zc::Vector<zc::Own<Type>> params2;
  params2.add(PrimitiveType::createI32());
  auto ret2 = PrimitiveType::createStr();
  FunctionType fn2(zc::mv(params2), zc::mv(ret2));

  ZC_EXPECT(!fn1.equals(fn2));
}

ZC_TEST("FunctionType.ToString") {
  zc::Vector<zc::Own<Type>> params;
  params.add(PrimitiveType::createI32());
  params.add(PrimitiveType::createStr());
  auto ret = PrimitiveType::createBool();
  FunctionType fnTy(zc::mv(params), zc::mv(ret));

  auto s = fnTy.toString();
  ZC_EXPECT(s.size() > 0);
}

ZC_TEST("FunctionType.VariadicFlag") {
  zc::Vector<zc::Own<Type>> params;
  auto ret = PrimitiveType::createUnit();
  FunctionType fnTy(zc::mv(params), zc::mv(ret));

  ZC_EXPECT(!fnTy.isVariadic());
  fnTy.setVariadic(true);
  ZC_EXPECT(fnTy.isVariadic());
  fnTy.setVariadic(false);
  ZC_EXPECT(!fnTy.isVariadic());
}

// ============================================================================
// TupleType
// ============================================================================

ZC_TEST("TupleType.CreateEmpty") {
  zc::Vector<zc::Own<Type>> elems;
  TupleType tuple(zc::mv(elems));

  ZC_EXPECT(tuple.getKind() == TypeKind::Tuple);
  ZC_EXPECT(isTuple(tuple));
  ZC_EXPECT(tuple.getElementCount() == 0);
  ZC_EXPECT(tuple.isEmpty());
}

ZC_TEST("TupleType.CreateSingle") {
  zc::Vector<zc::Own<Type>> elems;
  elems.add(PrimitiveType::createI32());
  TupleType tuple(zc::mv(elems));

  ZC_EXPECT(tuple.getElementCount() == 1);
  ZC_EXPECT(!tuple.isEmpty());
  ZC_EXPECT(isPrimitive(tuple.getElementType(0)));
}

ZC_TEST("TupleType.CreateMultiple") {
  zc::Vector<zc::Own<Type>> elems;
  elems.add(PrimitiveType::createI32());
  elems.add(PrimitiveType::createStr());
  elems.add(PrimitiveType::createBool());
  TupleType tuple(zc::mv(elems));

  ZC_EXPECT(tuple.getElementCount() == 3);
}

ZC_TEST("TupleType.Equality") {
  zc::Vector<zc::Own<Type>> elems1;
  elems1.add(PrimitiveType::createI32());
  elems1.add(PrimitiveType::createStr());
  TupleType t1(zc::mv(elems1));

  zc::Vector<zc::Own<Type>> elems2;
  elems2.add(PrimitiveType::createI32());
  elems2.add(PrimitiveType::createStr());
  TupleType t2(zc::mv(elems2));

  ZC_EXPECT(t1.equals(t2));
}

ZC_TEST("TupleType.EqualityDifferentLength") {
  zc::Vector<zc::Own<Type>> elems1;
  elems1.add(PrimitiveType::createI32());
  TupleType t1(zc::mv(elems1));

  zc::Vector<zc::Own<Type>> elems2;
  elems2.add(PrimitiveType::createI32());
  elems2.add(PrimitiveType::createStr());
  TupleType t2(zc::mv(elems2));

  ZC_EXPECT(!t1.equals(t2));
}

ZC_TEST("TupleType.ToString") {
  zc::Vector<zc::Own<Type>> elems;
  elems.add(PrimitiveType::createI32());
  elems.add(PrimitiveType::createStr());
  TupleType tuple(zc::mv(elems));

  auto s = tuple.toString();
  ZC_EXPECT(s.size() > 0);
}

// ============================================================================
// ArrayType
// ============================================================================

ZC_TEST("ArrayType.CreateI32Array") {
  ArrayType arr(PrimitiveType::createI32());
  ZC_EXPECT(arr.getKind() == TypeKind::Array);
  ZC_EXPECT(isArray(arr));
  ZC_EXPECT(isPrimitive(arr.getElementType()));
}

ZC_TEST("ArrayType.CreateStrArray") {
  ArrayType arr(PrimitiveType::createStr());
  ZC_EXPECT(arr.getElementType().getKind() == TypeKind::Primitive);
}

ZC_TEST("ArrayType.Equality") {
  ArrayType a1(PrimitiveType::createI32());
  ArrayType a2(PrimitiveType::createI32());
  ZC_EXPECT(a1.equals(a2));
}

ZC_TEST("ArrayType.EqualityDifferentElem") {
  ArrayType a1(PrimitiveType::createI32());
  ArrayType a2(PrimitiveType::createStr());
  ZC_EXPECT(!a1.equals(a2));
}

ZC_TEST("ArrayType.ToString") {
  ArrayType arr(PrimitiveType::createI32());
  auto s = arr.toString();
  ZC_EXPECT(s.size() > 0);
}

ZC_TEST("ObjectType.MemberLookupUsesStableNameContents") {
  ObjectType obj;
  obj.addMember("x"_zc, PrimitiveType::createI32());
  obj.addMember("name"_zc, PrimitiveType::createStr());

  ZC_EXPECT(obj.getMemberCount() == 2);
  auto x = obj.getMember("x"_zc);
  auto name = obj.getMember("name"_zc);
  ZC_EXPECT(x != zc::none);
  ZC_EXPECT(name != zc::none);
  ZC_IF_SOME(xTy, x) { ZC_EXPECT(isPrimitive(xTy)); }
  ZC_IF_SOME(nameTy, name) { ZC_EXPECT(isPrimitive(nameTy)); }
}

// ============================================================================
// NamedType
// ============================================================================

ZC_TEST("NamedType.CreateWithName") {
  NamedType ty("MyClass"_zc);
  ZC_EXPECT(ty.getKind() == TypeKind::Named);
  ZC_EXPECT(isNamed(ty));
  ZC_EXPECT(ty.getName() == "MyClass"_zc);
}

ZC_TEST("NamedType.EqualitySameName") {
  NamedType t1("Foo"_zc);
  NamedType t2("Foo"_zc);
  ZC_EXPECT(t1.equals(t2));
}

ZC_TEST("NamedType.EqualityDifferentName") {
  NamedType t1("Foo"_zc);
  NamedType t2("Bar"_zc);
  ZC_EXPECT(!t1.equals(t2));
}

ZC_TEST("NamedType.ToString") {
  NamedType ty("MyClass"_zc);
  auto s = ty.toString();
  ZC_EXPECT(s.contains("MyClass"));
}

ZC_TEST("NamedType.TypeArgsInitiallyEmpty") {
  NamedType ty("Vec"_zc);
  ZC_EXPECT(ty.getTypeArgCount() == 0);
}

ZC_TEST("NamedType.AddTypeArg") {
  NamedType ty("Vec"_zc);
  ty.addTypeArg(PrimitiveType::createI32());
  ZC_EXPECT(ty.getTypeArgCount() == 1);
  ZC_EXPECT(isPrimitive(ty.getTypeArg(0)));
}

ZC_TEST("NamedType.NameLookupUsesStableNameContents") {
  NamedType ty(zc::str("Number").asPtr());
  ZC_EXPECT(ty.getName() == "Number"_zc);
  auto rendered = ty.toString();
  ZC_EXPECT(rendered == "Number"_zc);
}

// ============================================================================
// TypeVar
// ============================================================================

ZC_TEST("TypeVar.CreateWithName") {
  TypeVar tv("T"_zc);
  ZC_EXPECT(tv.getKind() == TypeKind::TypeVar);
  ZC_EXPECT(isTypeVar(tv));
  ZC_EXPECT(tv.getName() == "T"_zc);
}

ZC_TEST("TypeVar.CreateWithId") {
  TypeVar tv("T"_zc, 42);
  ZC_EXPECT(tv.getName() == "T"_zc);
  ZC_EXPECT(tv.getId() == 42);
}

ZC_TEST("TypeVar.UniqueIds") {
  TypeVar t1("T"_zc, 1);
  TypeVar t2("T"_zc, 2);
  ZC_EXPECT(t1.getId() != t2.getId());
}

ZC_TEST("TypeVar.EqualitySameId") {
  TypeVar t1("T"_zc, 1);
  TypeVar t2("T"_zc, 1);
  ZC_EXPECT(t1.equals(t2));
}

ZC_TEST("TypeVar.EqualityDifferentId") {
  TypeVar t1("T"_zc, 1);
  TypeVar t2("T"_zc, 2);
  ZC_EXPECT(!t1.equals(t2));
}

ZC_TEST("TypeVar.ToString") {
  TypeVar tv("T"_zc);
  auto s = tv.toString();
  ZC_EXPECT(s.contains("T"));
}

ZC_TEST("TypeVar.AddUpperBound") {
  TypeVar tv("T"_zc);
  tv.addUpperBound(PrimitiveType::createI32());
  ZC_EXPECT(tv.getUpperBoundCount() == 1);
}

ZC_TEST("TypeVar.AddLowerBound") {
  TypeVar tv("T"_zc);
  tv.addLowerBound(PrimitiveType::createI32());
  ZC_EXPECT(tv.getLowerBoundCount() == 1);
}

// ============================================================================
// ReferenceType
// ============================================================================

ZC_TEST("ReferenceType.CreateConstRef") {
  ReferenceType ref(PrimitiveType::createI32(), Mutability::Const);
  ZC_EXPECT(ref.getKind() == TypeKind::Reference);
  ZC_EXPECT(isReference(ref));
  ZC_EXPECT(!ref.isMutable());
  ZC_EXPECT(ref.getMutability() == Mutability::Const);
}

ZC_TEST("ReferenceType.CreateMutableRef") {
  ReferenceType ref(PrimitiveType::createI32(), Mutability::Mutable);
  ZC_EXPECT(ref.isMutable());
  ZC_EXPECT(ref.getMutability() == Mutability::Mutable);
}

ZC_TEST("ReferenceType.PointeeType") {
  ReferenceType ref(PrimitiveType::createI32(), Mutability::Const);
  ZC_EXPECT(isPrimitive(ref.getPointeeType()));
}

ZC_TEST("ReferenceType.Equality") {
  ReferenceType r1(PrimitiveType::createI32(), Mutability::Const);
  ReferenceType r2(PrimitiveType::createI32(), Mutability::Const);
  ZC_EXPECT(r1.equals(r2));
}

ZC_TEST("ReferenceType.EqualityDifferentMutability") {
  ReferenceType r1(PrimitiveType::createI32(), Mutability::Const);
  ReferenceType r2(PrimitiveType::createI32(), Mutability::Mutable);
  ZC_EXPECT(!r1.equals(r2));
}

ZC_TEST("ReferenceType.MutableIsSubtypeOfConst") {
  ReferenceType mutRef(PrimitiveType::createI32(), Mutability::Mutable);
  ReferenceType constRef(PrimitiveType::createI32(), Mutability::Const);
  ZC_EXPECT(mutRef.isSubtypeOf(constRef));
}

ZC_TEST("ReferenceType.ToString") {
  ReferenceType ref(PrimitiveType::createI32(), Mutability::Const);
  auto s = ref.toString();
  ZC_EXPECT(s.size() > 0);
}

// ============================================================================
// RawPointerType
// ============================================================================

ZC_TEST("RawPointerType.CreateConst") {
  RawPointerType ptr(PrimitiveType::createI32(), Mutability::Const);
  ZC_EXPECT(ptr.getKind() == TypeKind::RawPointer);
  ZC_EXPECT(isRawPointer(ptr));
  ZC_EXPECT(!ptr.isMutable());
}

ZC_TEST("RawPointerType.CreateMutable") {
  RawPointerType ptr(PrimitiveType::createI32(), Mutability::Mutable);
  ZC_EXPECT(ptr.isMutable());
}

ZC_TEST("RawPointerType.Equality") {
  RawPointerType p1(PrimitiveType::createI32(), Mutability::Const);
  RawPointerType p2(PrimitiveType::createI32(), Mutability::Const);
  ZC_EXPECT(p1.equals(p2));
}

ZC_TEST("RawPointerType.MutableIsSubtypeOfConst") {
  RawPointerType mutPtr(PrimitiveType::createI32(), Mutability::Mutable);
  RawPointerType constPtr(PrimitiveType::createI32(), Mutability::Const);
  ZC_EXPECT(mutPtr.isSubtypeOf(constPtr));
}

// ============================================================================
// UnionType
// ============================================================================

ZC_TEST("UnionType.CreateTwoAlternatives") {
  zc::Vector<zc::Own<Type>> alts;
  alts.add(PrimitiveType::createI32());
  alts.add(PrimitiveType::createStr());
  UnionType unionTy(zc::mv(alts));

  ZC_EXPECT(unionTy.getKind() == TypeKind::Union);
  ZC_EXPECT(isUnion(unionTy));
  ZC_EXPECT(unionTy.getAlternativeCount() == 2);
}

ZC_TEST("UnionType.ContainsI32") {
  zc::Vector<zc::Own<Type>> alts;
  alts.add(PrimitiveType::createI32());
  alts.add(PrimitiveType::createStr());
  UnionType unionTy(zc::mv(alts));

  auto i32 = PrimitiveType::createI32();
  ZC_EXPECT(unionTy.contains(*i32));
}

ZC_TEST("UnionType.DoesNotContainBool") {
  zc::Vector<zc::Own<Type>> alts;
  alts.add(PrimitiveType::createI32());
  alts.add(PrimitiveType::createStr());
  UnionType unionTy(zc::mv(alts));

  auto boolTy = PrimitiveType::createBool();
  ZC_EXPECT(!unionTy.contains(*boolTy));
}

ZC_TEST("UnionType.IsNullable") {
  zc::Vector<zc::Own<Type>> alts;
  alts.add(PrimitiveType::createI32());
  alts.add(PrimitiveType::createNull());
  UnionType unionTy(zc::mv(alts));

  ZC_EXPECT(unionTy.isNullable());
}

ZC_TEST("UnionType.NotNullable") {
  zc::Vector<zc::Own<Type>> alts;
  alts.add(PrimitiveType::createI32());
  alts.add(PrimitiveType::createStr());
  UnionType unionTy(zc::mv(alts));

  ZC_EXPECT(!unionTy.isNullable());
}

ZC_TEST("UnionType.ToString") {
  zc::Vector<zc::Own<Type>> alts;
  alts.add(PrimitiveType::createI32());
  alts.add(PrimitiveType::createStr());
  UnionType unionTy(zc::mv(alts));

  auto s = unionTy.toString();
  ZC_EXPECT(s.size() > 0);
}

// ============================================================================
// InterfaceType
// ============================================================================

ZC_TEST("InterfaceType.CreateWithName") {
  InterfaceType iface("Drawable"_zc);
  ZC_EXPECT(iface.getKind() == TypeKind::Interface);
  ZC_EXPECT(isInterface(iface));
  ZC_EXPECT(iface.getName() == "Drawable"_zc);
}

ZC_TEST("InterfaceType.AddMethodAndParent") {
  InterfaceType iface("Child"_zc);
  iface.addMethod("draw"_zc,
                  zc::heap<FunctionType>(zc::Vector<zc::Own<Type>>(), PrimitiveType::createUnit()));
  iface.addParentInterface(zc::heap<InterfaceType>("Parent"_zc));

  ZC_EXPECT(iface.getMethodCount() == 1);
  ZC_EXPECT(iface.hasMethod("draw"_zc));
  ZC_EXPECT(iface.getParentInterfaceCount() == 1);
  ZC_EXPECT(isInterface(iface.getParentInterface(0)));
}

// ============================================================================
// IntersectionType
// ============================================================================

ZC_TEST("IntersectionType.CreateTwoConjuncts") {
  zc::Vector<zc::Own<Type>> conjuncts;
  conjuncts.add(zc::heap<InterfaceType>("Drawable"_zc));
  conjuncts.add(zc::heap<InterfaceType>("Serializable"_zc));
  IntersectionType inter(zc::mv(conjuncts));

  ZC_EXPECT(inter.getKind() == TypeKind::Intersection);
  ZC_EXPECT(isIntersection(inter));
  ZC_EXPECT(inter.getConjunctCount() == 2);
}

ZC_TEST("IntersectionType.Equality") {
  zc::Vector<zc::Own<Type>> left;
  left.add(zc::heap<InterfaceType>("Drawable"_zc));
  left.add(zc::heap<InterfaceType>("Serializable"_zc));
  IntersectionType a(zc::mv(left));

  zc::Vector<zc::Own<Type>> right;
  right.add(zc::heap<InterfaceType>("Drawable"_zc));
  right.add(zc::heap<InterfaceType>("Serializable"_zc));
  IntersectionType b(zc::mv(right));

  ZC_EXPECT(a.equals(b));
}

// ============================================================================
// ExistentialType
// ============================================================================

ZC_TEST("ExistentialType.CreateDynInterface") {
  ExistentialType dynDrawable(zc::heap<InterfaceType>("Drawable"_zc));

  ZC_EXPECT(dynDrawable.getKind() == TypeKind::Existential);
  ZC_EXPECT(isExistential(dynDrawable));
  ZC_EXPECT(isInterface(dynDrawable.getInterfaceType()));
}

ZC_TEST("ExistentialType.UpcastsThroughInterfaceParent") {
  auto child = zc::heap<InterfaceType>("Child"_zc);
  child->addParentInterface(zc::heap<InterfaceType>("Parent"_zc));
  ExistentialType childDyn(zc::mv(child));
  ExistentialType parentDyn(zc::heap<InterfaceType>("Parent"_zc));

  ZC_EXPECT(childDyn.isSubtypeOf(parentDyn));
}

ZC_TEST("ExistentialType.MarkerBoundsParticipateInIdentity") {
  zc::Vector<zc::StringPtr> sendable;
  sendable.add("Sendable"_zc);
  ExistentialType dynDrawable(zc::heap<InterfaceType>("Drawable"_zc));
  ExistentialType dynDrawableSendable(zc::heap<InterfaceType>("Drawable"_zc), sendable.asPtr());

  ZC_EXPECT(dynDrawableSendable.getMarkerCount() == 1);
  ZC_EXPECT(dynDrawableSendable.getMarkerName(0) == "Sendable"_zc);
  ZC_EXPECT(!dynDrawable.equals(dynDrawableSendable));
  ZC_EXPECT(dynDrawableSendable.isSubtypeOf(dynDrawable));
  ZC_EXPECT(!dynDrawable.isSubtypeOf(dynDrawableSendable));
}

ZC_TEST("ExistentialType.MarkerBoundsAreCanonicalized") {
  zc::Vector<zc::StringPtr> firstMarkers;
  firstMarkers.add("Sendable"_zc);
  firstMarkers.add("Shared"_zc);
  zc::Vector<zc::StringPtr> secondMarkers;
  secondMarkers.add("Shared"_zc);
  secondMarkers.add("Sendable"_zc);
  secondMarkers.add("Shared"_zc);
  ExistentialType first(zc::heap<InterfaceType>("Drawable"_zc), firstMarkers.asPtr());
  ExistentialType second(zc::heap<InterfaceType>("Drawable"_zc), secondMarkers.asPtr());
  ExistentialType sendableOnly(zc::heap<InterfaceType>("Drawable"_zc), firstMarkers.first(1));

  ZC_EXPECT(first.getMarkerCount() == 2);
  ZC_EXPECT(first.getMarkerName(0) == "Sendable"_zc);
  ZC_EXPECT(first.getMarkerName(1) == "Shared"_zc);
  ZC_EXPECT(first.equals(second));
  ZC_EXPECT(first.isSubtypeOf(second));
  ZC_EXPECT(second.isSubtypeOf(first));
  ZC_EXPECT(first.isSubtypeOf(sendableOnly));
  ZC_EXPECT(!sendableOnly.isSubtypeOf(first));
}

// ============================================================================
// AssociatedType
// ============================================================================

ZC_TEST("AssociatedType.CreateProjection") {
  AssociatedType item(zc::heap<NamedType>("Iterator"_zc), "Item"_zc);

  ZC_EXPECT(item.getKind() == TypeKind::Associated);
  ZC_EXPECT(isAssociated(item));
  ZC_EXPECT(item.getName() == "Item"_zc);
  ZC_EXPECT(isNamed(item.getParentType()));
}

ZC_TEST("AssociatedType.Equality") {
  AssociatedType a(zc::heap<NamedType>("Iterator"_zc), "Item"_zc);
  AssociatedType b(zc::heap<NamedType>("Iterator"_zc), "Item"_zc);

  ZC_EXPECT(a.equals(b));
}

// ============================================================================
// ErrorType
// ============================================================================

ZC_TEST("ErrorType.CreateDefault") {
  ErrorType err;
  ZC_EXPECT(err.getKind() == TypeKind::Error);
  ZC_EXPECT(isError(err));
}

ZC_TEST("ErrorType.CreateWithMessage") {
  ErrorType err("test error"_zc);
  ZC_EXPECT(isError(err));
  ZC_EXPECT(err.getMessage() == "test error"_zc);
}

ZC_TEST("ErrorType.UnifiesWithEverything") {
  ErrorType err;
  auto i32 = PrimitiveType::createI32();
  // Error type should be compatible with all types
  ZC_EXPECT(err.isSubtypeOf(*i32));
  ZC_EXPECT(i32->isSubtypeOf(err));
}

ZC_TEST("ErrorType.ToString") {
  ErrorType err;
  auto s = err.toString();
  ZC_EXPECT(s.size() > 0);
}

// ============================================================================
// Type classification helpers
// ============================================================================

ZC_TEST("Type.IsNumericI32") {
  auto ty = PrimitiveType::createI32();
  ZC_EXPECT(isNumeric(*ty));
}

ZC_TEST("Type.IsNumericF64") {
  auto ty = PrimitiveType::createF64();
  ZC_EXPECT(isNumeric(*ty));
}

ZC_TEST("Type.IsNumericBool") {
  auto ty = PrimitiveType::createBool();
  ZC_EXPECT(!isNumeric(*ty));
}

ZC_TEST("Type.IsStringStr") {
  auto ty = PrimitiveType::createStr();
  ZC_EXPECT(isString(*ty));
}

ZC_TEST("Type.IsStringI32") {
  auto ty = PrimitiveType::createI32();
  ZC_EXPECT(!isString(*ty));
}

ZC_TEST("Type.IsIntegerI32") {
  auto ty = PrimitiveType::createI32();
  ZC_EXPECT(isInteger(*ty));
}

ZC_TEST("Type.IsIntegerF64") {
  auto ty = PrimitiveType::createF64();
  ZC_EXPECT(!isInteger(*ty));
}

ZC_TEST("Type.IsSignedIntegerI32") {
  auto ty = PrimitiveType::createI32();
  ZC_EXPECT(isSignedInteger(*ty));
}

ZC_TEST("Type.IsSignedIntegerU32") {
  auto ty = PrimitiveType::createU32();
  ZC_EXPECT(!isSignedInteger(*ty));
}

ZC_TEST("Type.IsUnsignedIntegerU32") {
  auto ty = PrimitiveType::createU32();
  ZC_EXPECT(isUnsignedInteger(*ty));
}

ZC_TEST("Type.IsUnsignedIntegerI32") {
  auto ty = PrimitiveType::createI32();
  ZC_EXPECT(!isUnsignedInteger(*ty));
}

ZC_TEST("Type.IsFloatingPointF32") {
  auto ty = PrimitiveType::createF32();
  ZC_EXPECT(isFloatingPoint(*ty));
}

ZC_TEST("Type.IsFloatingPointI32") {
  auto ty = PrimitiveType::createI32();
  ZC_EXPECT(!isFloatingPoint(*ty));
}

// ============================================================================
// Type assignability
// ============================================================================

ZC_TEST("Type.AssignableSameType") {
  auto i32 = PrimitiveType::createI32();
  ZC_EXPECT(isAssignableTo(*i32, *i32));
}

ZC_TEST("Type.AssignableNeverToI32") {
  auto never = PrimitiveType::createNever();
  auto i32 = PrimitiveType::createI32();
  ZC_EXPECT(isAssignableTo(*never, *i32));
}

ZC_TEST("Type.AssignableI32ToAny") {
  auto i32 = PrimitiveType::createI32();
  auto any = PrimitiveType::createAny();
  ZC_EXPECT(isAssignableTo(*i32, *any));
}

ZC_TEST("NamedType.GenericArgumentsAreInvariant") {
  NamedType vecMutRef("Vec"_zc);
  vecMutRef.addTypeArg(zc::heap<ReferenceType>(PrimitiveType::createI32(), Mutability::Mutable));

  NamedType vecSharedRef("Vec"_zc);
  vecSharedRef.addTypeArg(zc::heap<ReferenceType>(PrimitiveType::createI32(), Mutability::Const));

  ZC_EXPECT(!vecMutRef.isSubtypeOf(vecSharedRef));
  ZC_EXPECT(!vecSharedRef.isSubtypeOf(vecMutRef));
}

ZC_TEST("TypeAlgebra.ClonePreservesCompositeStructure") {
  zc::Vector<zc::Own<Type>> params;
  params.add(zc::heap<ReferenceType>(PrimitiveType::createI32(), Mutability::Mutable));

  zc::Vector<zc::Own<Type>> returnAlts;
  returnAlts.add(PrimitiveType::createBool());
  returnAlts.add(zc::heap<NamedType>("ParseError"_zc));

  auto fn = zc::heap<FunctionType>(zc::mv(params), zc::heap<UnionType>(zc::mv(returnAlts)));
  auto generic = zc::heap<GenericParam>("T"_zc);
  generic->upperBounds.add(zc::heap<NamedType>("Display"_zc));
  fn->addGenericParam(zc::mv(generic));
  fn->setRaisesType(zc::heap<NamedType>("ParseError"_zc));

  zc::Vector<zc::Own<Type>> conjuncts;
  conjuncts.add(zc::heap<ExistentialType>(zc::heap<NamedType>("Drawable"_zc)));
  conjuncts.add(zc::heap<AssociatedType>(zc::heap<NamedType>("Iterator"_zc), "Item"_zc));

  zc::Vector<zc::Own<Type>> alts;
  alts.add(zc::mv(fn));
  alts.add(zc::heap<IntersectionType>(zc::mv(conjuncts)));

  UnionType original(zc::mv(alts));
  auto cloned = cloneType(original);

  ZC_EXPECT(cloned.get() != &original);
  ZC_EXPECT(cloned->equals(original));
  ZC_EXPECT(cloned->toString() == original.toString());
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
