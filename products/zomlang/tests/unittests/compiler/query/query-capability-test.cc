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
#include "zc/ztest/test.h"

namespace zomlang::compiler::query::test {

class EmptySemanticContextResources final : public SemanticContextCapabilityResources {};

class TestSemanticContextResources final : public SemanticContextCapabilityResources {
public:
  explicit TestSemanticContextResources(zc::MutexGuarded<bool>& destroyed) noexcept
      : destroyedField(destroyed) {}
  ~TestSemanticContextResources() noexcept(false) override {
    *destroyedField.lockExclusive() = true;
  }

private:
  zc::MutexGuarded<bool>& destroyedField;
};

QueryDatabase capabilityTestDatabase() {
  auto resources = zc::heap<EmptySemanticContextResources>();
  auto arena = zc::arc<SemanticContextCapabilityArena>(zc::mv(resources));
  return QueryDatabase(queryTestScheduler(), queryTestDescriptorInventory(), zc::mv(arena));
}

void registerCapabilityKinds(QueryDatabase& database) {
  ZC_REQUIRE(database.registerDescriptor<LowInput>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<LeafCapabilityQuery>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<ParentCapabilityQuery>().isRegistered());
}

}  // namespace zomlang::compiler::query::test

namespace zomlang::compiler::query {

template <>
class CapabilityCandidateContract<test::LeafCapabilityQuery> final {
public:
  static StableWitnessBytes encode(const test::LeafCapability& candidate) {
    return StableWitnessBytes(test::encodeUint32(candidate.value()));
  }
  static zc::Maybe<zc::Own<test::LeafCapability>> decode(zc::ArrayPtr<const uint8_t> bytes) {
    auto value = test::decodeUint32(bytes);
    if (value == zc::none) { return zc::none; }
    return zc::heap<test::LeafCapability>(ZC_ASSERT_NONNULL(value), 0);
  }
};

template <>
class CapabilityCandidateContract<test::ParentCapabilityQuery> final {
public:
  static StableWitnessBytes encode(const test::ParentCapability& candidate) {
    return StableWitnessBytes(test::encodeUint32(candidate.value()));
  }
  static zc::Maybe<zc::Own<test::ParentCapability>> decode(zc::ArrayPtr<const uint8_t> bytes) {
    auto value = test::decodeUint32(bytes);
    if (value == zc::none) { return zc::none; }
    return zc::heap<test::ParentCapability>(ZC_ASSERT_NONNULL(value), 0);
  }
};

template <>
class CapabilityCandidateContract<test::SlowCapabilityQuery> final {
public:
  static StableWitnessBytes encode(const test::LeafCapability& candidate) {
    return StableWitnessBytes(test::encodeUint32(candidate.value()));
  }
  static zc::Maybe<zc::Own<test::LeafCapability>> decode(zc::ArrayPtr<const uint8_t> bytes) {
    auto value = test::decodeUint32(bytes);
    if (value == zc::none) { return zc::none; }
    return zc::heap<test::LeafCapability>(ZC_ASSERT_NONNULL(value), 0);
  }
};

template <>
class CapabilityCandidateContract<test::RejectedCapabilityQuery> final {
public:
  static StableWitnessBytes encode(const test::LeafCapability& candidate) {
    return StableWitnessBytes(test::encodeUint32(candidate.value()));
  }
  static zc::Maybe<zc::Own<test::LeafCapability>> decode(zc::ArrayPtr<const uint8_t> bytes) {
    auto value = test::decodeUint32(bytes);
    if (value == zc::none) { return zc::none; }
    return zc::heap<test::LeafCapability>(ZC_ASSERT_NONNULL(value), 0);
  }
};

template <>
class CapabilityCandidateContract<test::TerminalCapabilityQuery> final {
public:
  static StableWitnessBytes encode(const test::LeafCapability& candidate) {
    return StableWitnessBytes(test::encodeUint32(candidate.value()));
  }
  static zc::Maybe<zc::Own<test::LeafCapability>> decode(zc::ArrayPtr<const uint8_t> bytes) {
    auto value = test::decodeUint32(bytes);
    if (value == zc::none) { return zc::none; }
    return zc::heap<test::LeafCapability>(ZC_ASSERT_NONNULL(value), 0);
  }
};

template <>
class CapabilityCandidateContract<test::FinalSealedCapabilityQuery> final {
public:
  static StableWitnessBytes encode(const test::LeafCapability& candidate) {
    return StableWitnessBytes(test::encodeUint32(candidate.value()));
  }
  static zc::Maybe<zc::Own<test::LeafCapability>> decode(zc::ArrayPtr<const uint8_t> bytes) {
    auto value = test::decodeUint32(bytes);
    if (value == zc::none) { return zc::none; }
    return zc::heap<test::LeafCapability>(ZC_ASSERT_NONNULL(value), 0);
  }
};

template <>
class CapabilityCandidateContract<test::FinalSealedParentCapabilityQuery> final {
public:
  static StableWitnessBytes encode(const test::ParentCapability& candidate) {
    return StableWitnessBytes(test::encodeUint32(candidate.value()));
  }
  static zc::Maybe<zc::Own<test::ParentCapability>> decode(zc::ArrayPtr<const uint8_t> bytes) {
    auto value = test::decodeUint32(bytes);
    if (value == zc::none) { return zc::none; }
    return zc::heap<test::ParentCapability>(ZC_ASSERT_NONNULL(value), 0);
  }
};

template <>
class CapabilityFailureContract<test::TerminalCapabilityQuery, KeyRejection<uint32_t>> final {
public:
  static zc::Array<uint8_t> encode(const uint32_t& failure) { return test::encodeUint32(failure); }
  static zc::Maybe<uint32_t> decode(zc::ArrayPtr<const uint8_t> bytes) {
    return test::decodeUint32(bytes);
  }
  static CapabilityRejectionCheck verify(
      CapabilityQueryContext<test::TerminalCapabilityQuery>& context,
      const test::TerminalCapabilityQuery::Key& key, const uint32_t& failure) {
    auto input = context.probeInput<test::LowInput>(key);
    return key == failure && !input.isRuntimeFailure() && input.kind() == QueryValueKind::Absence
               ? CapabilityRejectionCheck::Verified
               : CapabilityRejectionCheck::Rejected;
  }
};

}  // namespace zomlang::compiler::query

namespace zomlang::compiler::query::test {

CapabilityProviderResult<LeafCapabilityQuery> LeafCapabilityQuery::provide(
    CapabilityQueryContext<LeafCapabilityQuery>& context, const Key& key) {
  auto input = context.get<LowInput>(key);
  if (input.isRuntimeFailure()) {
    return CapabilityProviderResult<LeafCapabilityQuery>::runtimeRejected(input.runtimeFailure());
  }
  auto generation = context.probeInput<LowInput>(capabilityGenerationInputKey(key));
  if (generation.isRuntimeFailure()) {
    return CapabilityProviderResult<LeafCapabilityQuery>::runtimeRejected(
        generation.runtimeFailure());
  }
  const uint32_t generationValue =
      generation.kind() == QueryValueKind::Value ? generation.value() : 0;
  auto candidate = zc::heap<Capability>(input.value(), generationValue);
  auto witness = CapabilityCandidateContract<LeafCapabilityQuery>::encode(*candidate);
  return CapabilityProviderResult<LeafCapabilityQuery>::candidate(zc::mv(candidate),
                                                                  zc::mv(witness));
}

zc::Maybe<zc::Array<uint8_t>> LeafCapabilityQuery::verify(
    CapabilityQueryContext<LeafCapabilityQuery>&, const Key&, const Capability& candidate) {
  return encodeUint32(candidate.value());
}

CapabilityProviderResult<ParentCapabilityQuery> ParentCapabilityQuery::provide(
    CapabilityQueryContext<ParentCapabilityQuery>& context, const Key& key) {
  auto dependency = context.getCapability<LeafCapabilityQuery>(key);
  if (dependency.isRuntimeRejected()) {
    return CapabilityProviderResult<ParentCapabilityQuery>::runtimeRejected(
        dependency.runtimeFailure());
  }
  if (!dependency.isPublished()) {
    return CapabilityProviderResult<ParentCapabilityQuery>::runtimeRejected(
        QueryRuntimeFailure::InvariantViolation);
  }
  const auto& child = dependency.lease().capability();
  auto candidate = zc::heap<Capability>(child.value(), child.generation());
  auto witness = CapabilityCandidateContract<ParentCapabilityQuery>::encode(*candidate);
  return CapabilityProviderResult<ParentCapabilityQuery>::candidate(zc::mv(candidate),
                                                                    zc::mv(witness));
}

zc::Maybe<zc::Array<uint8_t>> ParentCapabilityQuery::verify(
    CapabilityQueryContext<ParentCapabilityQuery>&, const Key&, const Capability& candidate) {
  return encodeUint32(candidate.value());
}

CapabilityProviderResult<SlowCapabilityQuery> SlowCapabilityQuery::provide(
    CapabilityQueryContext<SlowCapabilityQuery>& context, const Key& key) {
  usleep(20000);
  auto input = context.get<LowInput>(key);
  if (input.isRuntimeFailure()) {
    return CapabilityProviderResult<SlowCapabilityQuery>::runtimeRejected(input.runtimeFailure());
  }
  auto candidate = zc::heap<Capability>(input.value(), 0);
  auto witness = CapabilityCandidateContract<SlowCapabilityQuery>::encode(*candidate);
  return CapabilityProviderResult<SlowCapabilityQuery>::candidate(zc::mv(candidate),
                                                                  zc::mv(witness));
}

zc::Maybe<zc::Array<uint8_t>> SlowCapabilityQuery::verify(
    CapabilityQueryContext<SlowCapabilityQuery>&, const Key&, const Capability& candidate) {
  return encodeUint32(candidate.value());
}

CapabilityProviderResult<RejectedCapabilityQuery> RejectedCapabilityQuery::provide(
    CapabilityQueryContext<RejectedCapabilityQuery>& context, const Key& key) {
  auto input = context.get<LowInput>(key);
  if (input.isRuntimeFailure()) {
    return CapabilityProviderResult<RejectedCapabilityQuery>::runtimeRejected(
        input.runtimeFailure());
  }
  auto candidate = zc::heap<Capability>(input.value(), 0);
  auto witness = CapabilityCandidateContract<RejectedCapabilityQuery>::encode(*candidate);
  return CapabilityProviderResult<RejectedCapabilityQuery>::candidate(zc::mv(candidate),
                                                                      zc::mv(witness));
}

zc::Maybe<zc::Array<uint8_t>> RejectedCapabilityQuery::verify(
    CapabilityQueryContext<RejectedCapabilityQuery>&, const Key&, const Capability& candidate) {
  return encodeUint32(candidate.value() + 1);
}

CapabilityProviderResult<TerminalCapabilityQuery> TerminalCapabilityQuery::provide(
    CapabilityQueryContext<TerminalCapabilityQuery>& context, const Key& key) {
  auto input = context.probeInput<LowInput>(key);
  if (input.isRuntimeFailure()) {
    return CapabilityProviderResult<TerminalCapabilityQuery>::runtimeRejected(
        input.runtimeFailure());
  }
  if (input.kind() == QueryValueKind::Absence) {
    return CapabilityProviderResult<TerminalCapabilityQuery>::keyRejected(uint32_t{key});
  }
  auto candidate = zc::heap<Capability>(input.value(), 0);
  auto witness = CapabilityCandidateContract<TerminalCapabilityQuery>::encode(*candidate);
  return CapabilityProviderResult<TerminalCapabilityQuery>::candidate(zc::mv(candidate),
                                                                      zc::mv(witness));
}

zc::Maybe<zc::Array<uint8_t>> TerminalCapabilityQuery::verify(
    CapabilityQueryContext<TerminalCapabilityQuery>& context, const Key& key,
    const Capability& candidate) {
  auto input = context.probeInput<LowInput>(key);
  if (input.isRuntimeFailure() || input.kind() != QueryValueKind::Value ||
      input.value() != candidate.value()) {
    return zc::none;
  }
  return encodeUint32(candidate.value());
}

TypedQueryResult<CapabilityRejectionProjectionQuery::Value>
CapabilityRejectionProjectionQuery::provide(QueryContext& context, const Key& key) {
  auto capability = context.getCapability<TerminalCapabilityQuery>(key);
  if (capability.isRuntimeRejected()) {
    return TypedQueryResult<Value>::runtimeFailure(capability.runtimeFailure());
  }
  if (capability.isKeyRejected()) {
    return TypedQueryResult<Value>::semanticFailure(encodeUint32(capability.keyFailure()));
  }
  if (!capability.isPublished()) {
    return TypedQueryResult<Value>::runtimeFailure(QueryRuntimeFailure::InvariantViolation);
  }
  return TypedQueryResult<Value>::value(capability.lease().capability().value());
}

bool CapabilityRejectionProjectionQuery::verify(QueryContext& context, const Key& key,
                                                const TypedQueryResult<Value>& result) {
  auto capability = context.getCapability<TerminalCapabilityQuery>(key);
  if (capability.isKeyRejected()) {
    return result.kind() == QueryValueKind::SemanticFailure &&
           result.semanticFailureBytes() == encodeUint32(capability.keyFailure()).asPtr();
  }
  return capability.isPublished() && result.kind() == QueryValueKind::Value &&
         result.value() == capability.lease().capability().value();
}

CapabilityProviderResult<FinalSealedCapabilityQuery> FinalSealedCapabilityQuery::provide(
    CapabilityQueryContext<FinalSealedCapabilityQuery>& context, const Key& key) {
  auto input = context.get<LowInput>(key);
  if (input.isRuntimeFailure()) {
    return CapabilityProviderResult<FinalSealedCapabilityQuery>::runtimeRejected(
        input.runtimeFailure());
  }
  auto candidate = zc::heap<Capability>(input.value(), 0);
  auto witness = CapabilityCandidateContract<FinalSealedCapabilityQuery>::encode(*candidate);
  return CapabilityProviderResult<FinalSealedCapabilityQuery>::candidate(zc::mv(candidate),
                                                                         zc::mv(witness));
}

zc::Maybe<zc::Array<uint8_t>> FinalSealedCapabilityQuery::verify(
    CapabilityQueryContext<FinalSealedCapabilityQuery>&, const Key&, const Capability& candidate) {
  return encodeUint32(candidate.value());
}

CapabilityProviderResult<FinalSealedParentCapabilityQuery>
FinalSealedParentCapabilityQuery::provide(
    CapabilityQueryContext<FinalSealedParentCapabilityQuery>& context, const Key& key) {
  auto dependency = context.getCapability<FinalSealedCapabilityQuery>(key);
  if (dependency.isRuntimeRejected()) {
    return CapabilityProviderResult<FinalSealedParentCapabilityQuery>::runtimeRejected(
        dependency.runtimeFailure());
  }
  if (!dependency.isPublished()) {
    return CapabilityProviderResult<FinalSealedParentCapabilityQuery>::runtimeRejected(
        QueryRuntimeFailure::InvariantViolation);
  }
  auto candidate = zc::heap<Capability>(dependency.lease().capability().value(),
                                        dependency.lease().capability().generation());
  auto witness = CapabilityCandidateContract<FinalSealedParentCapabilityQuery>::encode(*candidate);
  return CapabilityProviderResult<FinalSealedParentCapabilityQuery>::candidate(zc::mv(candidate),
                                                                               zc::mv(witness));
}

zc::Maybe<zc::Array<uint8_t>> FinalSealedParentCapabilityQuery::verify(
    CapabilityQueryContext<FinalSealedParentCapabilityQuery>&, const Key&,
    const Capability& candidate) {
  return encodeUint32(candidate.value());
}

ZC_TEST("QueryCapabilityTest.SameSnapshotReturnsOneRetainedMemoGeneration") {
  auto database = capabilityTestDatabase();
  registerCapabilityKinds(database);
  auto write = beginTransaction(database);
  ZC_REQUIRE(write.set<LowInput>(7, 70).isApplied());
  ZC_REQUIRE(write.commit().isCommitted());
  auto snapshot = database.snapshot();

  auto first = snapshot.getCapability<LeafCapabilityQuery>(7);
  auto second = snapshot.getCapability<LeafCapabilityQuery>(7);
  ZC_REQUIRE(first.isPublished());
  ZC_REQUIRE(second.isPublished());
  ZC_EXPECT(first.lease().capability().value() == 70);
  ZC_EXPECT(first.lease().capability().generation() == second.lease().capability().generation());
  ZC_EXPECT(first.lease().revision() == DatabaseRevision(1));
  ZC_EXPECT(first.lease().arenaRevision() == DatabaseRevision(1));
  ZC_EXPECT(first.lease().stableWitness() == encodeUint32(70).asPtr());
  ZC_EXPECT(snapshot.hasRetainedValue<LeafCapabilityQuery>(7));
  ZC_EXPECT(!snapshot.evictValue<LeafCapabilityQuery>(7));

  size_t executed = 0;
  for (const auto& event : snapshot.events()) {
    if (event.kind() == QueryEventKind::Executed) { ++executed; }
  }
  ZC_EXPECT(executed == 1);
}

ZC_TEST("QueryCapabilityTest.NewRevisionCreatesDistinctGenerationWithoutBackdating") {
  auto database = capabilityTestDatabase();
  registerCapabilityKinds(database);
  auto firstWrite = beginTransaction(database);
  ZC_REQUIRE(firstWrite.set<LowInput>(3, 30).isApplied());
  ZC_REQUIRE(firstWrite.set<LowInput>(capabilityGenerationInputKey(3), 1).isApplied());
  ZC_REQUIRE(firstWrite.commit().isCommitted());
  auto firstSnapshot = database.snapshot();
  auto first = firstSnapshot.getCapability<LeafCapabilityQuery>(3);
  ZC_REQUIRE(first.isPublished());
  auto firstLease = first.lease().retain();

  auto secondWrite = beginTransaction(database);
  ZC_REQUIRE(secondWrite.set<LowInput>(3, 30).isApplied());
  ZC_REQUIRE(secondWrite.set<LowInput>(capabilityGenerationInputKey(3), 2).isApplied());
  ZC_REQUIRE(secondWrite.commit().isCommitted());
  auto secondSnapshot = database.snapshot();
  ZC_EXPECT(!secondSnapshot.hasRetainedValue<LeafCapabilityQuery>(3));
  auto second = secondSnapshot.getCapability<LeafCapabilityQuery>(3);
  ZC_REQUIRE(second.isPublished());
  ZC_EXPECT(second.lease().revision() == DatabaseRevision(2));
  ZC_EXPECT(firstLease.revision() == DatabaseRevision(1));
  ZC_EXPECT(firstLease.capability().generation() != second.lease().capability().generation());
  ZC_EXPECT(firstLease.stableWitness() == second.lease().stableWitness());
}

ZC_TEST("QueryCapabilityTest.ConcurrentDemandsJoinOneCapabilityFlight") {
  auto database = capabilityTestDatabase();
  ZC_REQUIRE(database.registerDescriptor<LowInput>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<SlowCapabilityQuery>().isRegistered());
  auto write = beginTransaction(database);
  ZC_REQUIRE(write.set<LowInput>(8, 80).isApplied());
  ZC_REQUIRE(write.commit().isCommitted());
  auto snapshot = database.snapshot();
  zc::MutexGuarded<zc::Vector<uint32_t>> generations;

  {
    zc::Thread first([&]() {
      auto result = snapshot.getCapability<SlowCapabilityQuery>(8);
      if (result.isPublished()) {
        generations.lockExclusive()->add(result.lease().capability().generation());
      }
    });
    zc::Thread second([&]() {
      auto result = snapshot.getCapability<SlowCapabilityQuery>(8);
      if (result.isPublished()) {
        generations.lockExclusive()->add(result.lease().capability().generation());
      }
    });
  }

  auto retained = generations.lockShared();
  ZC_REQUIRE(retained->size() == 2);
  ZC_EXPECT((*retained)[0] == (*retained)[1]);
  retained.release();
  ZC_EXPECT(hasEvent(snapshot.events().asPtr(), QueryEventKind::SingleFlightJoined));
}

ZC_TEST("QueryCapabilityTest.ParentMemoRetainsCapabilityDependencyTransitively") {
  zc::Maybe<QueryCapabilityLease<const ParentCapability>> survivingLease;
  {
    auto database = capabilityTestDatabase();
    registerCapabilityKinds(database);
    auto write = beginTransaction(database);
    ZC_REQUIRE(write.set<LowInput>(11, 110).isApplied());
    ZC_REQUIRE(write.commit().isCommitted());
    auto snapshot = database.snapshot();

    auto parent = snapshot.getCapability<ParentCapabilityQuery>(11);
    ZC_REQUIRE(parent.isPublished());
    ZC_EXPECT(parent.lease().retainedDependencyCount() == 1);
    auto dependencies = snapshot.dependencies<ParentCapabilityQuery>(11);
    ZC_REQUIRE(dependencies.size() == 1);
    ZC_REQUIRE(dependencies[0].dependencies().size() == 1);
    auto witness = dependencies[0].dependencies()[0].stableWitness();
    ZC_REQUIRE(witness != zc::none);
    ZC_EXPECT(ZC_REQUIRE_NONNULL(witness) == encodeUint32(110).asPtr());
    survivingLease = parent.lease().retain();
  }

  ZC_REQUIRE(survivingLease != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(survivingLease).capability().value() == 110);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(survivingLease).retainedDependencyCount() == 1);
}

ZC_TEST("QueryCapabilityTest.SurvivingLeaseRetainsSessionAndSnapshotArenas") {
  zc::MutexGuarded<bool> resourcesDestroyed(false);
  zc::Maybe<QueryCapabilityLease<const LeafCapability>> survivingLease;
  {
    auto resources = zc::heap<TestSemanticContextResources>(resourcesDestroyed);
    auto arena = zc::arc<SemanticContextCapabilityArena>(zc::mv(resources));
    QueryDatabase database(queryTestScheduler(), queryTestDescriptorInventory(), zc::mv(arena));
    ZC_REQUIRE(database.registerDescriptor<LowInput>().isRegistered());
    ZC_REQUIRE(database.registerDescriptor<LeafCapabilityQuery>().isRegistered());
    auto write = beginTransaction(database);
    ZC_REQUIRE(write.set<LowInput>(12, 120).isApplied());
    ZC_REQUIRE(write.commit().isCommitted());
    auto snapshot = database.snapshot();
    auto result = snapshot.getCapability<LeafCapabilityQuery>(12);
    ZC_REQUIRE(result.isPublished());
    survivingLease = result.lease().retain();
  }

  ZC_EXPECT(!*resourcesDestroyed.lockShared());
  survivingLease = zc::none;
  ZC_EXPECT(*resourcesDestroyed.lockShared());
}

ZC_TEST("QueryCapabilityTest.VerifierMismatchAndTypedKeyRejectionPublishExactly") {
  auto database = capabilityTestDatabase();
  ZC_REQUIRE(database.registerDescriptor<LowInput>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<RejectedCapabilityQuery>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<TerminalCapabilityQuery>().isRegistered());
  auto write = beginTransaction(database);
  ZC_REQUIRE(write.set<LowInput>(5, 50).isApplied());
  ZC_REQUIRE(write.commit().isCommitted());
  auto snapshot = database.snapshot();

  auto rejected = snapshot.getCapability<RejectedCapabilityQuery>(5);
  ZC_REQUIRE(rejected.isRuntimeRejected());
  ZC_EXPECT(rejected.runtimeFailure() == QueryRuntimeFailure::VerifierRejected);
  ZC_EXPECT(snapshot.metadata<RejectedCapabilityQuery>(5) == zc::none);

  auto terminal = snapshot.getCapability<TerminalCapabilityQuery>(9);
  ZC_REQUIRE(terminal.isKeyRejected());
  ZC_EXPECT(terminal.keyFailure() == 9);
}

ZC_TEST("QueryCapabilityTest.SemanticParentInvalidatesAfterCapabilityRejection") {
  auto database = capabilityTestDatabase();
  ZC_REQUIRE(database.registerDescriptor<LowInput>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<TerminalCapabilityQuery>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<CapabilityRejectionProjectionQuery>().isRegistered());

  auto rejectedSnapshot = database.snapshot();
  auto rejected = rejectedSnapshot.get<CapabilityRejectionProjectionQuery>(9);
  ZC_REQUIRE(rejected.kind() == QueryValueKind::SemanticFailure);
  auto dependencies = rejectedSnapshot.dependencies<CapabilityRejectionProjectionQuery>(9);
  ZC_REQUIRE(dependencies.size() == 2);
  for (const auto& group : dependencies) {
    ZC_REQUIRE(group.dependencies().size() == 1);
    ZC_EXPECT(group.dependencies()[0].key().fingerprint() ==
              ZC_REQUIRE_NONNULL(rejectedSnapshot.keyFingerprint<TerminalCapabilityQuery>(9)));
  }

  auto write = beginTransaction(database);
  ZC_REQUIRE(write.set<LowInput>(9, 90).isApplied());
  ZC_REQUIRE(write.commit().isCommitted());
  auto recovered = database.snapshot().get<CapabilityRejectionProjectionQuery>(9);
  ZC_REQUIRE(recovered.kind() == QueryValueKind::Value);
  ZC_EXPECT(recovered.value() == 90);
}

ZC_TEST("QueryCapabilityTest.FinalSealedCapabilityRequiresMatchingAdmission") {
  using CompleteContext = driver::module_graph_query::CompleteCompilationContextAuthorityInput;

  auto database = capabilityTestDatabase();
  ZC_REQUIRE(database.registerDescriptor<LowInput>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<CompleteContext>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<FinalSealedCapabilityQuery>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<FinalSealedParentCapabilityQuery>().isRegistered());
  auto write = beginTransaction(database);
  ZC_REQUIRE(write.set<LowInput>(7, 70).isApplied());
  ZC_REQUIRE(write.set<CompleteContext>(7, 7).isApplied());
  ZC_REQUIRE(write.commit().isCommitted());
  auto ordinary = database.snapshot();

  auto rejected = ordinary.getCapability<FinalSealedCapabilityQuery>(7);
  ZC_REQUIRE(rejected.isRuntimeRejected());
  ZC_EXPECT(rejected.runtimeFailure() == QueryRuntimeFailure::FinalSealRequired);

  uint8_t bytes[32];
  for (auto& byte : bytes) { byte = 7; }
  auto witness = identity::Sha256Digest::fromBytes(zc::arrayPtr(bytes));
  ZC_REQUIRE(witness != zc::none);
  auto seal = database.sealInputs<CompleteContext>(ordinary, 7, ZC_REQUIRE_NONNULL(witness));
  ZC_REQUIRE(seal.isSealed());
  auto admitted = database.admitFinalSnapshot<CompleteContext>(database.snapshot(), seal.seal());
  ZC_REQUIRE(admitted.isAdmitted());
  auto sealed = zc::mv(admitted).takeSnapshot();
  auto published = sealed.getCapability<FinalSealedCapabilityQuery>(7);
  ZC_REQUIRE(published.isPublished());
  ZC_EXPECT(published.lease().capability().value() == 70);
  auto nested = sealed.getCapability<FinalSealedParentCapabilityQuery>(7);
  ZC_REQUIRE(nested.isPublished());
  ZC_EXPECT(nested.lease().capability().value() == 70);
}

ZC_TEST("QueryCapabilityTest.RealErasedResultsRejectForeignCoordinates") {
  auto firstDatabase = capabilityTestDatabase();
  auto secondDatabase = capabilityTestDatabase();
  for (auto* database : {&firstDatabase, &secondDatabase}) {
    ZC_REQUIRE(database->registerDescriptor<LowInput>().isRegistered());
    ZC_REQUIRE(database->registerDescriptor<LeafCapabilityQuery>().isRegistered());
    ZC_REQUIRE(database->registerDescriptor<SlowCapabilityQuery>().isRegistered());
    auto write = beginTransaction(*database);
    ZC_REQUIRE(write.set<LowInput>(3, 30).isApplied());
    ZC_REQUIRE(write.commit().isCommitted());
  }
  auto firstSnapshot = firstDatabase.snapshot();
  auto secondSnapshot = secondDatabase.snapshot();

  auto foreignDatabase = QueryRuntimeTestAccess::evaluate<LeafCapabilityQuery>(firstSnapshot, 3);
  auto foreignDatabaseResult =
      QueryRuntimeTestAccess::decode<LeafCapabilityQuery>(zc::mv(foreignDatabase), secondSnapshot);
  ZC_REQUIRE(foreignDatabaseResult.isRuntimeRejected());
  ZC_EXPECT(foreignDatabaseResult.runtimeFailure() == QueryRuntimeFailure::InvariantViolation);

  auto wrongDescriptor = QueryRuntimeTestAccess::evaluate<LeafCapabilityQuery>(firstSnapshot, 3);
  auto wrongDescriptorResult =
      QueryRuntimeTestAccess::decode<SlowCapabilityQuery>(zc::mv(wrongDescriptor), firstSnapshot);
  ZC_REQUIRE(wrongDescriptorResult.isRuntimeRejected());
  ZC_EXPECT(wrongDescriptorResult.runtimeFailure() == QueryRuntimeFailure::InvariantViolation);

  auto staleRevision = QueryRuntimeTestAccess::evaluate<LeafCapabilityQuery>(firstSnapshot, 3);
  auto nextWrite = beginTransaction(firstDatabase);
  ZC_REQUIRE(nextWrite.set<LowInput>(3, 31).isApplied());
  ZC_REQUIRE(nextWrite.commit().isCommitted());
  auto nextSnapshot = firstDatabase.snapshot();
  auto staleRevisionResult =
      QueryRuntimeTestAccess::decode<LeafCapabilityQuery>(zc::mv(staleRevision), nextSnapshot);
  ZC_REQUIRE(staleRevisionResult.isRuntimeRejected());
  ZC_EXPECT(staleRevisionResult.runtimeFailure() == QueryRuntimeFailure::InvariantViolation);

  auto original = firstSnapshot.getCapability<LeafCapabilityQuery>(3);
  ZC_REQUIRE(original.isPublished());
  ZC_EXPECT(original.lease().capability().value() == 30);
}

}  // namespace zomlang::compiler::query::test
