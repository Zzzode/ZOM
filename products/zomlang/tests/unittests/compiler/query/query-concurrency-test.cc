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
#include "zc/core/mutex.h"
#include "zc/core/thread.h"
#include "zc/core/time.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::query::test {

ZC_TEST("QueryConcurrencyTest.SameKeyAndRevisionUseSingleFlight") {
  QueryDatabase database(queryTestScheduler());
  registerCoreKinds(database);
  auto snapshot = database.snapshot();
  zc::MutexGuarded<zc::Vector<uint32_t>> values;

  {
    zc::Thread first([&]() {
      auto result = snapshot.get<SlowQuery>(7);
      if (!result.isRuntimeFailure()) { values.lockExclusive()->add(result.value()); }
    });
    zc::Thread second([&]() {
      auto result = snapshot.get<SlowQuery>(7);
      if (!result.isRuntimeFailure()) { values.lockExclusive()->add(result.value()); }
    });
  }

  auto retained = values.lockShared();
  ZC_REQUIRE(retained->size() == 2);
  ZC_EXPECT((*retained)[0] == 8);
  ZC_EXPECT((*retained)[1] == 8);
  retained.release();
  auto events = snapshot.events();
  ZC_EXPECT(hasEvent(events.asPtr(), QueryEventKind::SingleFlightJoined));
  size_t executed = 0;
  for (const auto& event : events) {
    if (event.kind() == QueryEventKind::Executed) { ++executed; }
  }
  ZC_EXPECT(executed == 1);
}

ZC_TEST("QueryConcurrencyTest.DirectCyclesPublishNoMemoAndRetryCleanly") {
  QueryDatabase database(queryTestScheduler());
  registerCoreKinds(database);
  auto snapshot = database.snapshot();
  auto first = snapshot.get<CycleAQuery>(1);
  ZC_EXPECT(first.isRuntimeFailure());
  ZC_EXPECT(first.runtimeFailure() == QueryRuntimeFailure::Cycle);
  ZC_EXPECT(snapshot.metadata<CycleAQuery>(1) == zc::none);
  ZC_EXPECT(snapshot.metadata<CycleBQuery>(1) == zc::none);

  auto second = snapshot.get<CycleAQuery>(1);
  ZC_EXPECT(second.isRuntimeFailure());
  ZC_EXPECT(second.runtimeFailure() == QueryRuntimeFailure::Cycle);
}

ZC_TEST("QueryConcurrencyTest.CrossWorkerWaitCyclesFailWithoutDeadlock") {
  QueryDatabase database(queryTestScheduler());
  registerCoreKinds(database);
  auto snapshot = database.snapshot();
  zc::MutexGuarded<zc::Vector<QueryRuntimeFailure>> failures;
  {
    zc::Thread first([&]() {
      auto result = snapshot.get<CrossWorkerCycleAQuery>(3);
      if (result.isRuntimeFailure()) { failures.lockExclusive()->add(result.runtimeFailure()); }
    });
    zc::Thread second([&]() {
      auto result = snapshot.get<CrossWorkerCycleBQuery>(3);
      if (result.isRuntimeFailure()) { failures.lockExclusive()->add(result.runtimeFailure()); }
    });
  }
  auto retained = failures.lockShared();
  ZC_REQUIRE(retained->size() == 2);
  ZC_EXPECT((*retained)[0] == QueryRuntimeFailure::Cycle);
  ZC_EXPECT((*retained)[1] == QueryRuntimeFailure::Cycle);
  retained.release();
  ZC_EXPECT(snapshot.metadata<CrossWorkerCycleAQuery>(3) == zc::none);
  ZC_EXPECT(snapshot.metadata<CrossWorkerCycleBQuery>(3) == zc::none);
}

ZC_TEST("QueryConcurrencyTest.CancellationAndVerifierFailurePublishNothing") {
  QueryDatabase database(queryTestScheduler());
  registerCoreKinds(database);
  auto write = beginTransaction(database);
  ZC_REQUIRE(write.set<HighInput>(90, 0));
  ZC_REQUIRE(write.commit() != zc::none);
  auto first = database.snapshot();

  auto rejected = first.get<VerifiedQuery>(8);
  ZC_EXPECT(rejected.isRuntimeFailure());
  ZC_EXPECT(rejected.runtimeFailure() == QueryRuntimeFailure::VerifierRejected);
  ZC_EXPECT(first.metadata<VerifiedQuery>(8) == zc::none);

  CancellationSource cancellation;
  cancellation.cancel();
  auto cancelled = first.get<SlowQuery>(1, cancellation.token());
  ZC_EXPECT(cancelled.isRuntimeFailure());
  ZC_EXPECT(cancelled.runtimeFailure() == QueryRuntimeFailure::Cancelled);
  ZC_EXPECT(first.metadata<SlowQuery>(1) == zc::none);

  auto update = beginTransaction(database);
  ZC_REQUIRE(update.set<HighInput>(90, 1));
  ZC_REQUIRE(update.commit() != zc::none);
  auto second = database.snapshot();
  ZC_EXPECT(second.get<VerifiedQuery>(8).value() == 8);
  auto verifiedMetadata = second.metadata<VerifiedQuery>(8);
  ZC_REQUIRE(verifiedMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(verifiedMetadata).minimumDurability() == Durability::High);
  ZC_EXPECT(second.dependencies<VerifiedQuery>(8).size() == 1);
}

ZC_TEST("QueryConcurrencyTest.CancellationDuringProviderPublishesNoCandidateOrDependencies") {
  QueryDatabase database(queryTestScheduler());
  registerCoreKinds(database);
  auto snapshot = database.snapshot();
  CancellationSource cancellation;
  auto token = cancellation.token();
  zc::MutexGuarded<zc::Maybe<QueryRuntimeFailure>> outcome;
  {
    zc::Thread request([&]() {
      auto result = snapshot.get<SlowQuery>(12, token);
      if (result.isRuntimeFailure()) { *outcome.lockExclusive() = result.runtimeFailure(); }
    });
    usleep(5000);
    cancellation.cancel();
  }

  auto retained = outcome.lockShared();
  ZC_REQUIRE(*retained != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(*retained) == QueryRuntimeFailure::Cancelled);
  retained.release();
  ZC_EXPECT(snapshot.metadata<SlowQuery>(12) == zc::none);
  ZC_EXPECT(snapshot.dependencies<SlowQuery>(12).size() == 0);
}

ZC_TEST("QueryConcurrencyTest.CancelledRequesterDoesNotCancelSharedFlight") {
  QueryDatabase database(queryTestScheduler());
  registerCoreKinds(database);
  auto snapshot = database.snapshot();
  CancellationSource firstCancellation;
  auto firstToken = firstCancellation.token();
  zc::MutexGuarded<zc::Maybe<QueryRuntimeFailure>> firstFailure;
  zc::MutexGuarded<zc::Maybe<uint32_t>> secondValue;
  {
    zc::Thread first([&]() {
      auto result = snapshot.get<SlowQuery>(20, firstToken);
      if (result.isRuntimeFailure()) { *firstFailure.lockExclusive() = result.runtimeFailure(); }
    });
    usleep(5000);
    zc::Thread second([&]() {
      auto result = snapshot.get<SlowQuery>(20);
      if (!result.isRuntimeFailure()) { *secondValue.lockExclusive() = result.value(); }
    });
    usleep(5000);
    firstCancellation.cancel();
  }

  auto cancelled = firstFailure.lockShared();
  ZC_REQUIRE(*cancelled != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(*cancelled) == QueryRuntimeFailure::Cancelled);
  cancelled.release();
  auto completed = secondValue.lockShared();
  ZC_REQUIRE(*completed != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(*completed) == 21);
  completed.release();
  ZC_EXPECT(snapshot.metadata<SlowQuery>(20) != zc::none);
}

ZC_TEST("QueryConcurrencyTest.OldSnapshotFlightCannotPublishIntoNewRevision") {
  QueryDatabase database(queryTestScheduler());
  registerCoreKinds(database);
  auto oldSnapshot = database.snapshot();
  zc::MutexGuarded<bool> oldCompleted(false);
  auto oldFlight = zc::heap<zc::Thread>([&]() {
    auto result = oldSnapshot.get<SlowQuery>(4);
    if (!result.isRuntimeFailure()) { *oldCompleted.lockExclusive() = true; }
  });
  usleep(5000);

  auto transaction = beginTransaction(database);
  ZC_REQUIRE(transaction.set<LowInput>(1, 1));
  ZC_REQUIRE(transaction.commit() != zc::none);
  auto newSnapshot = database.snapshot();
  oldFlight = nullptr;

  ZC_EXPECT(*oldCompleted.lockShared());
  ZC_EXPECT(oldSnapshot.metadata<SlowQuery>(4) != zc::none);
  ZC_EXPECT(newSnapshot.metadata<SlowQuery>(4) == zc::none);
  ZC_EXPECT(newSnapshot.get<SlowQuery>(4).value() == 5);
}

ZC_TEST("QueryConcurrencyTest.ParallelDependencyGroupExecutesConcurrently") {
  QueryDatabase database(queryTestScheduler());
  registerCoreKinds(database);
  auto snapshot = database.snapshot();
  const auto start = zc::systemPreciseMonotonicClock().now();
  auto result = snapshot.get<ParallelSlowSumQuery>(10);
  const auto elapsed = zc::systemPreciseMonotonicClock().now() - start;

  ZC_REQUIRE(!result.isRuntimeFailure());
  ZC_EXPECT(result.value() == 46);
  ZC_EXPECT(elapsed < 300 * zc::MILLISECONDS, elapsed / zc::MILLISECONDS);
  auto groups = snapshot.dependencies<ParallelSlowSumQuery>(10);
  ZC_REQUIRE(groups.size() == 1);
  ZC_EXPECT(groups[0].kind() == DependencyGroup::Kind::Parallel);
  ZC_EXPECT(groups[0].dependencies().size() == 4);
}

ZC_TEST("QueryConcurrencyTest.ParallelResultsRetainCallerKeyOrder") {
  QueryDatabase database(queryTestScheduler());
  registerCoreKinds(database);
  auto write = beginTransaction(database);
  ZC_REQUIRE(write.set<LowInput>(40, 1));
  ZC_REQUIRE(write.set<LowInput>(41, 2));
  ZC_REQUIRE(write.set<LowInput>(42, 3));
  ZC_REQUIRE(write.set<LowInput>(43, 4));
  ZC_REQUIRE(write.commit() != zc::none);

  auto snapshot = database.snapshot();
  auto result = snapshot.get<ParallelPositionalQuery>(40);
  ZC_REQUIRE(!result.isRuntimeFailure());
  ZC_EXPECT(result.value() == 4231);
  auto groups = snapshot.dependencies<ParallelPositionalQuery>(40);
  ZC_REQUIRE(groups.size() == 1);
  ZC_EXPECT(groups[0].kind() == DependencyGroup::Kind::Parallel);
  ZC_EXPECT(groups[0].dependencies().size() == 4);
}

ZC_TEST("QueryConcurrencyTest.PriorParallelDependencyGroupValidatesConcurrently") {
  QueryDatabase database(queryTestScheduler());
  registerCoreKinds(database);
  auto initial = beginTransaction(database);
  for (uint32_t key = 30; key < 34; ++key) { ZC_REQUIRE(initial.set<LowInput>(key, key)); }
  ZC_REQUIRE(initial.commit() != zc::none);
  auto first = database.snapshot();
  ZC_EXPECT(first.get<ParallelTrackedSumQuery>(30).value() == 126);

  auto changed = beginTransaction(database);
  for (uint32_t key = 30; key < 34; ++key) { ZC_REQUIRE(changed.set<LowInput>(key, key + 10)); }
  ZC_REQUIRE(changed.commit() != zc::none);
  auto second = database.snapshot();
  const auto start = zc::systemPreciseMonotonicClock().now();
  auto result = second.get<ParallelTrackedSumQuery>(30);
  const auto elapsed = zc::systemPreciseMonotonicClock().now() - start;

  ZC_REQUIRE(!result.isRuntimeFailure());
  ZC_EXPECT(result.value() == 166);
  ZC_EXPECT(elapsed < 300 * zc::MILLISECONDS, elapsed / zc::MILLISECONDS);
  ZC_EXPECT(hasEvent(second.events().asPtr(), QueryEventKind::RecomputedChanged));
}

ZC_TEST("QueryConcurrencyTest.NestedParallelGroupsFailClosedWithoutPoolStarvation") {
  QueryDatabase database(queryTestScheduler());
  registerCoreKinds(database);
  auto snapshot = database.snapshot();
  auto result = snapshot.get<NestedParallelRootQuery>(1);

  ZC_REQUIRE(result.isRuntimeFailure());
  ZC_EXPECT(result.runtimeFailure() == QueryRuntimeFailure::InvariantViolation);
  ZC_EXPECT(snapshot.metadata<NestedParallelRootQuery>(1) == zc::none);
  ZC_EXPECT(snapshot.metadata<NestedParallelLeafQuery>(1) == zc::none);
  ZC_EXPECT(hasEvent(snapshot.events().asPtr(), QueryEventKind::RuntimeFailed));
}

}  // namespace zomlang::compiler::query::test
