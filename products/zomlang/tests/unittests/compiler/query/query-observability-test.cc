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

ZC_TEST("QueryObservabilityTest.ClassifiesReuseChangeCancellationCycleAndRejection") {
  QueryDatabase database(queryTestScheduler());
  registerCoreKinds(database);
  auto write = beginTransaction(database);
  ZC_REQUIRE(write.set<LowInput>(5, 1));
  ZC_REQUIRE(write.set<HighInput>(90, 0));
  ZC_REQUIRE(write.commit() != zc::none);
  auto first = database.snapshot();

  ZC_EXPECT(first.get<ParityProjectionQuery>(5).value() == 1);
  ZC_EXPECT(first.get<ParityProjectionQuery>(5).value() == 1);
  ZC_EXPECT(hasEvent(first.events().asPtr(), QueryEventKind::Executed));
  ZC_EXPECT(hasEvent(first.events().asPtr(), QueryEventKind::GreenReused));

  auto update = beginTransaction(database);
  ZC_REQUIRE(update.set<LowInput>(5, 3));
  ZC_REQUIRE(update.commit() != zc::none);
  auto second = database.snapshot();
  ZC_EXPECT(second.get<ParityProjectionQuery>(5).value() == 1);

  CancellationSource cancellation;
  cancellation.cancel();
  auto cancelled = second.get<SlowQuery>(1, cancellation.token());
  ZC_REQUIRE(cancelled.isRuntimeFailure());
  ZC_EXPECT(cancelled.runtimeFailure() == QueryRuntimeFailure::Cancelled);

  auto cycle = second.get<CycleAQuery>(1);
  ZC_REQUIRE(cycle.isRuntimeFailure());
  ZC_EXPECT(cycle.runtimeFailure() == QueryRuntimeFailure::Cycle);

  auto rejected = second.get<VerifiedQuery>(8);
  ZC_REQUIRE(rejected.isRuntimeFailure());
  ZC_EXPECT(rejected.runtimeFailure() == QueryRuntimeFailure::VerifierRejected);

  auto missing = second.get<AddTenQuery>(999);
  ZC_REQUIRE(missing.isRuntimeFailure());
  ZC_EXPECT(missing.runtimeFailure() == QueryRuntimeFailure::MissingInput);

  auto events = second.events();
  ZC_EXPECT(hasEvent(events.asPtr(), QueryEventKind::RecomputedEqual));
  ZC_EXPECT(hasEvent(events.asPtr(), QueryEventKind::RecomputedChanged));
  ZC_EXPECT(hasEvent(events.asPtr(), QueryEventKind::Cancelled));
  ZC_EXPECT(hasEvent(events.asPtr(), QueryEventKind::Cycle));
  ZC_EXPECT(hasEvent(events.asPtr(), QueryEventKind::VerifierRejected));
  ZC_EXPECT(hasEvent(events.asPtr(), QueryEventKind::RuntimeFailed));
}

ZC_TEST("QueryObservabilityTest.ParallelCompletionOrderDoesNotChangeCanonicalTrace") {
  QueryDatabase firstDatabase(queryTestScheduler());
  QueryDatabase secondDatabase(queryTestScheduler());
  registerCoreKinds(firstDatabase);
  registerCoreKinds(secondDatabase);
  auto first = firstDatabase.snapshot();
  auto second = secondDatabase.snapshot();

  ZC_EXPECT(first.get<ParallelSlowSumQuery>(20).value() == 86);
  ZC_EXPECT(second.get<ParallelSlowSumQuery>(20).value() == 86);
  auto firstEvents = first.events();
  auto secondEvents = second.events();
  ZC_REQUIRE(firstEvents.size() == secondEvents.size());
  for (size_t index = 0; index < firstEvents.size(); ++index) {
    ZC_EXPECT(firstEvents[index].revision() == secondEvents[index].revision());
    ZC_EXPECT(firstEvents[index].key() == secondEvents[index].key());
    ZC_EXPECT(firstEvents[index].kind() == secondEvents[index].kind());
  }
}

}  // namespace zomlang::compiler::query::test
