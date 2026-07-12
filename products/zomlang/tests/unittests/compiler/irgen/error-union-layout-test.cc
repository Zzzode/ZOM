// Copyright (c) 2025 Zode.Z. All rights reserved
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

#include "zomlang/compiler/irgen/error-union-layout.h"

#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/irgen/target-data-layout.h"
#include "zomlang/compiler/type/named-type.h"
#include "zomlang/compiler/type/object-type.h"
#include "zomlang/compiler/type/primitive-type.h"
#include "zomlang/compiler/type/raw-pointer-type.h"
#include "zomlang/compiler/type/reference-type.h"
#include "zomlang/compiler/type/semantic-type-store.h"
#include "zomlang/compiler/type/tuple-type.h"
#include "zomlang/compiler/type/type.h"
#include "zomlang/compiler/type/union-type.h"
#include "zomlang/tests/unittests/compiler/test-semantic-type-context.h"

namespace zomlang {
namespace compiler {
namespace irgen {

ZC_TEST("TargetDataLayout.ExposesExplicitIlp32AndLp64PointerLayouts") {
  const auto ilp32 = TargetDataLayout::ilp32();
  const auto lp64 = TargetDataLayout::lp64();

  ZC_EXPECT(ilp32.getPointerSize() == 4);
  ZC_EXPECT(ilp32.getPointerAlignment() == 4);
  ZC_EXPECT(lp64.getPointerSize() == 8);
  ZC_EXPECT(lp64.getPointerAlignment() == 8);
  ZC_EXPECT(ilp32.getIntegerLayout(IntegerScalarWidth::W64).size == 8);
  ZC_EXPECT(ilp32.getIntegerLayout(IntegerScalarWidth::W64).align == 8);
  ZC_EXPECT(lp64.getFloatLayout(FloatScalarWidth::W32).size == 4);
  ZC_EXPECT(lp64.getBoolLayout().align == 1);
  ZC_EXPECT(lp64.getCharLayout().size == 4);
}

ZC_TEST("ErrorUnionLayout.CanonicalizesNestedUnionPermutationTags") {
  tests::TestSemanticTypeContext semanticContext;
  auto& semanticTypes = semanticContext.semanticTypes();

  zc::Vector<zc::Own<type::Type>> firstNestedAlternatives;
  firstNestedAlternatives.add(zc::heap<type::NamedType>("NetworkError"_zc));
  firstNestedAlternatives.add(zc::heap<type::NamedType>("IoError"_zc));
  zc::Vector<zc::Own<type::Type>> firstAlternatives;
  firstAlternatives.add(zc::heap<type::NamedType>("ParseError"_zc));
  firstAlternatives.add(type::PrimitiveType::createI32());
  firstAlternatives.add(zc::heap<type::UnionType>(zc::mv(firstNestedAlternatives)));
  type::UnionType firstUnion(zc::mv(firstAlternatives));

  zc::Vector<zc::Own<type::Type>> secondNestedAlternatives;
  secondNestedAlternatives.add(zc::heap<type::NamedType>("ParseError"_zc));
  secondNestedAlternatives.add(zc::heap<type::NamedType>("IoError"_zc));
  zc::Vector<zc::Own<type::Type>> secondAlternatives;
  secondAlternatives.add(zc::heap<type::NamedType>("NetworkError"_zc));
  secondAlternatives.add(zc::heap<type::UnionType>(zc::mv(secondNestedAlternatives)));
  secondAlternatives.add(type::PrimitiveType::createI32());
  secondAlternatives.add(zc::heap<type::NamedType>("IoError"_zc));
  type::UnionType secondUnion(zc::mv(secondAlternatives));

  auto successType = type::PrimitiveType::createI32();
  auto first =
      computeErrorUnionLayout(semanticTypes, TargetDataLayout::lp64(), firstUnion, *successType);
  auto second =
      computeErrorUnionLayout(semanticTypes, TargetDataLayout::lp64(), secondUnion, *successType);

  ZC_EXPECT(first.semanticTypeId == second.semanticTypeId);
  ZC_EXPECT(first.kind == ErrorUnionLayoutKind::TaggedUnion);
  ZC_EXPECT(first.alternatives.size() == 4);
  ZC_EXPECT(second.alternatives.size() == first.alternatives.size());
  ZC_EXPECT(first.alternatives[0].tag == 0);
  ZC_EXPECT(first.alternatives[0].kind == ErrorUnionAlternativeKind::Success);
  ZC_EXPECT(semanticTypes.getCanonicalKey(first.alternatives[0].semanticTypeId) == "i32"_zc);
  ZC_EXPECT(semanticTypes.getCanonicalKey(first.alternatives[1].semanticTypeId) ==
            "named(IoError)"_zc);
  ZC_EXPECT(semanticTypes.getCanonicalKey(first.alternatives[2].semanticTypeId) ==
            "named(NetworkError)"_zc);
  ZC_EXPECT(semanticTypes.getCanonicalKey(first.alternatives[3].semanticTypeId) ==
            "named(ParseError)"_zc);
  for (size_t i = 0; i < first.alternatives.size(); ++i) {
    ZC_EXPECT(first.alternatives[i].tag == second.alternatives[i].tag);
    ZC_EXPECT(semanticTypes.getCanonicalKey(first.alternatives[i].semanticTypeId) ==
              semanticTypes.getCanonicalKey(second.alternatives[i].semanticTypeId));
  }
}

ZC_TEST("ErrorUnionLayout.IgnoresStoreInsertionHistoryWhenAssigningTags") {
  auto makeUnion = []() {
    zc::Vector<zc::Own<type::Type>> alternatives;
    alternatives.add(zc::heap<type::NamedType>("ParseError"_zc));
    alternatives.add(type::PrimitiveType::createI32());
    alternatives.add(zc::heap<type::NamedType>("IoError"_zc));
    return type::UnionType(zc::mv(alternatives));
  };

  tests::TestSemanticTypeContext firstContext;
  tests::TestSemanticTypeContext secondContext;
  auto& firstSemanticTypes = firstContext.semanticTypes();
  auto& secondSemanticTypes = secondContext.semanticTypes();
  auto firstNoise = zc::heap<type::NamedType>("NoiseA"_zc);
  auto secondNoise = zc::heap<type::NamedType>("NoiseB"_zc);
  (void)firstSemanticTypes.intern(*firstNoise);
  (void)firstSemanticTypes.intern(*secondNoise);
  (void)secondSemanticTypes.intern(*secondNoise);

  auto firstUnion = makeUnion();
  auto secondUnion = makeUnion();
  auto firstSuccess = type::PrimitiveType::createI32();
  auto secondSuccess = type::PrimitiveType::createI32();
  auto first = computeErrorUnionLayout(firstSemanticTypes, TargetDataLayout::lp64(), firstUnion,
                                       *firstSuccess);
  auto second = computeErrorUnionLayout(secondSemanticTypes, TargetDataLayout::lp64(), secondUnion,
                                        *secondSuccess);

  ZC_ASSERT(first.alternatives.size() == second.alternatives.size());
  for (size_t i = 0; i < first.alternatives.size(); ++i) {
    ZC_EXPECT(first.alternatives[i].tag == second.alternatives[i].tag);
    ZC_EXPECT(firstSemanticTypes.getCanonicalKey(first.alternatives[i].semanticTypeId) ==
              secondSemanticTypes.getCanonicalKey(second.alternatives[i].semanticTypeId));
  }
}

ZC_TEST("ErrorUnionLayout.ComputesScalarPayloadLayout") {
  tests::TestSemanticTypeContext semanticContext;
  auto& semanticTypes = semanticContext.semanticTypes();
  zc::Vector<zc::Own<type::Type>> alternatives;
  alternatives.add(type::PrimitiveType::createUnit());
  alternatives.add(type::PrimitiveType::createI64());
  type::UnionType unionType(zc::mv(alternatives));
  auto successType = type::PrimitiveType::createUnit();

  auto layout =
      computeErrorUnionLayout(semanticTypes, TargetDataLayout::ilp32(), unionType, *successType);

  ZC_EXPECT(layout.kind == ErrorUnionLayoutKind::TaggedUnion);
  ZC_EXPECT(layout.payloadLayoutState == ErrorUnionPayloadLayoutState::Known);
  ZC_EXPECT(layout.payloadOffset == 8);
  ZC_EXPECT(layout.payloadSize == 8);
  ZC_EXPECT(layout.payloadAlign == 8);
  ZC_EXPECT(layout.size == 16);
  ZC_EXPECT(layout.align == 8);
  ZC_EXPECT(layout.alternatives[1].payloadSize == 8);
  ZC_EXPECT(layout.alternatives[1].payloadAlign == 8);
}

ZC_TEST("ErrorUnionLayout.UsesFourByteUnicodeScalarLayout") {
  tests::TestSemanticTypeContext semanticContext;
  auto& semanticTypes = semanticContext.semanticTypes();
  zc::Vector<zc::Own<type::Type>> alternatives;
  alternatives.add(type::PrimitiveType::createUnit());
  alternatives.add(type::PrimitiveType::createChar());
  type::UnionType unionType(zc::mv(alternatives));
  auto successType = type::PrimitiveType::createUnit();

  auto layout =
      computeErrorUnionLayout(semanticTypes, TargetDataLayout::ilp32(), unionType, *successType);

  ZC_EXPECT(layout.payloadLayoutState == ErrorUnionPayloadLayoutState::Known);
  ZC_EXPECT(layout.payloadOffset == 4);
  ZC_EXPECT(layout.payloadSize == 4);
  ZC_EXPECT(layout.payloadAlign == 4);
  ZC_EXPECT(layout.size == 8);
  ZC_EXPECT(layout.align == 4);
  ZC_EXPECT(layout.alternatives[1].payloadSize == 4);
  ZC_EXPECT(layout.alternatives[1].payloadAlign == 4);
}

ZC_TEST("ErrorUnionLayout.ComputesTuplePayloadLayout") {
  tests::TestSemanticTypeContext semanticContext;
  auto& semanticTypes = semanticContext.semanticTypes();
  zc::Vector<zc::Own<type::Type>> tupleElements;
  tupleElements.add(type::PrimitiveType::createI8());
  tupleElements.add(type::PrimitiveType::createI32());

  zc::Vector<zc::Own<type::Type>> alternatives;
  alternatives.add(type::PrimitiveType::createUnit());
  alternatives.add(zc::heap<type::TupleType>(zc::mv(tupleElements)));
  type::UnionType unionType(zc::mv(alternatives));
  auto successType = type::PrimitiveType::createUnit();

  auto layout =
      computeErrorUnionLayout(semanticTypes, TargetDataLayout::lp64(), unionType, *successType);

  ZC_EXPECT(layout.payloadLayoutState == ErrorUnionPayloadLayoutState::Known);
  ZC_EXPECT(layout.payloadOffset == 4);
  ZC_EXPECT(layout.payloadSize == 8);
  ZC_EXPECT(layout.payloadAlign == 4);
  ZC_EXPECT(layout.size == 12);
  ZC_EXPECT(layout.align == 4);
  ZC_EXPECT(layout.alternatives[1].payloadSize == 8);
  ZC_EXPECT(layout.alternatives[1].payloadAlign == 4);
}

ZC_TEST("ErrorUnionLayout.ComputesCanonicalObjectPayloadLayout") {
  tests::TestSemanticTypeContext semanticContext;
  auto& semanticTypes = semanticContext.semanticTypes();
  auto objectError = zc::heap<type::ObjectType>();
  objectError->addMember("c"_zc, type::PrimitiveType::createI8());
  objectError->addMember("b"_zc, type::PrimitiveType::createI32());
  objectError->addMember("a"_zc, type::PrimitiveType::createI8());

  zc::Vector<zc::Own<type::Type>> alternatives;
  alternatives.add(type::PrimitiveType::createUnit());
  alternatives.add(zc::mv(objectError));
  type::UnionType unionType(zc::mv(alternatives));
  auto successType = type::PrimitiveType::createUnit();

  auto layout =
      computeErrorUnionLayout(semanticTypes, TargetDataLayout::lp64(), unionType, *successType);

  ZC_EXPECT(layout.payloadLayoutState == ErrorUnionPayloadLayoutState::Known);
  ZC_EXPECT(layout.payloadOffset == 4);
  ZC_EXPECT(layout.payloadSize == 12);
  ZC_EXPECT(layout.payloadAlign == 4);
  ZC_EXPECT(layout.size == 16);
  ZC_EXPECT(layout.align == 4);
  ZC_EXPECT(layout.alternatives[1].payloadSize == 12);
  ZC_EXPECT(layout.alternatives[1].payloadAlign == 4);
}

ZC_TEST("ErrorUnionLayout.UsesExplicitTargetPointerSizeAndAlignment") {
  tests::TestSemanticTypeContext semanticContext;
  auto& semanticTypes = semanticContext.semanticTypes();
  zc::Vector<zc::Own<type::Type>> alternatives;
  alternatives.add(type::PrimitiveType::createUnit());
  alternatives.add(
      zc::heap<type::RawPointerType>(type::PrimitiveType::createI32(), type::Mutability::Const));
  alternatives.add(
      zc::heap<type::ReferenceType>(type::PrimitiveType::createI32(), type::Mutability::Mutable));
  type::UnionType unionType(zc::mv(alternatives));
  auto successType = type::PrimitiveType::createUnit();

  auto ilp32 =
      computeErrorUnionLayout(semanticTypes, TargetDataLayout::ilp32(), unionType, *successType);
  auto lp64 =
      computeErrorUnionLayout(semanticTypes, TargetDataLayout::lp64(), unionType, *successType);

  ZC_EXPECT(ilp32.payloadOffset == 4);
  ZC_EXPECT(ilp32.payloadSize == 4);
  ZC_EXPECT(ilp32.payloadAlign == 4);
  ZC_EXPECT(ilp32.size == 8);
  ZC_EXPECT(ilp32.align == 4);
  ZC_EXPECT(lp64.payloadOffset == 8);
  ZC_EXPECT(lp64.payloadSize == 8);
  ZC_EXPECT(lp64.payloadAlign == 8);
  ZC_EXPECT(lp64.size == 16);
  ZC_EXPECT(lp64.align == 8);
  for (size_t i = 1; i < ilp32.alternatives.size(); ++i) {
    ZC_EXPECT(ilp32.alternatives[i].payloadSize == 4);
    ZC_EXPECT(ilp32.alternatives[i].payloadAlign == 4);
    ZC_EXPECT(lp64.alternatives[i].payloadSize == 8);
    ZC_EXPECT(lp64.alternatives[i].payloadAlign == 8);
  }
}

ZC_TEST("ErrorUnionLayout.PreservesZeroSizedPayloads") {
  tests::TestSemanticTypeContext semanticContext;
  auto& semanticTypes = semanticContext.semanticTypes();
  zc::Vector<zc::Own<type::Type>> alternatives;
  alternatives.add(type::PrimitiveType::createUnit());
  alternatives.add(type::PrimitiveType::createNull());
  type::UnionType unionType(zc::mv(alternatives));
  auto successType = type::PrimitiveType::createUnit();

  auto layout =
      computeErrorUnionLayout(semanticTypes, TargetDataLayout::lp64(), unionType, *successType);

  ZC_EXPECT(layout.kind == ErrorUnionLayoutKind::TaggedUnion);
  ZC_EXPECT(layout.payloadLayoutState == ErrorUnionPayloadLayoutState::Known);
  ZC_EXPECT(layout.payloadOffset == 1);
  ZC_EXPECT(layout.payloadSize == 0);
  ZC_EXPECT(layout.payloadAlign == 1);
  ZC_EXPECT(layout.size == 1);
  ZC_EXPECT(layout.align == 1);
  ZC_EXPECT(layout.alternatives[0].payloadSize == 0);
  ZC_EXPECT(layout.alternatives[1].payloadSize == 0);
}

ZC_TEST("ErrorUnionLayout.UsesDirectSuccessWithoutErrorAlternatives") {
  tests::TestSemanticTypeContext semanticContext;
  auto& semanticTypes = semanticContext.semanticTypes();
  auto successType = type::PrimitiveType::createI64();

  auto layout =
      computeErrorUnionLayout(semanticTypes, TargetDataLayout::ilp32(), *successType, *successType);

  ZC_EXPECT(layout.kind == ErrorUnionLayoutKind::DirectSuccess);
  ZC_EXPECT(layout.payloadLayoutState == ErrorUnionPayloadLayoutState::Known);
  ZC_EXPECT(layout.tagOffset == 0);
  ZC_EXPECT(layout.payloadOffset == 0);
  ZC_EXPECT(layout.payloadSize == 8);
  ZC_EXPECT(layout.payloadAlign == 8);
  ZC_EXPECT(layout.size == 8);
  ZC_EXPECT(layout.align == 8);
  ZC_EXPECT(layout.alternatives.size() == 1);
  ZC_EXPECT(layout.alternatives[0].kind == ErrorUnionAlternativeKind::Success);
  ZC_EXPECT(layout.alternatives[0].tag == 0);
}

ZC_TEST("ErrorUnionLayout.MarksUnresolvedNamedPayloadLayoutUnknown") {
  tests::TestSemanticTypeContext semanticContext;
  auto& semanticTypes = semanticContext.semanticTypes();
  zc::Vector<zc::Own<type::Type>> alternatives;
  alternatives.add(type::PrimitiveType::createI32());
  alternatives.add(zc::heap<type::NamedType>("IoError"_zc));
  type::UnionType unionType(zc::mv(alternatives));
  auto successType = type::PrimitiveType::createI32();

  auto layout =
      computeErrorUnionLayout(semanticTypes, TargetDataLayout::lp64(), unionType, *successType);

  ZC_EXPECT(layout.kind == ErrorUnionLayoutKind::TaggedUnion);
  ZC_EXPECT(layout.payloadLayoutState == ErrorUnionPayloadLayoutState::Unknown);
  ZC_EXPECT(layout.payloadOffset == 4);
  ZC_EXPECT(layout.payloadSize == 4);
  ZC_EXPECT(layout.payloadAlign == 4);
  ZC_EXPECT(layout.size == 0);
  ZC_EXPECT(layout.align == 1);
  ZC_EXPECT(layout.alternatives[1].payloadLayoutState == ErrorUnionPayloadLayoutState::Unknown);
  ZC_EXPECT(layout.alternatives[1].tag == 1);
  ZC_EXPECT(semanticTypes.getCanonicalKey(layout.alternatives[1].semanticTypeId) ==
            "named(IoError)"_zc);
}

ZC_TEST("ErrorUnionLayout.MarksAnyPayloadLayoutUnknown") {
  tests::TestSemanticTypeContext semanticContext;
  auto& semanticTypes = semanticContext.semanticTypes();
  zc::Vector<zc::Own<type::Type>> alternatives;
  alternatives.add(type::PrimitiveType::createUnit());
  alternatives.add(type::PrimitiveType::createAny());
  type::UnionType unionType(zc::mv(alternatives));
  auto successType = type::PrimitiveType::createUnit();

  auto layout =
      computeErrorUnionLayout(semanticTypes, TargetDataLayout::lp64(), unionType, *successType);

  ZC_EXPECT(layout.payloadLayoutState == ErrorUnionPayloadLayoutState::Unknown);
  ZC_EXPECT(layout.size == 0);
  ZC_EXPECT(layout.align == 1);
  ZC_EXPECT(layout.alternatives[1].payloadLayoutState == ErrorUnionPayloadLayoutState::Unknown);
}

ZC_TEST("ErrorUnionLayout.SelectsWiderTagForManyAlternatives") {
  tests::TestSemanticTypeContext semanticContext;
  auto& semanticTypes = semanticContext.semanticTypes();
  zc::Vector<zc::Own<type::Type>> alternatives;
  alternatives.add(type::PrimitiveType::createI32());
  for (size_t i = 0; i < 256; ++i) {
    alternatives.add(zc::heap<type::NamedType>(zc::str("Error", static_cast<uint64_t>(i))));
  }
  type::UnionType unionType(zc::mv(alternatives));
  auto successType = type::PrimitiveType::createI32();

  auto layout =
      computeErrorUnionLayout(semanticTypes, TargetDataLayout::lp64(), unionType, *successType);

  ZC_EXPECT(layout.tagType == ErrorUnionTagType::U16);
  ZC_EXPECT(layout.alternatives.size() == 257);
  ZC_EXPECT(layout.alternatives[0].tag == 0);
  ZC_EXPECT(layout.alternatives[256].tag == 256);
}

}  // namespace irgen
}  // namespace compiler
}  // namespace zomlang
