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

ZC_TEST("QueryDatabaseTest.TypedRegistryRejectsDuplicateKindsAndSeals") {
  ZC_EXPECT(QueryKindContract::input("Invalid Domain"_zc, 1, 1, Durability::Low) == zc::none);
  QueryDatabase database(queryTestScheduler());
  ZC_EXPECT(database.registerInputKind<LowInput>() != zc::none);
  ZC_EXPECT(database.registerInputKind<LowInput>() == zc::none);
  ZC_EXPECT(database.registerDerivedKind<AddTenQuery>() != zc::none);

  auto snapshot = database.snapshot();
  ZC_EXPECT(database.registerInputKind<HighInput>() == zc::none);
  ZC_EXPECT(snapshot.revision() == DatabaseRevision(0));
}

ZC_TEST("QueryDatabaseTest.CanonicalFingerprintIsStableAndDomainSeparated") {
  QueryDatabase first(queryTestScheduler());
  QueryDatabase second(queryTestScheduler());
  ZC_REQUIRE(first.registerInputKind<LowInput>() != zc::none);
  ZC_REQUIRE(first.registerInputKind<HighInput>() != zc::none);
  ZC_REQUIRE(second.registerInputKind<LowInput>() != zc::none);
  auto firstSnapshot = first.snapshot();
  auto secondSnapshot = second.snapshot();

  auto low = firstSnapshot.keyFingerprint<LowInput>(7);
  auto same = secondSnapshot.keyFingerprint<LowInput>(7);
  auto high = firstSnapshot.keyFingerprint<HighInput>(7);
  ZC_REQUIRE(low != zc::none);
  ZC_REQUIRE(same != zc::none);
  ZC_REQUIRE(high != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(low) == ZC_REQUIRE_NONNULL(same));
  ZC_EXPECT(ZC_REQUIRE_NONNULL(low) != ZC_REQUIRE_NONNULL(high));

  const uint8_t expected[] = {0x3b, 0x8c, 0x64, 0x54, 0xbb, 0x84, 0xc6, 0x92, 0x50, 0x99, 0x10,
                              0x9c, 0xdd, 0x61, 0x33, 0x79, 0xe5, 0x25, 0x4c, 0x0c, 0x06, 0xc5,
                              0x68, 0x12, 0x3d, 0x03, 0x39, 0xc9, 0x0e, 0x13, 0xc9, 0xaa};
  ZC_EXPECT(ZC_REQUIRE_NONNULL(low).bytes() == zc::arrayPtr(expected));
}

ZC_TEST("QueryDatabaseTest.InputTransactionsPublishCompleteSnapshotsAndPreserveEqualChangedAt") {
  QueryDatabase database(queryTestScheduler());
  registerCoreKinds(database);
  auto initial = database.snapshot();

  auto firstWrite = beginTransaction(database);
  ZC_REQUIRE(firstWrite.set<LowInput>(1, 10));
  ZC_REQUIRE(firstWrite.set<LowInput>(2, 20));
  ZC_REQUIRE(firstWrite.set<FrozenInput>(1, 98));
  ZC_REQUIRE(firstWrite.set<FrozenInput>(1, 99));
  auto firstRevision = firstWrite.commit();
  ZC_REQUIRE(firstRevision != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(firstRevision) == DatabaseRevision(1));
  auto first = database.snapshot();

  ZC_EXPECT(initial.get<LowInput>(1).runtimeFailure() == QueryRuntimeFailure::MissingInput);
  ZC_EXPECT(first.get<LowInput>(1).value() == 10);
  ZC_EXPECT(first.get<LowInput>(2).value() == 20);
  auto firstMetadata = first.metadata<LowInput>(1);
  ZC_REQUIRE(firstMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(firstMetadata).changedAt() == DatabaseRevision(1));

  auto equalWrite = beginTransaction(database);
  ZC_REQUIRE(equalWrite.set<LowInput>(1, 10));
  ZC_REQUIRE(equalWrite.set<LowInput>(2, 20));
  ZC_EXPECT(!equalWrite.set<FrozenInput>(1, 100));
  ZC_REQUIRE(equalWrite.set<FrozenInput>(1, 99));
  auto secondRevision = equalWrite.commit();
  ZC_REQUIRE(secondRevision != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(secondRevision) == DatabaseRevision(2));
  auto second = database.snapshot();
  auto secondMetadata = second.metadata<LowInput>(1);
  ZC_REQUIRE(secondMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(secondMetadata).changedAt() == DatabaseRevision(1));
}

ZC_TEST("QueryDatabaseTest.OneExclusiveTransactionCanBeOpen") {
  QueryDatabase database(queryTestScheduler());
  ZC_REQUIRE(database.registerInputKind<LowInput>() != zc::none);
  auto transaction = database.beginInputTransaction();
  ZC_REQUIRE(transaction != zc::none);
  ZC_EXPECT(!ZC_REQUIRE_NONNULL(transaction).erase<HighInput>(1));
  ZC_EXPECT(database.beginInputTransaction() == zc::none);
  ZC_REQUIRE_NONNULL(transaction).abandon();
  ZC_EXPECT(database.beginInputTransaction() != zc::none);
}

ZC_TEST("QueryDatabaseTest.InputErasurePublishesACompleteRootWithoutTombstones") {
  QueryDatabase database(queryTestScheduler());
  registerCoreKinds(database);
  auto initialWrite = beginTransaction(database);
  ZC_REQUIRE(initialWrite.set<LowInput>(1, 10));
  ZC_REQUIRE(initialWrite.set<HighInput>(2, 20));
  ZC_REQUIRE(initialWrite.set<FrozenInput>(3, 30));
  ZC_REQUIRE(initialWrite.commit() != zc::none);

  auto removeLow = beginTransaction(database);
  ZC_EXPECT(!removeLow.erase<LowInput>(99));
  ZC_EXPECT(!removeLow.erase<FrozenInput>(3));
  ZC_EXPECT(!removeLow.erase<AddTenQuery>(1));
  ZC_REQUIRE(removeLow.erase<LowInput>(1));
  ZC_EXPECT(!removeLow.erase<LowInput>(1));
  ZC_REQUIRE(removeLow.commit() != zc::none);
  auto afterLowRemoval = database.snapshot();
  ZC_EXPECT(afterLowRemoval.get<LowInput>(1).runtimeFailure() == QueryRuntimeFailure::MissingInput);
  ZC_EXPECT(!afterLowRemoval.hasRetainedValue<LowInput>(1));
  ZC_EXPECT(afterLowRemoval.get<HighInput>(2).value() == 20);
  ZC_EXPECT(afterLowRemoval.get<FrozenInput>(3).value() == 30);

  auto removeHigh = beginTransaction(database);
  ZC_REQUIRE(removeHigh.erase<HighInput>(2));
  ZC_REQUIRE(removeHigh.commit() != zc::none);
  auto afterHighRemoval = database.snapshot();
  ZC_EXPECT(afterHighRemoval.get<HighInput>(2).runtimeFailure() ==
            QueryRuntimeFailure::MissingInput);
  ZC_EXPECT(!afterHighRemoval.hasRetainedValue<HighInput>(2));
}

ZC_TEST("QueryDatabaseTest.InputTransactionUsesLastOperationAndAbandonPreservesTheRoot") {
  QueryDatabase database(queryTestScheduler());
  registerCoreKinds(database);
  auto initialWrite = beginTransaction(database);
  ZC_REQUIRE(initialWrite.set<LowInput>(1, 10));
  ZC_REQUIRE(initialWrite.commit() != zc::none);
  auto initial = database.snapshot();
  auto initialMetadata = initial.metadata<LowInput>(1);
  ZC_REQUIRE(initialMetadata != zc::none);

  auto abandoned = beginTransaction(database);
  ZC_REQUIRE(abandoned.erase<LowInput>(1));
  abandoned.abandon();
  ZC_EXPECT(database.snapshot().get<LowInput>(1).value() == 10);

  auto eraseThenSet = beginTransaction(database);
  ZC_REQUIRE(eraseThenSet.erase<LowInput>(1));
  ZC_REQUIRE(eraseThenSet.set<LowInput>(1, 10));
  ZC_REQUIRE(eraseThenSet.commit() != zc::none);
  auto equal = database.snapshot();
  ZC_EXPECT(equal.get<LowInput>(1).value() == 10);
  auto equalMetadata = equal.metadata<LowInput>(1);
  ZC_REQUIRE(equalMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(equalMetadata).changedAt() ==
            ZC_REQUIRE_NONNULL(initialMetadata).changedAt());

  auto setThenErase = beginTransaction(database);
  ZC_REQUIRE(setThenErase.set<LowInput>(1, 11));
  ZC_REQUIRE(setThenErase.erase<LowInput>(1));
  ZC_REQUIRE(setThenErase.set<LowInput>(2, 22));
  ZC_REQUIRE(setThenErase.erase<LowInput>(2));
  ZC_REQUIRE(setThenErase.commit() != zc::none);
  auto removed = database.snapshot();
  ZC_EXPECT(removed.get<LowInput>(1).runtimeFailure() == QueryRuntimeFailure::MissingInput);
  ZC_EXPECT(removed.get<LowInput>(2).runtimeFailure() == QueryRuntimeFailure::MissingInput);
}

}  // namespace zomlang::compiler::query::test
