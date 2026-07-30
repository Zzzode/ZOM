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
#include "zc/core/thread.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::query::test {
namespace {

using CompleteContext = TestCompleteContextInput;
using CompleteContextSealResult = FinalSealResult<CompleteContext::Key, identity::Sha256Digest>;

identity::Sha256Digest repeatedDigest(uint8_t byte) {
  uint8_t bytes[32];
  for (auto& value : bytes) { value = byte; }
  auto result = identity::Sha256Digest::fromBytes(zc::arrayPtr(bytes));
  ZC_IF_SOME(value, result) { return value; }
  ZC_FAIL_REQUIRE("invalid final-seal digest fixture");
}

void registerFinalSealInput(QueryDatabase& database) {
  ZC_REQUIRE(database.registerDescriptor<LowInput>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<CompleteContext>().isRegistered());
}

QuerySnapshot publishCompleteContext(QueryDatabase& database, uint32_t key, uint32_t value) {
  auto write = beginTransaction(database);
  ZC_REQUIRE(write.set<CompleteContext>(key, value).isApplied());
  ZC_REQUIRE(write.commit().isCommitted());
  return database.snapshot();
}

}  // namespace

ZC_TEST("QueryDatabaseTest.GeneratedRegistryRejectsDuplicateAndLateRegistration") {
  auto database = queryTestDatabase();
  ZC_EXPECT(database.registerDescriptor<LowInput>().isRegistered());
  auto duplicate = database.registerDescriptor<LowInput>();
  ZC_EXPECT(!duplicate.isRegistered());
  ZC_EXPECT(duplicate.failure() == DescriptorRegistrationFailure::SlotAlreadyRegistered);
  ZC_EXPECT(database.registerDescriptor<AddTenQuery>().isRegistered());

  auto snapshot = database.snapshot();
  ZC_EXPECT_THROW_MESSAGE("registration attempted after registry publication",
                          database.registerDescriptor<HighInput>());
  ZC_EXPECT(snapshot.revision() == DatabaseRevision(0));
}

ZC_TEST("QueryDatabaseTest.CanonicalFingerprintIsStableAndDomainSeparated") {
  auto first = queryTestDatabase();
  auto second = queryTestDatabase();
  ZC_REQUIRE(first.registerDescriptor<LowInput>().isRegistered());
  ZC_REQUIRE(first.registerDescriptor<HighInput>().isRegistered());
  ZC_REQUIRE(second.registerDescriptor<LowInput>().isRegistered());
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

  const uint8_t expected[] = {0x88, 0xd3, 0x4b, 0xf7, 0x11, 0xe5, 0x29, 0xf7, 0x3c, 0x08, 0xa9,
                              0xcf, 0xc6, 0x9e, 0xd7, 0x1e, 0xed, 0xc6, 0x85, 0x48, 0x01, 0xc9,
                              0x33, 0x18, 0x94, 0x28, 0x2d, 0x65, 0xe1, 0xbc, 0x20, 0x9a};
  ZC_EXPECT(ZC_REQUIRE_NONNULL(low).bytes() == zc::arrayPtr(expected));
}

ZC_TEST("QueryDatabaseTest.InputTransactionsPublishCompleteSnapshotsAndPreserveEqualChangedAt") {
  auto database = queryTestDatabase();
  registerCoreKinds(database);
  auto initial = database.snapshot();

  auto firstWrite = beginTransaction(database);
  ZC_REQUIRE(firstWrite.set<LowInput>(1, 10).isApplied());
  ZC_REQUIRE(firstWrite.set<LowInput>(2, 20).isApplied());
  ZC_REQUIRE(firstWrite.set<FrozenInput>(1, 99).isApplied());
  auto firstRevision = firstWrite.commit();
  ZC_REQUIRE(firstRevision.isCommitted());
  ZC_EXPECT(firstRevision.revision() == DatabaseRevision(1));
  auto first = database.snapshot();

  ZC_EXPECT(initial.get<LowInput>(1).runtimeFailure() == QueryRuntimeFailure::MissingInput);
  ZC_EXPECT(first.get<LowInput>(1).value() == 10);
  ZC_EXPECT(first.get<LowInput>(2).value() == 20);
  auto firstMetadata = first.metadata<LowInput>(1);
  ZC_REQUIRE(firstMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(firstMetadata).changedAt() == DatabaseRevision(1));

  auto equalWrite = beginTransaction(database);
  ZC_REQUIRE(equalWrite.set<LowInput>(1, 10).isApplied());
  ZC_REQUIRE(equalWrite.set<LowInput>(2, 20).isApplied());
  auto frozenMutation = equalWrite.set<FrozenInput>(1, 100);
  ZC_EXPECT(!frozenMutation.isApplied());
  ZC_EXPECT(frozenMutation.failure() == InputTransactionFailure::FrozenInputMutation);
  auto secondRevision = equalWrite.commit();
  ZC_REQUIRE(secondRevision.isCommitted());
  ZC_EXPECT(secondRevision.revision() == DatabaseRevision(2));
  auto second = database.snapshot();
  auto secondMetadata = second.metadata<LowInput>(1);
  ZC_REQUIRE(secondMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(secondMetadata).changedAt() == DatabaseRevision(1));
}

ZC_TEST("QueryDatabaseTest.OneExclusiveTransactionCanBeOpen") {
  auto database = queryTestDatabase();
  ZC_REQUIRE(database.registerDescriptor<LowInput>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<HighInput>().isRegistered());
  auto transaction = database.beginInputTransaction(DatabaseRevision(0));
  ZC_REQUIRE(transaction.isOpened());
  auto write = zc::mv(transaction).takeTransaction();
  auto missing = write.erase<HighInput>(1);
  ZC_EXPECT(!missing.isApplied());
  ZC_EXPECT(missing.failure() == InputTransactionFailure::MissingInputForErase);
  auto concurrent = database.beginInputTransaction(DatabaseRevision(0));
  ZC_EXPECT(!concurrent.isOpened());
  ZC_EXPECT(concurrent.failure() == InputTransactionFailure::TransactionAlreadyOpen);
  write.abandon();
  ZC_EXPECT(database.beginInputTransaction(DatabaseRevision(0)).isOpened());
}

ZC_TEST("QueryDatabaseTest.InputErasurePublishesACompleteRootWithoutTombstones") {
  auto database = queryTestDatabase();
  registerCoreKinds(database);
  auto initialWrite = beginTransaction(database);
  ZC_REQUIRE(initialWrite.set<LowInput>(1, 10).isApplied());
  ZC_REQUIRE(initialWrite.set<HighInput>(2, 20).isApplied());
  ZC_REQUIRE(initialWrite.set<FrozenInput>(3, 30).isApplied());
  ZC_REQUIRE(initialWrite.commit().isCommitted());

  auto removeLow = beginTransaction(database);
  ZC_EXPECT(!removeLow.erase<LowInput>(99).isApplied());
  ZC_EXPECT(!removeLow.erase<FrozenInput>(3).isApplied());
  ZC_EXPECT(!removeLow.erase<AddTenQuery>(1).isApplied());
  ZC_REQUIRE(removeLow.erase<LowInput>(1).isApplied());
  ZC_EXPECT(!removeLow.erase<LowInput>(1).isApplied());
  ZC_REQUIRE(removeLow.commit().isCommitted());
  auto afterLowRemoval = database.snapshot();
  ZC_EXPECT(afterLowRemoval.get<LowInput>(1).runtimeFailure() == QueryRuntimeFailure::MissingInput);
  ZC_EXPECT(!afterLowRemoval.hasRetainedValue<LowInput>(1));
  ZC_EXPECT(afterLowRemoval.get<HighInput>(2).value() == 20);
  ZC_EXPECT(afterLowRemoval.get<FrozenInput>(3).value() == 30);

  auto removeHigh = beginTransaction(database);
  ZC_REQUIRE(removeHigh.erase<HighInput>(2).isApplied());
  ZC_REQUIRE(removeHigh.commit().isCommitted());
  auto afterHighRemoval = database.snapshot();
  ZC_EXPECT(afterHighRemoval.get<HighInput>(2).runtimeFailure() ==
            QueryRuntimeFailure::MissingInput);
  ZC_EXPECT(!afterHighRemoval.hasRetainedValue<HighInput>(2));
}

ZC_TEST("QueryDatabaseTest.InputTransactionRejectsDuplicateOperationsAndAbandonPreservesRoot") {
  auto database = queryTestDatabase();
  registerCoreKinds(database);
  auto initialWrite = beginTransaction(database);
  ZC_REQUIRE(initialWrite.set<LowInput>(1, 10).isApplied());
  ZC_REQUIRE(initialWrite.commit().isCommitted());
  auto initial = database.snapshot();
  auto initialMetadata = initial.metadata<LowInput>(1);
  ZC_REQUIRE(initialMetadata != zc::none);

  auto abandoned = beginTransaction(database);
  ZC_REQUIRE(abandoned.erase<LowInput>(1).isApplied());
  abandoned.abandon();
  ZC_EXPECT(database.snapshot().get<LowInput>(1).value() == 10);

  auto duplicate = beginTransaction(database);
  ZC_REQUIRE(duplicate.set<LowInput>(1, 10).isApplied());
  auto duplicateResult = duplicate.erase<LowInput>(1);
  ZC_EXPECT(!duplicateResult.isApplied());
  ZC_EXPECT(duplicateResult.failure() == InputTransactionFailure::DuplicateInputOperation);
  ZC_REQUIRE(duplicate.commit().isCommitted());
  auto equal = database.snapshot();
  auto equalMetadata = equal.metadata<LowInput>(1);
  ZC_REQUIRE(equalMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(equalMetadata).changedAt() ==
            ZC_REQUIRE_NONNULL(initialMetadata).changedAt());
}

ZC_TEST("QueryDatabaseTest.InputProbeTracksPresenceWithoutTombstonesOrContextPoisoning") {
  auto database = queryTestDatabase();
  registerCoreKinds(database);
  auto initialWrite = beginTransaction(database);
  ZC_REQUIRE(initialWrite.set<HighInput>(100, 40).isApplied());
  ZC_REQUIRE(initialWrite.commit().isCommitted());

  auto absent = database.snapshot();
  auto absentRootProbe = absent.probeInput<LowInput>(5);
  ZC_REQUIRE(!absentRootProbe.isRuntimeFailure());
  ZC_EXPECT(absentRootProbe.kind() == QueryValueKind::Absence);
  ZC_EXPECT(absent.get<LowInput>(5).runtimeFailure() == QueryRuntimeFailure::MissingInput);
  ZC_EXPECT(!absent.hasRetainedValue<LowInput>(5));
  ZC_EXPECT(absent.metadata<LowInput>(5) == zc::none);

  auto absentValue = absent.get<OptionalLowInputQuery>(5);
  ZC_REQUIRE(!absentValue.isRuntimeFailure());
  ZC_EXPECT(absentValue.value() == 40);
  auto absentDependencies = absent.dependencies<OptionalLowInputQuery>(5);
  ZC_REQUIRE(absentDependencies.size() == 2);
  ZC_REQUIRE(absentDependencies[0].dependencies().size() == 1);
  const auto absentObservation = absentDependencies[0].dependencies()[0].inputProbeObservation();
  ZC_REQUIRE(absentObservation != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(absentObservation) == InputProbeObservation::Absent);
  ZC_EXPECT(absentDependencies[1].dependencies()[0].inputProbeObservation() == zc::none);
  auto clonedProbeGroup = absentDependencies[0].clone();
  const auto clonedObservation = clonedProbeGroup.dependencies()[0].inputProbeObservation();
  ZC_REQUIRE(clonedObservation != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(clonedObservation) == InputProbeObservation::Absent);
  auto absentMetadata = absent.metadata<OptionalLowInputQuery>(5);
  ZC_REQUIRE(absentMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(absentMetadata).minimumDurability() == Durability::Low);

  auto unrelatedWrite = beginTransaction(database);
  ZC_REQUIRE(unrelatedWrite.set<LowInput>(99, 1).isApplied());
  ZC_REQUIRE(unrelatedWrite.commit().isCommitted());
  auto unrelated = database.snapshot();
  ZC_EXPECT(unrelated.get<OptionalLowInputQuery>(5).value() == 40);
  ZC_EXPECT(hasEvent(unrelated.events().asPtr(), QueryEventKind::GreenReused));

  auto presentWrite = beginTransaction(database);
  ZC_REQUIRE(presentWrite.set<LowInput>(5, 2).isApplied());
  ZC_REQUIRE(presentWrite.commit().isCommitted());
  auto present = database.snapshot();
  auto presentRootProbe = present.probeInput<LowInput>(5);
  ZC_REQUIRE(!presentRootProbe.isRuntimeFailure());
  ZC_EXPECT(presentRootProbe.value() == 2);
  ZC_EXPECT(present.get<OptionalLowInputQuery>(5).value() == 42);
  auto presentDependencies = present.dependencies<OptionalLowInputQuery>(5);
  ZC_REQUIRE(presentDependencies.size() == 2);
  const auto presentObservation = presentDependencies[0].dependencies()[0].inputProbeObservation();
  ZC_REQUIRE(presentObservation != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(presentObservation) == InputProbeObservation::Present);

  auto equalPresentWrite = beginTransaction(database);
  ZC_REQUIRE(equalPresentWrite.set<LowInput>(5, 2).isApplied());
  ZC_REQUIRE(equalPresentWrite.commit().isCommitted());
  auto equalPresent = database.snapshot();
  ZC_EXPECT(equalPresent.get<OptionalLowInputQuery>(5).value() == 42);
  ZC_EXPECT(hasEvent(equalPresent.events().asPtr(), QueryEventKind::GreenReused));

  auto changedWrite = beginTransaction(database);
  ZC_REQUIRE(changedWrite.set<LowInput>(5, 3).isApplied());
  ZC_REQUIRE(changedWrite.commit().isCommitted());
  auto changed = database.snapshot();
  ZC_EXPECT(changed.get<OptionalLowInputQuery>(5).value() == 43);

  auto removedWrite = beginTransaction(database);
  ZC_REQUIRE(removedWrite.erase<LowInput>(5).isApplied());
  ZC_REQUIRE(removedWrite.commit().isCommitted());
  auto removed = database.snapshot();
  ZC_EXPECT(removed.get<OptionalLowInputQuery>(5).value() == 40);
  ZC_EXPECT(!removed.hasRetainedValue<LowInput>(5));

  auto invalidRootProbe = removed.probeInput<AddTenQuery>(5);
  ZC_REQUIRE(invalidRootProbe.isRuntimeFailure());
  ZC_EXPECT(invalidRootProbe.runtimeFailure() == QueryRuntimeFailure::InvariantViolation);
  auto invalidProviderProbe = removed.get<InvalidDerivedInputProbeQuery>(5);
  ZC_REQUIRE(invalidProviderProbe.isRuntimeFailure());
  ZC_EXPECT(invalidProviderProbe.runtimeFailure() == QueryRuntimeFailure::InvariantViolation);

  auto malformedProbe = removed.probeInput<MalformedLowInputKey>(5);
  ZC_REQUIRE(malformedProbe.isRuntimeFailure());
  ZC_EXPECT(malformedProbe.runtimeFailure() == QueryRuntimeFailure::InvalidKeyEncoding);
  auto malformedRequiredRead = removed.get<MalformedLowInputKey>(5);
  ZC_REQUIRE(malformedRequiredRead.isRuntimeFailure());
  ZC_EXPECT(malformedRequiredRead.runtimeFailure() == QueryRuntimeFailure::MissingInput);
  auto malformedWrite = beginTransaction(database);
  auto malformedMutation = malformedWrite.set<MalformedLowInputKey>(5, 3);
  ZC_EXPECT(!malformedMutation.isApplied());
  ZC_EXPECT(malformedMutation.failure() == InputTransactionFailure::InvalidKeyEncoding);
  malformedWrite.abandon();

  CancellationSource cancelled;
  cancelled.cancel();
  auto cancelledProbe = removed.probeInput<LowInput>(5, cancelled.token());
  ZC_REQUIRE(cancelledProbe.isRuntimeFailure());
  ZC_EXPECT(cancelledProbe.runtimeFailure() == QueryRuntimeFailure::Cancelled);

  ZC_REQUIRE(removed.evictValue<OptionalLowInputQuery>(5));
  ZC_EXPECT(!removed.hasRetainedValue<OptionalLowInputQuery>(5));
  auto evictedDependencies = removed.dependencies<OptionalLowInputQuery>(5);
  ZC_REQUIRE(evictedDependencies.size() == 2);
  const auto evictedObservation = evictedDependencies[0].dependencies()[0].inputProbeObservation();
  ZC_REQUIRE(evictedObservation != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(evictedObservation) == InputProbeObservation::Absent);
}

ZC_TEST("QueryDatabaseTest.FinalSealIsOneShotRevisionNeutralAndIrreversible") {
  auto database = queryTestDatabase();
  registerFinalSealInput(database);
  auto snapshot = publishCompleteContext(database, 7, 7);
  const auto revision = snapshot.revision();
  auto result = database.sealInputs<CompleteContext>(snapshot, 7, repeatedDigest(7));
  ZC_REQUIRE(result.isSealed());
  ZC_EXPECT(result.seal().revision() == revision);
  ZC_EXPECT(database.snapshot().revision() == revision);

  auto repeated = database.sealInputs<CompleteContext>(snapshot, 7, repeatedDigest(7));
  ZC_REQUIRE(!repeated.isSealed());
  ZC_EXPECT(repeated.failure() == InputTransactionFailure::FinalSealAlreadyPublished);

  auto mutation = database.beginInputTransaction(revision);
  ZC_REQUIRE(!mutation.isOpened());
  ZC_EXPECT(mutation.failure() == InputTransactionFailure::InputMutationAfterFinalSeal);

  auto admitted = database.admitFinalSnapshot<CompleteContext>(database.snapshot(), result.seal());
  ZC_REQUIRE(admitted.isAdmitted());
  ZC_EXPECT(zc::mv(admitted).takeSnapshot().revision() == revision);
}

ZC_TEST("QueryDatabaseTest.FinalSealRejectsPhaseOneAndAuthorityFailuresExactly") {
  auto database = queryTestDatabase();
  registerFinalSealInput(database);
  auto stale = publishCompleteContext(database, 7, 7);
  auto nextWrite = beginTransaction(database);
  ZC_REQUIRE(nextWrite.set<LowInput>(1, 1).isApplied());
  ZC_REQUIRE(nextWrite.commit().isCommitted());

  auto staleResult = database.sealInputs<CompleteContext>(stale, 7, repeatedDigest(7));
  ZC_REQUIRE(!staleResult.isSealed());
  ZC_EXPECT(staleResult.failure() == InputTransactionFailure::StaleSnapshot);

  auto foreignDatabase = queryTestDatabase();
  registerFinalSealInput(foreignDatabase);
  auto foreign = publishCompleteContext(foreignDatabase, 7, 7);
  auto foreignResult = database.sealInputs<CompleteContext>(foreign, 7, repeatedDigest(7));
  ZC_REQUIRE(!foreignResult.isSealed());
  ZC_EXPECT(foreignResult.failure() == InputTransactionFailure::ForeignSnapshot);

  auto open = database.beginInputTransaction(database.snapshot().revision());
  ZC_REQUIRE(open.isOpened());
  auto openWrite = zc::mv(open).takeTransaction();
  auto openResult = database.sealInputs<CompleteContext>(database.snapshot(), 7, repeatedDigest(7));
  ZC_REQUIRE(!openResult.isSealed());
  ZC_EXPECT(openResult.failure() == InputTransactionFailure::OpenTransactionDuringFinalSeal);
  openWrite.abandon();

  auto invalidAuthority =
      database.sealInputs<CompleteContext>(database.snapshot(), 7, repeatedDigest(8));
  ZC_REQUIRE(!invalidAuthority.isSealed());
  ZC_EXPECT(invalidAuthority.failure() == InputTransactionFailure::InvalidFinalSealAuthority);
}

ZC_TEST("QueryDatabaseTest.FinalAdmissionRejectsForeignSnapshotAndSealCoordinates") {
  auto first = queryTestDatabase();
  auto second = queryTestDatabase();
  registerFinalSealInput(first);
  registerFinalSealInput(second);
  auto firstSnapshot = publishCompleteContext(first, 5, 5);
  auto secondSnapshot = publishCompleteContext(second, 5, 5);
  auto firstSeal = first.sealInputs<CompleteContext>(firstSnapshot, 5, repeatedDigest(5));
  ZC_REQUIRE(firstSeal.isSealed());

  auto foreignAdmission =
      first.admitFinalSnapshot<CompleteContext>(zc::mv(secondSnapshot), firstSeal.seal());
  ZC_REQUIRE(!foreignAdmission.isAdmitted());
  ZC_EXPECT(foreignAdmission.failure() == QueryRuntimeFailure::FinalSealMismatch);

  auto unsealedDatabase = queryTestDatabase();
  registerFinalSealInput(unsealedDatabase);
  auto unsealedSnapshot = publishCompleteContext(unsealedDatabase, 5, 5);
  auto missingAdmission = unsealedDatabase.admitFinalSnapshot<CompleteContext>(
      zc::mv(unsealedSnapshot), firstSeal.seal());
  ZC_REQUIRE(!missingAdmission.isAdmitted());
  ZC_EXPECT(missingAdmission.failure() == QueryRuntimeFailure::FinalSealRequired);
}

ZC_TEST("QueryDatabaseTest.FinalSealRaceReturnsStaleSnapshotAfterWinningCommit") {
  auto database = queryTestDatabase();
  registerFinalSealInput(database);
  auto snapshot = publishCompleteContext(database, 4, 4);
  QueryRuntimeTestAccess::armFinalSealPhaseTwoGate(database);
  zc::Maybe<CompleteContextSealResult> result;
  {
    zc::Thread sealing(
        [&]() { result = database.sealInputs<CompleteContext>(snapshot, 4, repeatedDigest(4)); });
    QueryRuntimeTestAccess::waitForFinalSealPhaseTwoGate(database);
    auto write = beginTransaction(database);
    ZC_REQUIRE(write.set<LowInput>(1, 1).isApplied());
    ZC_REQUIRE(write.commit().isCommitted());
    QueryRuntimeTestAccess::releaseFinalSealPhaseTwoGate(database);
  }
  ZC_REQUIRE(result != zc::none);
  ZC_REQUIRE(!ZC_REQUIRE_NONNULL(result).isSealed());
  ZC_EXPECT(ZC_REQUIRE_NONNULL(result).failure() == InputTransactionFailure::StaleSnapshot);
}

ZC_TEST("QueryDatabaseTest.FinalSealRaceReturnsOpenTransactionBeforeStaleness") {
  auto database = queryTestDatabase();
  registerFinalSealInput(database);
  auto snapshot = publishCompleteContext(database, 4, 4);
  QueryRuntimeTestAccess::armFinalSealPhaseTwoGate(database);
  zc::Maybe<CompleteContextSealResult> result;
  zc::Maybe<InputTransaction> openWrite;
  {
    zc::Thread sealing(
        [&]() { result = database.sealInputs<CompleteContext>(snapshot, 4, repeatedDigest(4)); });
    QueryRuntimeTestAccess::waitForFinalSealPhaseTwoGate(database);
    auto open = database.beginInputTransaction(snapshot.revision());
    ZC_REQUIRE(open.isOpened());
    openWrite = zc::mv(open).takeTransaction();
    QueryRuntimeTestAccess::releaseFinalSealPhaseTwoGate(database);
  }
  ZC_REQUIRE(result != zc::none);
  ZC_REQUIRE(!ZC_REQUIRE_NONNULL(result).isSealed());
  ZC_EXPECT(ZC_REQUIRE_NONNULL(result).failure() ==
            InputTransactionFailure::OpenTransactionDuringFinalSeal);
  ZC_REQUIRE(openWrite != zc::none);
  ZC_REQUIRE_NONNULL(openWrite).abandon();
}

ZC_TEST("QueryDatabaseTest.FinalSealRaceReturnsPublishedSealBeforeVerifierRejection") {
  auto database = queryTestDatabase();
  registerFinalSealInput(database);
  auto snapshot = publishCompleteContext(database, 6, 6);
  QueryRuntimeTestAccess::armFinalSealPhaseTwoGate(database);
  zc::Maybe<CompleteContextSealResult> firstResult;
  {
    zc::Thread first([&]() {
      firstResult = database.sealInputs<CompleteContext>(snapshot, 6, repeatedDigest(7));
    });
    QueryRuntimeTestAccess::waitForFinalSealPhaseTwoGate(database);
    auto winner = database.sealInputs<CompleteContext>(snapshot, 6, repeatedDigest(6));
    ZC_REQUIRE(winner.isSealed());
    QueryRuntimeTestAccess::releaseFinalSealPhaseTwoGate(database);
  }
  ZC_REQUIRE(firstResult != zc::none);
  ZC_REQUIRE(!ZC_REQUIRE_NONNULL(firstResult).isSealed());
  ZC_EXPECT(ZC_REQUIRE_NONNULL(firstResult).failure() ==
            InputTransactionFailure::FinalSealAlreadyPublished);
}

ZC_TEST("QueryDatabaseTest.FinalSealRacePrioritizesCommitOverVerifierRejection") {
  auto database = queryTestDatabase();
  registerFinalSealInput(database);
  auto snapshot = publishCompleteContext(database, 9, 8);
  QueryRuntimeTestAccess::armFinalSealPhaseTwoGate(database);
  zc::Maybe<CompleteContextSealResult> result;
  {
    zc::Thread sealing(
        [&]() { result = database.sealInputs<CompleteContext>(snapshot, 9, repeatedDigest(9)); });
    QueryRuntimeTestAccess::waitForFinalSealPhaseTwoGate(database);
    auto write = beginTransaction(database);
    ZC_REQUIRE(write.set<LowInput>(2, 2).isApplied());
    ZC_REQUIRE(write.commit().isCommitted());
    QueryRuntimeTestAccess::releaseFinalSealPhaseTwoGate(database);
  }
  ZC_REQUIRE(result != zc::none);
  ZC_REQUIRE(!ZC_REQUIRE_NONNULL(result).isSealed());
  ZC_EXPECT(ZC_REQUIRE_NONNULL(result).failure() == InputTransactionFailure::StaleSnapshot);
}

}  // namespace zomlang::compiler::query::test
