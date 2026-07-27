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

struct TestActiveKey final {
  uint32_t value;
};

class TestActiveResources final : public SemanticContextCapabilityResources {
public:
  ZC_NODISCARD uint32_t materialize(uint32_t key) const {
    auto locked = materialized.lockExclusive();
    for (const auto existing : *locked) {
      if (existing == key) { return existing; }
    }
    locked->add(key);
    return key;
  }

private:
  mutable zc::MutexGuarded<zc::Vector<uint32_t>> materialized;
};

class MaterializedTestCapability final {
public:
  explicit MaterializedTestCapability(uint32_t handle) noexcept : handleField(handle) {}

  ZC_NODISCARD uint32_t handle() const noexcept { return handleField; }

private:
  uint32_t handleField;
};

struct MaterializingCapabilityQuery {
  using Key = TestActiveKey;
  using Capability = MaterializedTestCapability;

  static zc::StringPtr domain() { return "test.capability.materialize-active"_zc; }
  static QueryKindContract contract() {
    return derivedContract(domain(), ReuseClass::RevisionLocal, RetentionClass::Retained);
  }
  static zc::Array<uint8_t> encodeKey(const Key& key) { return encodeUint32(key.value); }
  static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes) {
    auto decoded = decodeUint32(bytes);
    if (decoded == zc::none) { return zc::none; }
    return TestActiveKey{ZC_ASSERT_NONNULL(decoded)};
  }
  static CapabilityProviderResult<Capability> provide(CapabilityQueryContext& context,
                                                      const Key& key);
  static zc::Maybe<zc::Array<uint8_t>> verify(CapabilityQueryContext&, const Key& key,
                                              const Capability& candidate);
};

}  // namespace zomlang::compiler::query::test

namespace zomlang::compiler::query {

template <>
struct ActiveMaterialization<test::TestActiveKey> final {
  using Handle = uint32_t;

  ZC_NODISCARD static TypedQueryResult<Handle> materialize(
      const SemanticContextCapabilityResources& resources, const test::TestActiveKey& key) {
    if (typeid(resources) != typeid(test::TestActiveResources)) {
      return TypedQueryResult<Handle>::runtimeFailure(QueryRuntimeFailure::InvariantViolation);
    }
    const auto& typed = static_cast<const test::TestActiveResources&>(resources);
    return TypedQueryResult<Handle>::value(typed.materialize(key.value));
  }
};

template <>
struct ActiveMembership<test::TestActiveKey> final {
  ZC_NODISCARD static TypedQueryResult<bool> demand(QueryContext& context,
                                                    const test::TestActiveKey& key) {
    auto membership = context.get<test::LowInput>(key.value);
    if (membership.isRuntimeFailure()) {
      return TypedQueryResult<bool>::runtimeFailure(membership.runtimeFailure());
    }
    return TypedQueryResult<bool>::value(membership.kind() == QueryValueKind::Value &&
                                         membership.value() == key.value);
  }
};

template <>
struct ActiveMaterializerPermission<test::MaterializingCapabilityQuery> final {
  static constexpr bool allowed = true;
};

}  // namespace zomlang::compiler::query

namespace zomlang::compiler::query::test {

template <typename Context>
concept HasActiveMaterialization =
    requires(Context& context, const TestActiveKey& key) { context.materializeActive(key); };

static_assert(HasActiveMaterialization<CapabilityQueryContext>);
static_assert(!HasActiveMaterialization<QueryContext>);

CapabilityProviderResult<MaterializingCapabilityQuery::Capability>
MaterializingCapabilityQuery::provide(CapabilityQueryContext& context, const Key& key) {
  auto handle = context.materializeActive(key);
  if (handle.isRuntimeFailure()) {
    return CapabilityProviderResult<Capability>::runtimeFailure(handle.runtimeFailure());
  }
  if (handle.kind() != QueryValueKind::Value) {
    return CapabilityProviderResult<Capability>::absence();
  }
  return CapabilityProviderResult<Capability>::value(zc::heap<Capability>(handle.value()),
                                                     encodeUint32(handle.value()));
}

zc::Maybe<zc::Array<uint8_t>> MaterializingCapabilityQuery::verify(CapabilityQueryContext&,
                                                                   const Key& key,
                                                                   const Capability& candidate) {
  if (candidate.handle() != key.value) { return zc::none; }
  return encodeUint32(candidate.handle());
}

namespace {

struct UnpermittedMaterializingCapabilityQuery final : MaterializingCapabilityQuery {
  static zc::StringPtr domain() { return "test.capability.unpermitted-materializer"_zc; }
  static QueryKindContract contract() {
    return derivedContract(domain(), ReuseClass::RevisionLocal, RetentionClass::Retained);
  }
};

constexpr uint32_t CAPABILITY_GENERATION_INPUT_MASK = uint32_t{1} << 31;

uint32_t capabilityGenerationInputKey(uint32_t key) {
  return key ^ CAPABILITY_GENERATION_INPUT_MASK;
}

class LeafCapability final {
public:
  LeafCapability(uint32_t value, uint32_t generation)
      : valuesField(zc::heapArray<uint32_t>(1)), generationField(generation) {
    valuesField[0] = value;
  }

  ZC_NODISCARD zc::ArrayPtr<const uint32_t> values() const ZC_LIFETIMEBOUND {
    return valuesField.asPtr();
  }
  ZC_NODISCARD uint32_t generation() const noexcept { return generationField; }

private:
  zc::Array<uint32_t> valuesField;
  uint32_t generationField;
};

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

class EmptySemanticContextResources final : public SemanticContextCapabilityResources {};

QueryDatabase capabilityTestDatabase() {
  auto resources = zc::heap<EmptySemanticContextResources>();
  auto arena = zc::arc<SemanticContextCapabilityArena>(zc::mv(resources));
  return QueryDatabase(queryTestScheduler(), zc::mv(arena));
}

struct LeafCapabilityQuery {
  using Key = uint32_t;
  using Capability = LeafCapability;

  static zc::StringPtr domain() { return "test.capability.leaf"_zc; }
  static QueryKindContract contract() {
    return derivedContract(domain(), ReuseClass::RevisionLocal, RetentionClass::Retained);
  }
  static zc::Array<uint8_t> encodeKey(const Key& key) { return encodeUint32(key); }
  static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes) { return decodeUint32(bytes); }
  static CapabilityProviderResult<Capability> provide(CapabilityQueryContext& context,
                                                      const Key& key) {
    auto input = context.get<LowInput>(key);
    if (input.isRuntimeFailure()) {
      return CapabilityProviderResult<Capability>::runtimeFailure(input.runtimeFailure());
    }
    auto generation = context.probeInput<LowInput>(capabilityGenerationInputKey(key));
    if (generation.isRuntimeFailure()) {
      return CapabilityProviderResult<Capability>::runtimeFailure(generation.runtimeFailure());
    }
    const auto generationValue =
        generation.kind() == QueryValueKind::Value ? generation.value() : uint32_t{0};
    return CapabilityProviderResult<Capability>::value(
        zc::heap<Capability>(input.value(), generationValue), encodeUint32(input.value()));
  }
  static zc::Maybe<zc::Array<uint8_t>> verify(CapabilityQueryContext&, const Key&,
                                              const Capability& candidate) {
    return encodeUint32(candidate.values()[0]);
  }
};

class OtherCapability final {};

struct MismatchedCapabilityDemand {
  using Key = uint32_t;
  using Capability = OtherCapability;

  static zc::StringPtr domain() { return LeafCapabilityQuery::domain(); }
  static zc::Array<uint8_t> encodeKey(const Key& key) { return encodeUint32(key); }
};

class ParentCapability final {
public:
  ParentCapability(zc::ArrayPtr<const uint32_t> borrowedValues, uint32_t generation) noexcept
      : borrowedValuesField(borrowedValues), generationField(generation) {}

  ZC_NODISCARD uint32_t value() const noexcept { return borrowedValuesField[0]; }
  ZC_NODISCARD uint32_t generation() const noexcept { return generationField; }

private:
  zc::ArrayPtr<const uint32_t> borrowedValuesField;
  uint32_t generationField;
};

struct ParentCapabilityQuery {
  using Key = uint32_t;
  using Capability = ParentCapability;

  static zc::StringPtr domain() { return "test.capability.parent"_zc; }
  static QueryKindContract contract() {
    return derivedContract(domain(), ReuseClass::RevisionLocal, RetentionClass::Retained);
  }
  static zc::Array<uint8_t> encodeKey(const Key& key) { return encodeUint32(key); }
  static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes) { return decodeUint32(bytes); }
  static CapabilityProviderResult<Capability> provide(CapabilityQueryContext& context,
                                                      const Key& key) {
    auto dependency = context.getCapability<LeafCapabilityQuery>(key);
    if (dependency.isRuntimeFailure()) {
      return CapabilityProviderResult<Capability>::runtimeFailure(dependency.runtimeFailure());
    }
    if (dependency.kind() != QueryValueKind::Value) {
      return CapabilityProviderResult<Capability>::absence();
    }
    const auto values = dependency.value().capability().values();
    return CapabilityProviderResult<Capability>::value(
        zc::heap<Capability>(values, dependency.value().capability().generation()),
        encodeUint32(values[0]));
  }
  static zc::Maybe<zc::Array<uint8_t>> verify(CapabilityQueryContext&, const Key&,
                                              const Capability& candidate) {
    return encodeUint32(candidate.value());
  }
};

struct SlowCapabilityQuery final : LeafCapabilityQuery {
  static zc::StringPtr domain() { return "test.capability.slow"_zc; }
  static QueryKindContract contract() {
    return derivedContract(domain(), ReuseClass::RevisionLocal, RetentionClass::Retained);
  }
  static CapabilityProviderResult<Capability> provide(CapabilityQueryContext& context,
                                                      const Key& key) {
    usleep(20000);
    return LeafCapabilityQuery::provide(context, key);
  }
};

struct InvalidRetentionCapabilityQuery final : LeafCapabilityQuery {
  static zc::StringPtr domain() { return "test.capability.invalid-retention"_zc; }
  static QueryKindContract contract() {
    return derivedContract(domain(), ReuseClass::RevisionLocal, RetentionClass::Evictable);
  }
};

struct InvalidReuseCapabilityQuery final : LeafCapabilityQuery {
  static zc::StringPtr domain() { return "test.capability.invalid-reuse"_zc; }
  static QueryKindContract contract() {
    return derivedContract(domain(), ReuseClass::Semantic, RetentionClass::Retained);
  }
};

struct RejectedCapabilityQuery final : LeafCapabilityQuery {
  static zc::StringPtr domain() { return "test.capability.rejected"_zc; }
  static QueryKindContract contract() {
    return derivedContract(domain(), ReuseClass::RevisionLocal, RetentionClass::Retained);
  }
  static zc::Maybe<zc::Array<uint8_t>> verify(CapabilityQueryContext&, const Key&,
                                              const Capability& candidate) {
    return encodeUint32(candidate.values()[0] + 1);
  }
};

struct TerminalCapabilityQuery final : LeafCapabilityQuery {
  static zc::StringPtr domain() { return "test.capability.terminal"_zc; }
  static QueryKindContract contract() {
    return derivedContract(domain(), ReuseClass::RevisionLocal, RetentionClass::Retained);
  }
  static CapabilityProviderResult<Capability> provide(CapabilityQueryContext&, const Key& key) {
    if (key == 0) { return CapabilityProviderResult<Capability>::absence(); }
    return CapabilityProviderResult<Capability>::semanticFailure(encodeUint32(key));
  }
};

void registerCapabilityKinds(QueryDatabase& database) {
  ZC_REQUIRE(database.registerInputKind<LowInput>() != zc::none);
  ZC_REQUIRE(database.registerRevisionLocalCapabilityKind<LeafCapabilityQuery>() != zc::none);
  ZC_REQUIRE(database.registerRevisionLocalCapabilityKind<ParentCapabilityQuery>() != zc::none);
}

}  // namespace

ZC_TEST("QueryCapabilityTest.RejectsNonRevisionLocalAndEvictableDescriptors") {
  auto database = capabilityTestDatabase();
  ZC_EXPECT(database.registerRevisionLocalCapabilityKind<InvalidRetentionCapabilityQuery>() ==
            zc::none);
  ZC_EXPECT(database.registerRevisionLocalCapabilityKind<InvalidReuseCapabilityQuery>() ==
            zc::none);
}

ZC_TEST("QueryCapabilityTest.RequiresAnExplicitSessionResourceArena") {
  QueryDatabase canonicalOnlyDatabase(queryTestScheduler());
  ZC_EXPECT(canonicalOnlyDatabase.registerRevisionLocalCapabilityKind<LeafCapabilityQuery>() ==
            zc::none);
}

ZC_TEST("QueryCapabilityTest.AllowsOnlyPermittedCapabilityDescriptorsToMaterializeActiveKeys") {
  auto resources = zc::heap<TestActiveResources>();
  auto arena = zc::arc<SemanticContextCapabilityArena>(zc::mv(resources));
  QueryDatabase database(queryTestScheduler(), zc::mv(arena));
  ZC_REQUIRE(database.registerInputKind<LowInput>() != zc::none);
  ZC_REQUIRE(database.registerRevisionLocalCapabilityKind<MaterializingCapabilityQuery>() !=
             zc::none);
  auto write = beginTransaction(database);
  ZC_REQUIRE(write.set<LowInput>(41, 41));
  ZC_REQUIRE(write.set<LowInput>(42, 0));
  ZC_REQUIRE(write.commit() != zc::none);
  ZC_REQUIRE(database.sealInputRoot());
  auto snapshot = database.snapshot();

  auto active = snapshot.getCapability<MaterializingCapabilityQuery>(TestActiveKey{41});
  ZC_REQUIRE(!active.isRuntimeFailure());
  ZC_REQUIRE(active.kind() == QueryValueKind::Value);
  ZC_EXPECT(active.value().capability().handle() == 41);
  auto dependencies = snapshot.dependencies<MaterializingCapabilityQuery>(TestActiveKey{41});
  ZC_REQUIRE(dependencies.size() == 1);
  ZC_REQUIRE(dependencies[0].dependencies().size() == 1);
  ZC_EXPECT(dependencies[0].dependencies()[0].key().canonicalBytes() == encodeUint32(41).asPtr());

  auto inactive = snapshot.getCapability<MaterializingCapabilityQuery>(TestActiveKey{42});
  ZC_REQUIRE(!inactive.isRuntimeFailure());
  ZC_EXPECT(inactive.kind() == QueryValueKind::Absence);
}

ZC_TEST("QueryCapabilityTest.RejectsAnUnpermittedCapabilityDescriptorAtRuntime") {
  auto resources = zc::heap<TestActiveResources>();
  auto arena = zc::arc<SemanticContextCapabilityArena>(zc::mv(resources));
  QueryDatabase database(queryTestScheduler(), zc::mv(arena));
  ZC_REQUIRE(database.registerInputKind<LowInput>() != zc::none);
  ZC_REQUIRE(
      database.registerRevisionLocalCapabilityKind<UnpermittedMaterializingCapabilityQuery>() !=
      zc::none);
  auto write = beginTransaction(database);
  ZC_REQUIRE(write.set<LowInput>(46, 46));
  ZC_REQUIRE(write.commit() != zc::none);
  ZC_REQUIRE(database.sealInputRoot());
  auto snapshot = database.snapshot();

  auto result = snapshot.getCapability<UnpermittedMaterializingCapabilityQuery>(TestActiveKey{46});
  ZC_REQUIRE(result.isRuntimeFailure());
  ZC_EXPECT(static_cast<uint8_t>(result.runtimeFailure()) ==
            static_cast<uint8_t>(QueryRuntimeFailure::ProviderRejected));
  ZC_EXPECT(
      snapshot.dependencies<UnpermittedMaterializingCapabilityQuery>(TestActiveKey{46}).empty());
}

ZC_TEST("QueryCapabilityTest.RejectsActiveMaterializationBeforeAndOutsideTheFinalBarrier") {
  auto resources = zc::heap<TestActiveResources>();
  auto arena = zc::arc<SemanticContextCapabilityArena>(zc::mv(resources));
  QueryDatabase database(queryTestScheduler(), zc::mv(arena));
  ZC_REQUIRE(database.registerInputKind<LowInput>() != zc::none);
  ZC_REQUIRE(database.registerRevisionLocalCapabilityKind<MaterializingCapabilityQuery>() !=
             zc::none);
  auto stagingWrite = beginTransaction(database);
  ZC_REQUIRE(stagingWrite.set<LowInput>(51, 51));
  ZC_REQUIRE(stagingWrite.commit() != zc::none);
  auto staging = database.snapshot();
  auto beforeBarrier = staging.getCapability<MaterializingCapabilityQuery>(TestActiveKey{51});
  ZC_REQUIRE(beforeBarrier.isRuntimeFailure());
  ZC_EXPECT(beforeBarrier.runtimeFailure() == QueryRuntimeFailure::ProviderRejected);

  auto finalWrite = beginTransaction(database);
  ZC_REQUIRE(finalWrite.set<LowInput>(52, 52));
  ZC_REQUIRE(finalWrite.commit() != zc::none);
  auto finalSnapshot = database.snapshot();
  ZC_REQUIRE(database.sealInputRoot());

  auto stale = staging.getCapability<MaterializingCapabilityQuery>(TestActiveKey{51});
  ZC_REQUIRE(stale.isRuntimeFailure());
  ZC_EXPECT(stale.runtimeFailure() == QueryRuntimeFailure::ProviderRejected);
  auto finalResult = finalSnapshot.getCapability<MaterializingCapabilityQuery>(TestActiveKey{51});
  ZC_REQUIRE(!finalResult.isRuntimeFailure());
  ZC_EXPECT(finalResult.kind() == QueryValueKind::Value);
}

ZC_TEST("QueryCapabilityTest.SameSnapshotReturnsOneRetainedMemoGeneration") {
  auto database = capabilityTestDatabase();
  registerCapabilityKinds(database);
  auto write = beginTransaction(database);
  ZC_REQUIRE(write.set<LowInput>(7, 70));
  ZC_REQUIRE(write.commit() != zc::none);
  auto snapshot = database.snapshot();

  auto first = snapshot.getCapability<LeafCapabilityQuery>(7);
  auto second = snapshot.getCapability<LeafCapabilityQuery>(7);
  ZC_REQUIRE(!first.isRuntimeFailure());
  ZC_REQUIRE(!second.isRuntimeFailure());
  ZC_EXPECT(first.value().capability().values()[0] == 70);
  ZC_EXPECT(first.value().capability().generation() == second.value().capability().generation());
  ZC_EXPECT(first.value().revision() == DatabaseRevision(1));
  ZC_EXPECT(first.value().arenaRevision() == DatabaseRevision(1));
  ZC_EXPECT(first.value().stableWitness() == encodeUint32(70).asPtr());
  ZC_EXPECT(snapshot.hasRetainedValue<LeafCapabilityQuery>(7));
  ZC_EXPECT(!snapshot.evictValue<LeafCapabilityQuery>(7));
  auto mismatched = snapshot.getCapability<MismatchedCapabilityDemand>(7);
  ZC_REQUIRE(mismatched.isRuntimeFailure());
  ZC_EXPECT(mismatched.runtimeFailure() == QueryRuntimeFailure::InvariantViolation);

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
  ZC_REQUIRE(firstWrite.set<LowInput>(3, 30));
  ZC_REQUIRE(firstWrite.set<LowInput>(capabilityGenerationInputKey(3), 1));
  ZC_REQUIRE(firstWrite.commit() != zc::none);
  auto firstSnapshot = database.snapshot();
  auto first = firstSnapshot.getCapability<LeafCapabilityQuery>(3);
  ZC_REQUIRE(!first.isRuntimeFailure());
  auto firstLease = first.value().clone();

  auto secondWrite = beginTransaction(database);
  ZC_REQUIRE(secondWrite.set<LowInput>(3, 30));
  ZC_REQUIRE(secondWrite.set<LowInput>(capabilityGenerationInputKey(3), 2));
  ZC_REQUIRE(secondWrite.commit() != zc::none);
  auto secondSnapshot = database.snapshot();
  ZC_EXPECT(!secondSnapshot.hasRetainedValue<LeafCapabilityQuery>(3));
  auto second = secondSnapshot.getCapability<LeafCapabilityQuery>(3);
  ZC_REQUIRE(!second.isRuntimeFailure());
  ZC_EXPECT(second.value().revision() == DatabaseRevision(2));
  ZC_EXPECT(firstLease.revision() == DatabaseRevision(1));
  ZC_EXPECT(firstLease.capability().generation() != second.value().capability().generation());
  ZC_EXPECT(firstLease.stableWitness() == second.value().stableWitness());
}

ZC_TEST("QueryCapabilityTest.ConcurrentDemandsJoinOneCapabilityFlight") {
  auto database = capabilityTestDatabase();
  ZC_REQUIRE(database.registerInputKind<LowInput>() != zc::none);
  ZC_REQUIRE(database.registerRevisionLocalCapabilityKind<SlowCapabilityQuery>() != zc::none);
  auto write = beginTransaction(database);
  ZC_REQUIRE(write.set<LowInput>(8, 80));
  ZC_REQUIRE(write.commit() != zc::none);
  auto snapshot = database.snapshot();
  zc::MutexGuarded<zc::Vector<uint32_t>> generations;

  {
    zc::Thread first([&]() {
      auto result = snapshot.getCapability<SlowCapabilityQuery>(8);
      if (!result.isRuntimeFailure()) {
        generations.lockExclusive()->add(result.value().capability().generation());
      }
    });
    zc::Thread second([&]() {
      auto result = snapshot.getCapability<SlowCapabilityQuery>(8);
      if (!result.isRuntimeFailure()) {
        generations.lockExclusive()->add(result.value().capability().generation());
      }
    });
  }

  auto retained = generations.lockShared();
  ZC_REQUIRE(retained->size() == 2);
  ZC_EXPECT((*retained)[0] == (*retained)[1]);
  retained.release();
  ZC_EXPECT(hasEvent(snapshot.events().asPtr(), QueryEventKind::SingleFlightJoined));
}

ZC_TEST("QueryCapabilityTest.ParentMemoRetainsBorrowedCapabilityTransitively") {
  zc::Maybe<QueryCapabilityLease<const ParentCapability>> survivingLease;
  {
    auto database = capabilityTestDatabase();
    registerCapabilityKinds(database);
    auto write = beginTransaction(database);
    ZC_REQUIRE(write.set<LowInput>(11, 110));
    ZC_REQUIRE(write.commit() != zc::none);
    auto snapshot = database.snapshot();

    auto parent = snapshot.getCapability<ParentCapabilityQuery>(11);
    ZC_REQUIRE(!parent.isRuntimeFailure());
    ZC_EXPECT(parent.value().retainedDependencyCount() == 1);
    auto dependencies = snapshot.dependencies<ParentCapabilityQuery>(11);
    ZC_REQUIRE(dependencies.size() == 1);
    ZC_REQUIRE(dependencies[0].dependencies().size() == 1);
    auto witness = dependencies[0].dependencies()[0].stableWitness();
    ZC_REQUIRE(witness != zc::none);
    ZC_EXPECT(ZC_REQUIRE_NONNULL(witness) == encodeUint32(110).asPtr());
    survivingLease = parent.value().clone();
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
    QueryDatabase database(queryTestScheduler(), zc::mv(arena));
    ZC_REQUIRE(database.registerInputKind<LowInput>() != zc::none);
    ZC_REQUIRE(database.registerRevisionLocalCapabilityKind<LeafCapabilityQuery>() != zc::none);
    auto write = beginTransaction(database);
    ZC_REQUIRE(write.set<LowInput>(12, 120));
    ZC_REQUIRE(write.commit() != zc::none);
    auto snapshot = database.snapshot();
    auto result = snapshot.getCapability<LeafCapabilityQuery>(12);
    ZC_REQUIRE(!result.isRuntimeFailure());
    survivingLease = result.value().clone();
  }

  ZC_EXPECT(!*resourcesDestroyed.lockShared());
  survivingLease = zc::none;
  ZC_EXPECT(*resourcesDestroyed.lockShared());
}

ZC_TEST("QueryCapabilityTest.VerifierMismatchAndCanonicalTerminalsPublishCorrectly") {
  auto database = capabilityTestDatabase();
  ZC_REQUIRE(database.registerInputKind<LowInput>() != zc::none);
  ZC_REQUIRE(database.registerRevisionLocalCapabilityKind<RejectedCapabilityQuery>() != zc::none);
  ZC_REQUIRE(database.registerRevisionLocalCapabilityKind<TerminalCapabilityQuery>() != zc::none);
  auto write = beginTransaction(database);
  ZC_REQUIRE(write.set<LowInput>(5, 50));
  ZC_REQUIRE(write.commit() != zc::none);
  auto snapshot = database.snapshot();

  auto rejected = snapshot.getCapability<RejectedCapabilityQuery>(5);
  ZC_REQUIRE(rejected.isRuntimeFailure());
  ZC_EXPECT(rejected.runtimeFailure() == QueryRuntimeFailure::VerifierRejected);
  ZC_EXPECT(snapshot.metadata<RejectedCapabilityQuery>(5) == zc::none);

  auto absent = snapshot.getCapability<TerminalCapabilityQuery>(0);
  ZC_EXPECT(!absent.isRuntimeFailure());
  ZC_EXPECT(absent.kind() == QueryValueKind::Absence);
  auto failed = snapshot.getCapability<TerminalCapabilityQuery>(9);
  ZC_EXPECT(!failed.isRuntimeFailure());
  ZC_EXPECT(failed.kind() == QueryValueKind::SemanticFailure);
  ZC_EXPECT(failed.semanticFailureBytes() == encodeUint32(9).asPtr());
}

}  // namespace zomlang::compiler::query::test
