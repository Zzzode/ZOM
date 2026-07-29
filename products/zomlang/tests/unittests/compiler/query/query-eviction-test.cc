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

#include "query-test-specs.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::query::test {

ZC_TEST("QueryEvictionTest.RetainsDependenciesAndConservativelyRecomputesWithoutEqualityWitness") {
  auto database = queryTestDatabase();
  registerCoreKinds(database);
  auto write = beginTransaction(database);
  ZC_REQUIRE(write.set<LowInput>(40, 5).isApplied());
  ZC_REQUIRE(write.set<LowInput>(99, 1).isApplied());
  ZC_REQUIRE(write.commit().isCommitted());
  auto first = database.snapshot();

  ZC_EXPECT(first.get<EvictableQuery>(40).value() == 15);
  auto initialMetadata = first.metadata<EvictableQuery>(40);
  auto initialDependencies = first.dependencies<EvictableQuery>(40);
  ZC_REQUIRE(initialMetadata != zc::none);
  ZC_REQUIRE(initialDependencies.size() == 1);
  ZC_EXPECT(first.hasRetainedValue<EvictableQuery>(40));
  ZC_EXPECT(!first.evictValue<AddTenQuery>(40));
  ZC_REQUIRE(first.evictValue<EvictableQuery>(40));
  ZC_EXPECT(!first.hasRetainedValue<EvictableQuery>(40));
  ZC_EXPECT(!first.evictValue<EvictableQuery>(40));
  ZC_EXPECT(hasEvent(first.events().asPtr(), QueryEventKind::ValueEvicted));

  auto evictedMetadata = first.metadata<EvictableQuery>(40);
  auto evictedDependencies = first.dependencies<EvictableQuery>(40);
  ZC_REQUIRE(evictedMetadata != zc::none);
  ZC_REQUIRE(evictedDependencies.size() == 1);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(evictedMetadata).verifiedAt() ==
            ZC_REQUIRE_NONNULL(initialMetadata).verifiedAt());
  ZC_EXPECT(ZC_REQUIRE_NONNULL(evictedMetadata).changedAt() ==
            ZC_REQUIRE_NONNULL(initialMetadata).changedAt());
  ZC_EXPECT(evictedDependencies[0].dependencies()[0].key() ==
            initialDependencies[0].dependencies()[0].key());

  auto unrelated = beginTransaction(database);
  ZC_REQUIRE(unrelated.set<LowInput>(99, 2).isApplied());
  ZC_REQUIRE(unrelated.commit().isCommitted());
  auto second = database.snapshot();
  ZC_EXPECT(!second.hasRetainedValue<EvictableQuery>(40));
  ZC_EXPECT(second.dependencies<EvictableQuery>(40).size() == 1);
  ZC_EXPECT(second.get<EvictableQuery>(40).value() == 15);
  ZC_EXPECT(second.hasRetainedValue<EvictableQuery>(40));

  auto recomputedMetadata = second.metadata<EvictableQuery>(40);
  ZC_REQUIRE(recomputedMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(recomputedMetadata).verifiedAt() == DatabaseRevision(2));
  ZC_EXPECT(ZC_REQUIRE_NONNULL(recomputedMetadata).changedAt() == DatabaseRevision(2));
  ZC_EXPECT(hasEvent(second.events().asPtr(), QueryEventKind::RecomputedChanged));
}

}  // namespace zomlang::compiler::query::test
