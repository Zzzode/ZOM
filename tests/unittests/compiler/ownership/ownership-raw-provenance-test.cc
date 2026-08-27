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
// See the License for the specific language governing permissions and
// limitations under the License.

#include "zc/ztest/test.h"
#include "compiler/ir/ir-identity.h"
#include "compiler/mir/built-mir.h"
#include "compiler/ownership/facts/raw-provenance.h"
#include "tests/unittests/compiler/test-semantic-type-context.h"

namespace zomlang::compiler::ownership::facts {
namespace {

using type::SemanticTypeId;

MirEventKey makeEventKey(uint32_t operandOrdinal = 0) {
  return MirEventKey{MirLocation{identity::DefId{}, MirPoint::entry()}, operandOrdinal};
}

mir::MirPlace makePlace(uint32_t localOrdinal) {
  auto local = mir::MirLocalId::fromOrdinal(localOrdinal);
  ZC_REQUIRE(local != zc::none);
  ZC_IF_SOME(id, local) {
    zc::Vector<mir::MirProjection> projections;
    return mir::MirPlace(id, tests::testSemanticType(), zc::mv(projections),
                         tests::testSemanticType(1));
  }
  ZC_UNREACHABLE
}

MovePathKey makeMovePathKey(uint32_t localOrdinal) {
  return MovePathKey{identity::DefId{}, makePlace(localOrdinal)};
}

UnsafeBoundaryKey makeUnsafeBoundaryKey(uint32_t ordinal = 0) {
  return UnsafeBoundaryKey{makeEventKey(), ordinal};
}

ZC_TEST("RawInputOrigin equality distinguishes receiver and parameter seeds") {
  RawInputOrigin receiver{true, 0};
  RawInputOrigin receiverCopy{true, 0};
  RawInputOrigin parameter{false, 0};
  RawInputOrigin parameterOther{false, 1};

  ZC_EXPECT(receiver == receiverCopy);
  ZC_EXPECT(receiver != parameter);
  ZC_EXPECT(parameter != parameterOther);
}

ZC_TEST("RawStaticAddressOrigin equality compares creation events") {
  RawStaticAddressOrigin first{makeEventKey(0)};
  RawStaticAddressOrigin firstCopy{makeEventKey(0)};
  RawStaticAddressOrigin second{makeEventKey(1)};

  ZC_EXPECT(first == firstCopy);
  ZC_EXPECT(first != second);
}

ZC_TEST("RawUnsafeAddressOrigin equality compares boundary keys") {
  RawUnsafeAddressOrigin first{makeUnsafeBoundaryKey(0)};
  RawUnsafeAddressOrigin firstCopy{makeUnsafeBoundaryKey(0)};
  RawUnsafeAddressOrigin second{makeUnsafeBoundaryKey(1)};

  ZC_EXPECT(first == firstCopy);
  ZC_EXPECT(first != second);
}

ZC_TEST("cloneRawProvenanceOrigin clones RawInputOrigin") {
  RawProvenanceOrigin origin{RawInputOrigin{false, 3}};
  auto cloned = cloneRawProvenanceOrigin(origin);

  ZC_REQUIRE(cloned.is<RawInputOrigin>());
  ZC_EXPECT(cloned.get<RawInputOrigin>() == RawInputOrigin(false, 3));
}

ZC_TEST("cloneRawProvenanceOrigin clones RawStaticAddressOrigin") {
  RawProvenanceOrigin origin{RawStaticAddressOrigin{makeEventKey(7)}};
  auto cloned = cloneRawProvenanceOrigin(origin);

  ZC_REQUIRE(cloned.is<RawStaticAddressOrigin>());
  ZC_EXPECT(cloned.get<RawStaticAddressOrigin>() == RawStaticAddressOrigin{makeEventKey(7)});
}

ZC_TEST("cloneRawProvenanceOrigin clones RawUnsafeAddressOrigin") {
  RawProvenanceOrigin origin{RawUnsafeAddressOrigin{makeUnsafeBoundaryKey(2)}};
  auto cloned = cloneRawProvenanceOrigin(origin);

  ZC_REQUIRE(cloned.is<RawUnsafeAddressOrigin>());
  ZC_EXPECT(cloned.get<RawUnsafeAddressOrigin>() ==
            RawUnsafeAddressOrigin{makeUnsafeBoundaryKey(2)});
}

ZC_TEST("cloneRawProvenanceOrigin clones RawReferenceOrigin") {
  ReferenceInputOrigin refOrigin{makeEventKey(1), OwnershipPoint::cfg(MirPoint::entry()),
                                 ParameterReferenceOrigin{0}, makeMovePathKey(1)};
  RawProvenanceOrigin origin{RawReferenceOrigin{refOrigin.clone()}};
  auto cloned = cloneRawProvenanceOrigin(origin);

  ZC_REQUIRE(cloned.is<RawReferenceOrigin>());
  const auto& clonedRef = cloned.get<RawReferenceOrigin>().origin;
  ZC_EXPECT(clonedRef.entry == refOrigin.entry);
  ZC_EXPECT(clonedRef.activation.kind() == refOrigin.activation.kind());
  ZC_REQUIRE(clonedRef.detail.is<ParameterReferenceOrigin>());
  ZC_EXPECT(clonedRef.detail.get<ParameterReferenceOrigin>().rootParameter == 0);
  ZC_EXPECT(clonedRef.referent.owner == refOrigin.referent.owner);
  ZC_EXPECT(clonedRef.referent.place.local() == refOrigin.referent.place.local());
}

ZC_TEST("RawProvenanceCarrierKey clone deep-copies destination move path") {
  auto key = RawProvenanceCarrierKey{makeEventKey(5), makeMovePathKey(1)};
  auto cloned = key.clone();

  ZC_EXPECT(cloned.introduction == key.introduction);
  ZC_EXPECT(cloned.destination.owner == key.destination.owner);
  ZC_EXPECT(cloned.destination.place.local() == key.destination.place.local());
  ZC_EXPECT(cloned.destination.place.rootType() == key.destination.place.rootType());
  ZC_EXPECT(cloned.destination.place.resultType() == key.destination.place.resultType());
}

ZC_TEST("RawProvenanceFact clone deep-copies predecessors and origins") {
  auto key = RawProvenanceCarrierKey{makeEventKey(1), makeMovePathKey(1)};
  zc::Vector<RawProvenanceCarrierKey> predecessors;
  predecessors.add(RawProvenanceCarrierKey{makeEventKey(2), makeMovePathKey(2)});
  predecessors.add(RawProvenanceCarrierKey{makeEventKey(3), makeMovePathKey(3)});
  zc::Vector<RawProvenanceOrigin> origins;
  origins.add(RawProvenanceOrigin{RawInputOrigin{true, 0}});
  origins.add(RawProvenanceOrigin{RawStaticAddressOrigin{makeEventKey(4)}});

  RawProvenanceFact fact{key.clone(), zc::mv(predecessors), zc::mv(origins)};
  auto cloned = fact.clone();

  ZC_EXPECT(cloned.key.introduction == fact.key.introduction);
  ZC_EXPECT(cloned.key.destination.place.local() == fact.key.destination.place.local());
  ZC_REQUIRE(cloned.predecessors.size() == fact.predecessors.size());
  ZC_EXPECT(cloned.predecessors[0].introduction == fact.predecessors[0].introduction);
  ZC_EXPECT(cloned.predecessors[1].introduction == fact.predecessors[1].introduction);
  ZC_REQUIRE(cloned.origins.size() == fact.origins.size());
  ZC_REQUIRE(cloned.origins[0].is<RawInputOrigin>());
  ZC_EXPECT(cloned.origins[0].get<RawInputOrigin>() == RawInputOrigin(true, 0));
  ZC_REQUIRE(cloned.origins[1].is<RawStaticAddressOrigin>());
  ZC_EXPECT(cloned.origins[1].get<RawStaticAddressOrigin>() ==
            RawStaticAddressOrigin{makeEventKey(4)});
}

}  // namespace
}  // namespace zomlang::compiler::ownership::facts
