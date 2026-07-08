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

#include "zomlang/compiler/type/type-interner.h"

#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/type/associated-type.h"
#include "zomlang/compiler/type/existential-type.h"
#include "zomlang/compiler/type/function-type.h"
#include "zomlang/compiler/type/interface-type.h"
#include "zomlang/compiler/type/intersection-type.h"
#include "zomlang/compiler/type/named-type.h"
#include "zomlang/compiler/type/object-type.h"
#include "zomlang/compiler/type/primitive-type.h"
#include "zomlang/compiler/type/type.h"
#include "zomlang/compiler/type/union-type.h"

namespace zomlang {
namespace compiler {
namespace type {

ZC_TEST("TypeInterner.SamePrimitiveGetsSameId") {
  TypeInterner interner;
  auto i32a = PrimitiveType::createI32();
  auto i32b = PrimitiveType::createI32();

  auto idA = interner.intern(*i32a);
  auto idB = interner.intern(*i32b);

  ZC_EXPECT(idA == idB);
  ZC_EXPECT(interner.size() == 1);
  ZC_EXPECT(interner.getCanonicalKey(idA) == "i32"_zc);
}

ZC_TEST("TypeInterner.UnionOrderDoesNotAffectId") {
  TypeInterner interner;

  zc::Vector<zc::Own<Type>> altsA;
  altsA.add(PrimitiveType::createI32());
  altsA.add(PrimitiveType::createStr());
  UnionType unionA(zc::mv(altsA));

  zc::Vector<zc::Own<Type>> altsB;
  altsB.add(PrimitiveType::createStr());
  altsB.add(PrimitiveType::createI32());
  UnionType unionB(zc::mv(altsB));

  ZC_EXPECT(interner.intern(unionA) == interner.intern(unionB));
  ZC_EXPECT(interner.size() == 1);
}

ZC_TEST("TypeInterner.UnionDeduplicatesAlternatives") {
  TypeInterner interner;

  zc::Vector<zc::Own<Type>> altsA;
  altsA.add(PrimitiveType::createI32());
  altsA.add(PrimitiveType::createI32());
  UnionType unionA(zc::mv(altsA));

  auto i32 = PrimitiveType::createI32();

  ZC_EXPECT(interner.intern(unionA) == interner.intern(*i32));
}

ZC_TEST("TypeInterner.UnionRemovesNever") {
  TypeInterner interner;

  zc::Vector<zc::Own<Type>> alts;
  alts.add(PrimitiveType::createI32());
  alts.add(PrimitiveType::createNever());
  UnionType unionTy(zc::mv(alts));

  auto i32 = PrimitiveType::createI32();

  ZC_EXPECT(interner.intern(unionTy) == interner.intern(*i32));
}

ZC_TEST("TypeInterner.IntersectionOrderDoesNotAffectId") {
  TypeInterner interner;

  zc::Vector<zc::Own<Type>> conjunctsA;
  conjunctsA.add(PrimitiveType::createI32());
  conjunctsA.add(PrimitiveType::createStr());
  IntersectionType intersectionA(zc::mv(conjunctsA));

  zc::Vector<zc::Own<Type>> conjunctsB;
  conjunctsB.add(PrimitiveType::createStr());
  conjunctsB.add(PrimitiveType::createI32());
  IntersectionType intersectionB(zc::mv(conjunctsB));

  ZC_EXPECT(interner.intern(intersectionA) == interner.intern(intersectionB));
  ZC_EXPECT(interner.size() == 1);
}

ZC_TEST("TypeInterner.IntersectionWithNeverBecomesNever") {
  TypeInterner interner;

  zc::Vector<zc::Own<Type>> conjuncts;
  conjuncts.add(PrimitiveType::createI32());
  conjuncts.add(PrimitiveType::createNever());
  IntersectionType intersectionTy(zc::mv(conjuncts));

  auto never = PrimitiveType::createNever();

  ZC_EXPECT(interner.intern(intersectionTy) == interner.intern(*never));
}

ZC_TEST("TypeInterner.ObjectMemberOrderDoesNotAffectId") {
  TypeInterner interner;

  ObjectType objectA;
  objectA.addMember("x"_zc, PrimitiveType::createI32());
  objectA.addMember("name"_zc, PrimitiveType::createStr());

  ObjectType objectB;
  objectB.addMember("name"_zc, PrimitiveType::createStr());
  objectB.addMember("x"_zc, PrimitiveType::createI32());

  ZC_EXPECT(interner.intern(objectA) == interner.intern(objectB));
  ZC_EXPECT(interner.size() == 1);
}

ZC_TEST("TypeInterner.FunctionRaisesUnionIsCanonical") {
  TypeInterner interner;

  zc::Vector<zc::Own<Type>> paramsA;
  auto fnA = zc::heap<FunctionType>(zc::mv(paramsA), PrimitiveType::createI32());
  zc::Vector<zc::Own<Type>> raisesA;
  raisesA.add(PrimitiveType::createStr());
  raisesA.add(PrimitiveType::createNull());
  fnA->setRaisesType(zc::heap<UnionType>(zc::mv(raisesA)));

  zc::Vector<zc::Own<Type>> paramsB;
  auto fnB = zc::heap<FunctionType>(zc::mv(paramsB), PrimitiveType::createI32());
  zc::Vector<zc::Own<Type>> raisesB;
  raisesB.add(PrimitiveType::createNull());
  raisesB.add(PrimitiveType::createStr());
  fnB->setRaisesType(zc::heap<UnionType>(zc::mv(raisesB)));

  ZC_EXPECT(interner.intern(*fnA) == interner.intern(*fnB));
  ZC_EXPECT(interner.size() == 1);
}

ZC_TEST("TypeInterner.InterfaceGetsStableId") {
  TypeInterner interner;
  InterfaceType first("Drawable"_zc);
  InterfaceType second("Drawable"_zc);

  ZC_EXPECT(interner.intern(first) == interner.intern(second));
}

ZC_TEST("TypeInterner.ExistentialGetsStableId") {
  TypeInterner interner;
  ExistentialType first(zc::heap<InterfaceType>("Drawable"_zc));
  ExistentialType second(zc::heap<InterfaceType>("Drawable"_zc));

  ZC_EXPECT(interner.intern(first) == interner.intern(second));
}

ZC_TEST("TypeInterner.ExistentialMarkersAffectStableId") {
  TypeInterner interner;
  zc::Vector<zc::StringPtr> markers;
  markers.add("Sendable"_zc);
  ExistentialType plain(zc::heap<InterfaceType>("Drawable"_zc));
  ExistentialType marked(zc::heap<InterfaceType>("Drawable"_zc), markers.asPtr());

  ZC_EXPECT(interner.intern(plain) != interner.intern(marked));
}

ZC_TEST("TypeInterner.ExistentialMarkerOrderDoesNotAffectStableId") {
  TypeInterner interner;
  zc::Vector<zc::StringPtr> firstMarkers;
  firstMarkers.add("Sendable"_zc);
  firstMarkers.add("Shared"_zc);
  zc::Vector<zc::StringPtr> secondMarkers;
  secondMarkers.add("Shared"_zc);
  secondMarkers.add("Sendable"_zc);
  secondMarkers.add("Shared"_zc);
  ExistentialType first(zc::heap<InterfaceType>("Drawable"_zc), firstMarkers.asPtr());
  ExistentialType second(zc::heap<InterfaceType>("Drawable"_zc), secondMarkers.asPtr());

  ZC_EXPECT(interner.intern(first) == interner.intern(second));
}

ZC_TEST("TypeInterner.AssociatedTypeGetsStableId") {
  TypeInterner interner;
  AssociatedType first(zc::heap<NamedType>("Iterator"_zc), "Item"_zc);
  AssociatedType second(zc::heap<NamedType>("Iterator"_zc), "Item"_zc);

  ZC_EXPECT(interner.intern(first) == interner.intern(second));
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
