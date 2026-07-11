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

#include "zomlang/compiler/type/coercion.h"

#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/type/existential-type.h"
#include "zomlang/compiler/type/interface-type.h"
#include "zomlang/compiler/type/primitive-type.h"
#include "zomlang/compiler/type/raw-pointer-type.h"
#include "zomlang/compiler/type/reference-type.h"
#include "zomlang/compiler/type/type.h"
#include "zomlang/compiler/type/union-type.h"

namespace zomlang {
namespace compiler {
namespace type {

ZC_TEST("Coercion.Identity") {
  CoercionResolver resolver;
  auto i32 = PrimitiveType::createI32();

  auto result = resolver.check(*i32, *i32);
  ZC_EXPECT(result.success);
  ZC_EXPECT(result.kind == CoercionKind::Identity);
}

ZC_TEST("Coercion.NeverToAnyType") {
  CoercionResolver resolver;
  auto never = PrimitiveType::createNever();
  auto i32 = PrimitiveType::createI32();

  auto result = resolver.check(*never, *i32);
  ZC_EXPECT(result.success);
  ZC_EXPECT(result.kind == CoercionKind::NeverToAny);
}

ZC_TEST("Coercion.ConcreteToAny") {
  CoercionResolver resolver;
  auto i32 = PrimitiveType::createI32();
  auto any = PrimitiveType::createAny();

  auto result = resolver.check(*i32, *any);
  ZC_EXPECT(result.success);
  ZC_EXPECT(result.kind == CoercionKind::ToAny);
}

ZC_TEST("Coercion.MutableRefToSharedRef") {
  CoercionResolver resolver;
  ReferenceType mutRef(PrimitiveType::createI32(), Mutability::Mutable);
  ReferenceType sharedRef(PrimitiveType::createI32(), Mutability::Const);

  auto result = resolver.check(mutRef, sharedRef);
  ZC_EXPECT(result.success);
  ZC_EXPECT(result.kind == CoercionKind::MutRefToSharedRef);
}

ZC_TEST("Coercion.SharedRefToConstRawPointer") {
  CoercionResolver resolver;
  ReferenceType sharedRef(PrimitiveType::createI32(), Mutability::Const);
  RawPointerType constPtr(PrimitiveType::createI32(), Mutability::Const);

  auto result = resolver.check(sharedRef, constPtr);
  ZC_EXPECT(result.success);
  ZC_EXPECT(result.kind == CoercionKind::SharedRefToConstRaw);
}

ZC_TEST("Coercion.MutableRefToMutableRawPointer") {
  CoercionResolver resolver;
  ReferenceType mutRef(PrimitiveType::createI32(), Mutability::Mutable);
  RawPointerType mutPtr(PrimitiveType::createI32(), Mutability::Mutable);

  auto result = resolver.check(mutRef, mutPtr);
  ZC_EXPECT(result.success);
  ZC_EXPECT(result.kind == CoercionKind::MutRefToMutRaw);
}

ZC_TEST("Coercion.SharedRefDoesNotCoerceToMutableRawPointer") {
  CoercionResolver resolver;
  ReferenceType sharedRef(PrimitiveType::createI32(), Mutability::Const);
  RawPointerType mutPtr(PrimitiveType::createI32(), Mutability::Mutable);

  ZC_EXPECT(!resolver.canCoerce(sharedRef, mutPtr));
}

ZC_TEST("Coercion.SharedRefDoesNotCoerceToMutableRef") {
  CoercionResolver resolver;
  ReferenceType sharedRef(PrimitiveType::createI32(), Mutability::Const);
  ReferenceType mutRef(PrimitiveType::createI32(), Mutability::Mutable);

  ZC_EXPECT(!resolver.canCoerce(sharedRef, mutRef));
}

ZC_TEST("Coercion.MutableRawToConstRaw") {
  CoercionResolver resolver;
  RawPointerType mutPtr(PrimitiveType::createI32(), Mutability::Mutable);
  RawPointerType constPtr(PrimitiveType::createI32(), Mutability::Const);

  auto result = resolver.check(mutPtr, constPtr);
  ZC_EXPECT(result.success);
  ZC_EXPECT(result.kind == CoercionKind::MutRawToConstRaw);
}

ZC_TEST("Coercion.ConstRawDoesNotCoerceToMutableRaw") {
  CoercionResolver resolver;
  RawPointerType constPtr(PrimitiveType::createI32(), Mutability::Const);
  RawPointerType mutPtr(PrimitiveType::createI32(), Mutability::Mutable);

  ZC_EXPECT(!resolver.canCoerce(constPtr, mutPtr));
}

ZC_TEST("Coercion.ConcreteToUnion") {
  CoercionResolver resolver;
  zc::Vector<zc::Own<Type>> alts;
  alts.add(PrimitiveType::createI32());
  alts.add(PrimitiveType::createStr());
  UnionType unionTy(zc::mv(alts));
  auto i32 = PrimitiveType::createI32();

  auto result = resolver.check(*i32, unionTy);
  ZC_EXPECT(result.success);
  ZC_EXPECT(result.kind == CoercionKind::UnionInjection);
}

ZC_TEST("Coercion.NullToNullableUnion") {
  CoercionResolver resolver;
  zc::Vector<zc::Own<Type>> alts;
  alts.add(PrimitiveType::createI32());
  alts.add(PrimitiveType::createNull());
  UnionType nullableI32(zc::mv(alts));
  auto nullTy = PrimitiveType::createNull();

  auto result = resolver.check(*nullTy, nullableI32);
  ZC_EXPECT(result.success);
  ZC_EXPECT(result.kind == CoercionKind::NullToNullableUnion);
}

ZC_TEST("Coercion.NullDoesNotCoerceToBareReference") {
  CoercionResolver resolver;
  auto nullTy = PrimitiveType::createNull();
  ReferenceType ref(PrimitiveType::createI32(), Mutability::Const);

  ZC_EXPECT(!resolver.canCoerce(*nullTy, ref));
}

ZC_TEST("Coercion.UnrelatedTypesFail") {
  CoercionResolver resolver;
  auto i32 = PrimitiveType::createI32();
  auto str = PrimitiveType::createStr();

  ZC_EXPECT(!resolver.canCoerce(*i32, *str));
}

ZC_TEST("Coercion.DynInterfaceUpcast") {
  CoercionResolver resolver;
  auto childIface = zc::heap<InterfaceType>("Child"_zc);
  childIface->addParentInterface(zc::heap<InterfaceType>("Parent"_zc));

  ExistentialType childDyn(zc::mv(childIface));
  ExistentialType parentDyn(zc::heap<InterfaceType>("Parent"_zc));

  auto result = resolver.check(childDyn, parentDyn);
  ZC_EXPECT(result.success);
  ZC_EXPECT(result.kind == CoercionKind::DynUpcast);
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
