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

ZC_TEST("QueryRedGreenTest.TracksActualReadsAndReplacesBranchDependencies") {
  auto database = queryTestDatabase();
  registerCoreKinds(database);
  auto write = beginTransaction(database);
  ZC_REQUIRE(write.set<LowInput>(0, 0).isApplied());
  ZC_REQUIRE(write.set<LowInput>(1, 11).isApplied());
  ZC_REQUIRE(write.set<LowInput>(2, 22).isApplied());
  ZC_REQUIRE(write.commit().isCommitted());
  auto first = database.snapshot();
  ZC_EXPECT(first.get<BranchQuery>(0).value() == 11);
  auto firstDependencies = first.dependencies<BranchQuery>(0);
  ZC_REQUIRE(firstDependencies.size() == 2);

  auto unusedWrite = beginTransaction(database);
  ZC_REQUIRE(unusedWrite.set<LowInput>(2, 23).isApplied());
  ZC_REQUIRE(unusedWrite.commit().isCommitted());
  auto unused = database.snapshot();
  ZC_EXPECT(unused.get<BranchQuery>(0).value() == 11);
  ZC_EXPECT(hasEvent(unused.events().asPtr(), QueryEventKind::GreenReused));

  auto switchWrite = beginTransaction(database);
  ZC_REQUIRE(switchWrite.set<LowInput>(0, 1).isApplied());
  ZC_REQUIRE(switchWrite.commit().isCommitted());
  auto switched = database.snapshot();
  ZC_EXPECT(switched.get<BranchQuery>(0).value() == 23);
  auto switchedDependencies = switched.dependencies<BranchQuery>(0);
  ZC_REQUIRE(switchedDependencies.size() == 2);
  ZC_EXPECT(hasEvent(switched.events().asPtr(), QueryEventKind::RecomputedChanged));
}

ZC_TEST("QueryRedGreenTest.BackdatesEqualProjectionAndPublishesChangedProjection") {
  QueryDatabase database(queryTestScheduler(), queryTestDescriptorInventory());
  registerCoreKinds(database);
  auto write = beginTransaction(database);
  ZC_REQUIRE(write.set<LowInput>(5, 1).isApplied());
  ZC_REQUIRE(write.commit().isCommitted());
  auto first = database.snapshot();
  ZC_EXPECT(first.get<ParityProjectionQuery>(5).value() == 1);
  ZC_EXPECT(first.get<ChangedProjectionQuery>(5).value() == 11);
  auto projectionMetadata = first.metadata<ParityProjectionQuery>(5);
  auto localMetadata = first.metadata<ChangedProjectionQuery>(5);
  ZC_REQUIRE(projectionMetadata != zc::none && localMetadata != zc::none);

  auto update = beginTransaction(database);
  ZC_REQUIRE(update.set<LowInput>(5, 3).isApplied());
  ZC_REQUIRE(update.commit().isCommitted());
  auto second = database.snapshot();
  ZC_EXPECT(second.get<ParityProjectionQuery>(5).value() == 1);
  ZC_EXPECT(second.get<ChangedProjectionQuery>(5).value() == 13);
  auto secondProjection = second.metadata<ParityProjectionQuery>(5);
  auto secondLocal = second.metadata<ChangedProjectionQuery>(5);
  ZC_REQUIRE(secondProjection != zc::none && secondLocal != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(secondProjection).changedAt() ==
            ZC_REQUIRE_NONNULL(projectionMetadata).changedAt());
  ZC_EXPECT(ZC_REQUIRE_NONNULL(secondLocal).changedAt() == DatabaseRevision(2));
  ZC_EXPECT(hasEvent(second.events().asPtr(), QueryEventKind::RecomputedEqual));
}

ZC_TEST("QueryRedGreenTest.DurabilityFastPathAndDecreaseRemainSound") {
  auto database = queryTestDatabase();
  registerCoreKinds(database);
  auto write = beginTransaction(database);
  ZC_REQUIRE(write.set<HighInput>(9, 0).isApplied());
  ZC_REQUIRE(write.set<HighInput>(10, 77).isApplied());
  ZC_REQUIRE(write.set<LowInput>(10, 100).isApplied());
  ZC_REQUIRE(write.set<LowInput>(99, 1).isApplied());
  ZC_REQUIRE(write.commit().isCommitted());
  auto first = database.snapshot();
  ZC_EXPECT(first.get<HighOnlyQuery>(10).value() == 77);
  ZC_EXPECT(first.get<DurabilitySwitchQuery>(0).value() == 77);
  auto firstSwitch = first.metadata<DurabilitySwitchQuery>(0);
  ZC_REQUIRE(firstSwitch != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(firstSwitch).minimumDurability() == Durability::High);

  auto lowOnly = beginTransaction(database);
  ZC_REQUIRE(lowOnly.set<LowInput>(99, 2).isApplied());
  ZC_REQUIRE(lowOnly.commit().isCommitted());
  auto second = database.snapshot();
  ZC_EXPECT(second.get<HighOnlyQuery>(10).value() == 77);
  ZC_EXPECT(hasEvent(second.events().asPtr(), QueryEventKind::GreenReused));

  auto decrease = beginTransaction(database);
  ZC_REQUIRE(decrease.set<HighInput>(9, 1).isApplied());
  ZC_REQUIRE(decrease.commit().isCommitted());
  auto third = database.snapshot();
  ZC_EXPECT(third.get<DurabilitySwitchQuery>(0).value() == 77);
  auto thirdSwitch = third.metadata<DurabilitySwitchQuery>(0);
  ZC_REQUIRE(thirdSwitch != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(thirdSwitch).minimumDurability() == Durability::Low);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(thirdSwitch).changedAt() == DatabaseRevision(3));
  ZC_EXPECT(hasEvent(third.events().asPtr(), QueryEventKind::DurabilityDecreased));

  auto lowDependency = beginTransaction(database);
  ZC_REQUIRE(lowDependency.set<LowInput>(10, 101).isApplied());
  ZC_REQUIRE(lowDependency.commit().isCommitted());
  auto fourth = database.snapshot();
  ZC_EXPECT(fourth.get<DurabilitySwitchQuery>(0).value() == 77);
  ZC_EXPECT(hasEvent(fourth.events().asPtr(), QueryEventKind::RecomputedEqual));
}

ZC_TEST("QueryRedGreenTest.ParallelGroupsAreCanonicalAndAlternativesAreMemoized") {
  auto database = queryTestDatabase();
  registerCoreKinds(database);
  auto write = beginTransaction(database);
  ZC_REQUIRE(write.set<LowInput>(20, 2).isApplied());
  ZC_REQUIRE(write.set<LowInput>(21, 3).isApplied());
  ZC_REQUIRE(write.set<LowInput>(30, 0).isApplied());
  ZC_REQUIRE(write.commit().isCommitted());
  auto snapshot = database.snapshot();
  ZC_EXPECT(snapshot.get<ParallelSumQuery>(20).value() == 5);
  auto groups = snapshot.dependencies<ParallelSumQuery>(20);
  ZC_REQUIRE(groups.size() == 1);
  ZC_EXPECT(groups[0].kind() == DependencyGroup::Kind::Parallel);
  ZC_REQUIRE(groups[0].dependencies().size() == 2);
  ZC_EXPECT(groups[0].dependencies()[0].key() < groups[0].dependencies()[1].key());

  auto absent = snapshot.get<DeterministicAlternativeQuery>(30);
  ZC_EXPECT(!absent.isRuntimeFailure());
  ZC_EXPECT(absent.kind() == QueryValueKind::Absence);

  auto failureWrite = beginTransaction(database);
  ZC_REQUIRE(failureWrite.set<LowInput>(30, 1).isApplied());
  ZC_REQUIRE(failureWrite.commit().isCommitted());
  auto failureSnapshot = database.snapshot();
  auto failure = failureSnapshot.get<DeterministicAlternativeQuery>(30);
  ZC_EXPECT(!failure.isRuntimeFailure());
  ZC_EXPECT(failure.kind() == QueryValueKind::SemanticFailure);
  ZC_EXPECT(failure.semanticFailureBytes() == encodeUint32(0xdead).asPtr());

  auto valueWrite = beginTransaction(database);
  ZC_REQUIRE(valueWrite.set<LowInput>(30, 9).isApplied());
  ZC_REQUIRE(valueWrite.commit().isCommitted());
  auto valueSnapshot = database.snapshot();
  auto value = valueSnapshot.get<DeterministicAlternativeQuery>(30);
  ZC_EXPECT(!value.isRuntimeFailure());
  ZC_EXPECT(value.kind() == QueryValueKind::Value);
  ZC_EXPECT(value.value() == 9);
  auto alternativeDependencies = valueSnapshot.dependencies<DeterministicAlternativeQuery>(30);
  ZC_EXPECT(alternativeDependencies.size() == 1);
}

ZC_TEST("QueryRedGreenTest.RemovalInvalidatesAtItsDurabilityAndReaddBackdatesEqualValue") {
  auto database = queryTestDatabase();
  registerCoreKinds(database);
  auto initialWrite = beginTransaction(database);
  ZC_REQUIRE(initialWrite.set<LowInput>(5, 1).isApplied());
  ZC_REQUIRE(initialWrite.set<HighInput>(5, 50).isApplied());
  ZC_REQUIRE(initialWrite.commit().isCommitted());
  auto initial = database.snapshot();
  ZC_EXPECT(initial.get<AddTenQuery>(5).value() == 11);
  ZC_EXPECT(initial.get<HighOnlyQuery>(5).value() == 50);
  auto initialAddMetadata = initial.metadata<AddTenQuery>(5);
  ZC_REQUIRE(initialAddMetadata != zc::none);

  auto removeLow = beginTransaction(database);
  ZC_REQUIRE(removeLow.erase<LowInput>(5).isApplied());
  ZC_REQUIRE(removeLow.commit().isCommitted());
  auto lowRemoved = database.snapshot();
  ZC_EXPECT(lowRemoved.get<AddTenQuery>(5).runtimeFailure() == QueryRuntimeFailure::MissingInput);
  ZC_EXPECT(lowRemoved.get<HighOnlyQuery>(5).value() == 50);
  ZC_EXPECT(hasEvent(lowRemoved.events().asPtr(), QueryEventKind::GreenReused));

  auto readdLow = beginTransaction(database);
  ZC_REQUIRE(readdLow.set<LowInput>(5, 1).isApplied());
  ZC_REQUIRE(readdLow.commit().isCommitted());
  auto readded = database.snapshot();
  ZC_EXPECT(readded.get<AddTenQuery>(5).value() == 11);
  auto readdedMetadata = readded.metadata<AddTenQuery>(5);
  ZC_REQUIRE(readdedMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(readdedMetadata).verifiedAt() == DatabaseRevision(3));
  ZC_EXPECT(ZC_REQUIRE_NONNULL(readdedMetadata).changedAt() ==
            ZC_REQUIRE_NONNULL(initialAddMetadata).changedAt());
  auto readdedDependencies = readded.dependencies<AddTenQuery>(5);
  ZC_REQUIRE(readdedDependencies.size() == 1);
  ZC_REQUIRE(readdedDependencies[0].dependencies().size() == 1);
  ZC_EXPECT(readdedDependencies[0].dependencies()[0].changedAt() == DatabaseRevision(3));
  ZC_EXPECT(hasEvent(readded.events().asPtr(), QueryEventKind::RecomputedEqual));

  auto removeHigh = beginTransaction(database);
  ZC_REQUIRE(removeHigh.erase<HighInput>(5).isApplied());
  ZC_REQUIRE(removeHigh.commit().isCommitted());
  auto highRemoved = database.snapshot();
  ZC_EXPECT(highRemoved.get<HighOnlyQuery>(5).runtimeFailure() ==
            QueryRuntimeFailure::MissingInput);
}

}  // namespace zomlang::compiler::query::test
