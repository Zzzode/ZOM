// Copyright (c) 2026 Zode.Z. All rights reserved
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

#include "zomlang/compiler/type/semantic-type-store.h"

#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/type/named-type.h"
#include "zomlang/compiler/type/primitive-type.h"
#include "zomlang/compiler/type/reference-type.h"
#include "zomlang/compiler/type/tuple-type.h"
#include "zomlang/compiler/type/union-type.h"

namespace zomlang::compiler::type {
namespace {

struct StoreFixture final {
  StoreFixture() {
    auto issuedContext = factory.issue();
    ZC_REQUIRE(issuedContext != zc::none);
    ZC_IF_SOME(value, issuedContext) { context = value; }
    auto issuedToken = factory.issueSemanticTypeStoreConstructionToken(context);
    ZC_REQUIRE(issuedToken != zc::none);
    ZC_IF_SOME(value, issuedToken) { store = zc::heap<SemanticTypeStore>(zc::mv(value)); }
  }

  identity::SemanticContextFactory factory;
  identity::SemanticContextBrand context;
  zc::Own<SemanticTypeStore> store;
};

}  // namespace

ZC_TEST("SemanticTypeStore.StablePrimitiveIdentity") {
  StoreFixture fixture;
  auto first = PrimitiveType::createI32();
  auto second = PrimitiveType::createI32();

  const auto firstId = fixture.store->intern(*first);
  const auto secondId = fixture.store->intern(*second);

  ZC_EXPECT(firstId == secondId);
  ZC_EXPECT(firstId.belongsTo(fixture.context));
  ZC_EXPECT(fixture.store->contains(firstId));
  ZC_EXPECT(fixture.store->getCanonicalKey(firstId) == "i32"_zc);
  ZC_EXPECT(fixture.store->size() == 1);
}

ZC_TEST("SemanticTypeStore.CanonicalCompositeForms") {
  StoreFixture fixture;

  zc::Vector<zc::Own<Type>> tupleElements;
  tupleElements.add(PrimitiveType::createI32());
  tupleElements.add(PrimitiveType::createStr());
  TupleType tuple(zc::mv(tupleElements));
  const auto tupleId = fixture.store->intern(tuple);

  ReferenceType reference(PrimitiveType::createI32(), Mutability::Const);
  const auto referenceId = fixture.store->intern(reference);

  NamedType nominal("Widget"_zc);
  const auto nominalId = fixture.store->intern(nominal);

  zc::Vector<zc::Own<Type>> firstAlternatives;
  firstAlternatives.add(PrimitiveType::createI32());
  firstAlternatives.add(PrimitiveType::createStr());
  UnionType firstUnion(zc::mv(firstAlternatives));
  zc::Vector<zc::Own<Type>> secondAlternatives;
  secondAlternatives.add(PrimitiveType::createStr());
  secondAlternatives.add(PrimitiveType::createI32());
  UnionType secondUnion(zc::mv(secondAlternatives));

  ZC_EXPECT(fixture.store->getCanonicalKey(tupleId) == "tuple(i32, str)"_zc);
  ZC_EXPECT(fixture.store->getCanonicalKey(referenceId) == "ref(const, i32)"_zc);
  ZC_EXPECT(fixture.store->getCanonicalKey(nominalId) == "named(Widget)"_zc);
  ZC_EXPECT(fixture.store->intern(firstUnion) == fixture.store->intern(secondUnion));
}

ZC_TEST("SemanticTypeStore.RejectsForeignContextIdentity") {
  StoreFixture first;
  StoreFixture second;
  auto type = PrimitiveType::createI32();

  const auto firstId = first.store->intern(*type);
  const auto secondId = second.store->intern(*type);

  ZC_EXPECT(firstId != secondId);
  ZC_EXPECT(!first.store->contains(secondId));
  ZC_EXPECT(first.store->get(secondId) == zc::none);
}

ZC_TEST("SemanticTypeStore.TokenIsSingleUsePerContext") {
  identity::SemanticContextFactory factory;
  auto issuedContext = factory.issue();
  ZC_REQUIRE(issuedContext != zc::none);
  ZC_IF_SOME(context, issuedContext) {
    auto first = factory.issueSemanticTypeStoreConstructionToken(context);
    ZC_EXPECT(first != zc::none);
    ZC_EXPECT(factory.issueSemanticTypeStoreConstructionToken(context) == zc::none);
  }
}

}  // namespace zomlang::compiler::type
