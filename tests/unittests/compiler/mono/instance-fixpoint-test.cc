// Copyright (c) 2026 Zode.Z. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/mono/instance-fixpoint.h"

#include <cstdint>

#include "compiler/checker/inference/checked-facts.h"
#include "compiler/identity/brand.h"
#include "compiler/identity/canonical/identity-interner-set.h"
#include "compiler/identity/crypto/overload-header-digest.h"
#include "compiler/identity/key/definition-key.h"
#include "compiler/identity/source/source-snapshot.h"
#include "compiler/mir/built-mir.h"
#include "compiler/mono/instance-cloner.h"
#include "compiler/mono/instance-set.h"
#include "compiler/type/semantic-type-data.h"
#include "tests/unittests/compiler/test-semantic-identities.h"
#include "zc/core/memory.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::mono {
namespace {

namespace tpsem = type::semantic;

using checker::checked::CanonicalSubstitutionId;
using checker::checked::FrozenSubstitutionStore;
using checker::checked::FrozenWitnessStore;
using checker::checked::SubstitutionData;
using checker::checked::WitnessArgumentsData;
using checker::checked::WitnessArgumentsId;
using checker::checked::WitnessEntry;
using checker::signature::CanonicalConstValue;
using checker::signature::CanonicalInteger;
using checker::signature::IntegerSign;
using identity::DefId;
using identity::GenericParameterIdentityRecord;
using identity::GenericParameterKey;
using identity::IdentityInternerSet;
using identity::RegistryBrandIssuer;
using identity::SemanticContextBrand;
using identity::SemanticContextFactory;
using identity::SourceSpan;
using identity::StableGenericParameterOwnerKey;
using ir::InstanceId;
using mir::MirBasicBlock;
using mir::MirBlockId;
using mir::MirBorrowKind;
using mir::MirCallableIdentity;
using mir::MirCallEffect;
using mir::MirFunction;
using mir::MirFunctionKind;
using mir::MirInitializationKind;
using mir::MirLocalDeclaration;
using mir::MirLocalId;
using mir::MirLocalKind;
using mir::MirOperand;
using mir::MirPlace;
using mir::MirProjection;
using mir::MirRvalue;
using mir::MirSourceScope;
using mir::MirSourceScopeId;
using mir::MirStatement;
using mir::MirTerminator;
using mir::MirUnsafeScopeBoundaryKind;
using type::SemanticTypeStore;

MirBlockId blockId(uint32_t ordinal) {
  return ZC_REQUIRE_NONNULL(MirBlockId::fromOrdinal(ordinal));
}
MirLocalId localId(uint32_t ordinal) {
  return ZC_REQUIRE_NONNULL(MirLocalId::fromOrdinal(ordinal));
}
MirSourceScopeId scopeId(uint32_t ordinal) {
  return ZC_REQUIRE_NONNULL(MirSourceScopeId::fromOrdinal(ordinal));
}

SourceSpan span() {
  auto snapshot = identity::ImmutableSourceSnapshot::from(tests::test_identity_detail::source(),
                                                          zc::heapArray<uint8_t>(8, uint8_t{0}));
  ZC_REQUIRE(snapshot != zc::none);
  return ZC_REQUIRE_NONNULL(ZC_ASSERT_NONNULL(snapshot).span(1, 7));
}

CanonicalConstValue integerConst(uint8_t value) {
  auto magnitude = zc::heapArray<uint8_t>(1, value);
  return CanonicalConstValue::integer(
      CanonicalInteger{IntegerSign::NonNegative, zc::mv(magnitude)});
}

MirPlace placeOf(MirLocalId local, identity::SemanticTypeId type) {
  return MirPlace(local, type, zc::Vector<MirProjection>(), type);
}

MirFunction functionWithBlocks(DefId owner, identity::SemanticTypeId resultType,
                               zc::Vector<MirBasicBlock>&& blocks) {
  zc::Vector<MirSourceScope> scopes;
  scopes.add(MirSourceScope{scopeId(1), zc::none, span()});
  zc::Vector<MirLocalDeclaration> locals;
  locals.add(MirLocalDeclaration{localId(1), MirLocalKind::FunctionResult, resultType, scopeId(1),
                                 span()});
  return MirFunction{owner,
                     MirFunctionKind::Function,
                     identity::DefinitionKind::Function,
                     resultType,
                     span(),
                     zc::mv(scopes),
                     zc::mv(locals),
                     zc::mv(blocks)};
}

MirFunction voidFunction(DefId owner, identity::SemanticTypeId resultType) {
  zc::Vector<MirBasicBlock> blocks;
  blocks.add(MirBasicBlock{blockId(1), scopeId(1), zc::Vector<MirStatement>(),
                           MirTerminator::returnVoid(span())});
  return functionWithBlocks(owner, resultType, zc::mv(blocks));
}

MirTerminator callTerminator(MirCallableIdentity&& callee, identity::SemanticTypeId scalar,
                             MirLocalId destination, MirBlockId normalTarget) {
  zc::Vector<MirOperand> arguments;
  arguments.add(MirOperand::constant(scalar, integerConst(42)));
  zc::Maybe<mir::MirBlockId> unwind;
  return MirTerminator::call(zc::mv(callee), zc::mv(arguments), MirCallEffect::noActivation(),
                             placeOf(destination, scalar), normalTarget, zc::mv(unwind), span());
}

/// \brief Owns one semantic context with branded definitions and the canonical
/// type store the fixpoint interns substituted grounds into.
class MonoCoreFixture final {
public:
  MonoCoreFixture() {
    auto issuedContext = factory.issue();
    ZC_REQUIRE(issuedContext != zc::none);
    context = ZC_ASSERT_NONNULL(issuedContext);
    auto issuedInterners = IdentityInternerSet::create(factory, context);
    ZC_REQUIRE(issuedInterners != zc::none);
    interners = zc::heap<IdentityInternerSet>(zc::mv(ZC_ASSERT_NONNULL(issuedInterners)));
    auto issuedBrandIssuer = factory.issueRegistryBrandIssuer(context);
    ZC_REQUIRE(issuedBrandIssuer != zc::none);
    brandIssuer = zc::mv(ZC_ASSERT_NONNULL(issuedBrandIssuer));
    auto issuedToken = factory.issueSemanticTypeStoreConstructionToken(context);
    ZC_REQUIRE(issuedToken != zc::none);
    semanticTypes = zc::heap<SemanticTypeStore>(zc::mv(ZC_ASSERT_NONNULL(issuedToken)), *interners);
  }

  SemanticContextBrand brand() const noexcept { return context; }
  SemanticTypeStore& store() noexcept { return *semanticTypes; }

  DefId functionDefinition(zc::StringPtr name) {
    auto declared = identity::DeclaredDefinitionName::fromCanonical(name);
    ZC_REQUIRE(declared != zc::none);
    zc::Vector<identity::EnclosingStableOwnerKey> owners;
    auto overloadBytes = zc::heapArray<uint8_t>(32, static_cast<uint8_t>(name.size()));
    auto overloadDigest = identity::OverloadHeaderDigest::fromBytes(overloadBytes.asPtr());
    ZC_REQUIRE(overloadDigest != zc::none);
    zc::Maybe<identity::OverloadHeaderDigest> overload = zc::mv(ZC_ASSERT_NONNULL(overloadDigest));
    auto record = identity::DefinitionIdentityRecord::from(
        tests::test_identity_detail::module(), zc::mv(owners), identity::DefinitionKind::Function,
        identity::DefinitionNamespace::Value, zc::mv(ZC_ASSERT_NONNULL(declared)),
        zc::mv(overload));
    ZC_REQUIRE(record != zc::none);
    auto key = identity::DefinitionKey::compute(ZC_ASSERT_NONNULL(record));
    auto admitted = interners->internDefinition(context, key, ZC_ASSERT_NONNULL(record));
    ZC_REQUIRE(admitted.is<DefId>());
    return admitted.get<DefId>();
  }

  GenericParameterKey genericKey(DefId owner, uint32_t ordinal) {
    auto entry = interners->definition(owner);
    ZC_REQUIRE(entry != zc::none);
    auto stableOwner =
        StableGenericParameterOwnerKey::definition(ZC_ASSERT_NONNULL(entry).key().clone());
    auto record = GenericParameterIdentityRecord::type(zc::mv(stableOwner), ordinal);
    auto key = GenericParameterKey::compute(record);
    auto admitted = interners->internGenericParameter(context, key, record);
    ZC_REQUIRE(admitted.is<identity::GenericParameterId>());
    return key;
  }

  identity::SemanticTypeId primitive(tpsem::PrimitiveKind kind) {
    auto canonical =
        semanticTypes->canonicalizeClosed(tpsem::TypeData(tpsem::PrimitiveTypeData{kind}));
    ZC_REQUIRE(canonical.is<tpsem::CanonicalTypeData>());
    auto interned = semanticTypes->intern(zc::mv(canonical).get<tpsem::CanonicalTypeData>());
    ZC_REQUIRE(interned.is<type::SemanticTypeInterned>());
    return interned.get<type::SemanticTypeInterned>().id;
  }

  identity::SemanticTypeId typeParameter(const GenericParameterKey& key) {
    auto canonical = semanticTypes->canonicalizeClosed(
        tpsem::TypeData(tpsem::TypeParameterTypeData{key.clone()}));
    ZC_REQUIRE(canonical.is<tpsem::CanonicalTypeData>());
    auto interned = semanticTypes->intern(zc::mv(canonical).get<tpsem::CanonicalTypeData>());
    ZC_REQUIRE(interned.is<type::SemanticTypeInterned>());
    return interned.get<type::SemanticTypeInterned>().id;
  }

  void freezeStores(zc::Vector<FrozenSubstitutionStore::Record>&& substitutionRecords,
                    zc::Vector<FrozenWitnessStore::Record>&& witnessRecords) {
    auto substitutionBrand = ZC_ASSERT_NONNULL(brandIssuer).issue();
    auto witnessBrand = ZC_ASSERT_NONNULL(brandIssuer).issue();
    ZC_REQUIRE(substitutionBrand != zc::none && witnessBrand != zc::none);
    auto substitutions = FrozenSubstitutionStore::from(
        context, ZC_ASSERT_NONNULL(substitutionBrand), zc::mv(substitutionRecords));
    auto witnesses =
        FrozenWitnessStore::from(context, ZC_ASSERT_NONNULL(witnessBrand), zc::mv(witnessRecords));
    ZC_REQUIRE(substitutions != zc::none && witnesses != zc::none);
    substitutionStore = zc::mv(ZC_ASSERT_NONNULL(substitutions));
    witnessStore = zc::mv(ZC_ASSERT_NONNULL(witnesses));
  }

  CanonicalSubstitutionId substitutionId(uint32_t index) {
    return ZC_ASSERT_NONNULL(ZC_ASSERT_NONNULL(substitutionStore).idAt(index));
  }
  WitnessArgumentsId witnessId(uint32_t index) {
    return ZC_ASSERT_NONNULL(ZC_ASSERT_NONNULL(witnessStore).idAt(index));
  }

  InstanceId instance(DefId definition, CanonicalSubstitutionId substitution,
                      WitnessArgumentsId witness) {
    return ZC_ASSERT_NONNULL(ir::InstanceId::from(context, definition, substitution, witness));
  }

  zc::Array<uint8_t> definitionKey(DefId definition) {
    auto entry = interners->definition(definition);
    ZC_REQUIRE(entry != zc::none);
    return ZC_ASSERT_NONNULL(entry).key().encode();
  }

private:
  SemanticContextFactory factory;
  SemanticContextBrand context;
  zc::Own<IdentityInternerSet> interners;
  zc::Maybe<RegistryBrandIssuer> brandIssuer;
  zc::Own<SemanticTypeStore> semanticTypes;
  zc::Maybe<FrozenSubstitutionStore> substitutionStore;
  zc::Maybe<FrozenWitnessStore> witnessStore;
};

struct TemplateEntry final {
  DefId definition;
  zc::Own<MirFunction> function;
  zc::Vector<GenericParameterKey> keys;
};

struct LiveSubstitution final {
  CanonicalSubstitutionId id;
  SubstitutionData data;
  zc::Array<uint8_t> bytes;
};

struct LiveWitness final {
  WitnessArgumentsId id;
  zc::Array<uint8_t> bytes;
};

/// \brief Hand-driven evidence provider: templates, keys, and records are
/// installed by the component test instead of a session repository.
class FakeProvider final : public MonoEvidenceProvider {
public:
  explicit FakeProvider(MonoCoreFixture& fixture) : fixture(fixture) {}

  void setTemplate(DefId definition, MirFunction&& function,
                   zc::Vector<GenericParameterKey>&& keys) {
    templates.add(TemplateEntry{definition, zc::heap<MirFunction>(zc::mv(function)), zc::mv(keys)});
  }

  void addSubstitution(GenericParameterKey parameter, identity::SemanticTypeId argument) {
    zc::Vector<GenericParameterKey> parameters;
    parameters.add(zc::mv(parameter));
    zc::Vector<identity::SemanticTypeId> arguments;
    arguments.add(argument);
    auto bytes =
        zc::heapArray<uint8_t>(1, static_cast<uint8_t>(0xa0 + pendingSubstitutions.size()));
    pendingSubstitutions.add(PendingSubstitution{
        SubstitutionData{zc::mv(parameters), zc::mv(arguments)}, zc::mv(bytes)});
  }

  void addEmptyWitness() {
    auto bytes = zc::heapArray<uint8_t>(1, static_cast<uint8_t>(0xb0 + pendingWitnesses.size()));
    pendingWitnesses.add(zc::mv(bytes));
  }

  /// \brief Freezes the fixture stores and binds issued ids to pending records.
  void freeze() {
    zc::Vector<FrozenSubstitutionStore::Record> substitutionRecords;
    zc::Vector<FrozenWitnessStore::Record> witnessRecords;
    for (const auto& pending : pendingSubstitutions) {
      zc::Vector<GenericParameterKey> parameters;
      for (const auto& parameter : pending.data.parameters) { parameters.add(parameter.clone()); }
      zc::Vector<identity::SemanticTypeId> arguments;
      for (const auto argument : pending.data.arguments) { arguments.add(argument); }
      substitutionRecords.add(
          FrozenSubstitutionStore::Record{SubstitutionData{zc::mv(parameters), zc::mv(arguments)},
                                          zc::heapArray(pending.bytes.asPtr())});
    }
    for (const auto& pending : pendingWitnesses) {
      witnessRecords.add(FrozenWitnessStore::Record{
          WitnessArgumentsData{zc::Vector<WitnessEntry>()}, zc::heapArray(pending.asPtr())});
    }
    fixture.freezeStores(zc::mv(substitutionRecords), zc::mv(witnessRecords));
    for (uint32_t index = 0; index < pendingSubstitutions.size(); ++index) {
      liveSubstitutions.add(LiveSubstitution{fixture.substitutionId(index),
                                             zc::mv(pendingSubstitutions[index].data),
                                             zc::mv(pendingSubstitutions[index].bytes)});
    }
    for (uint32_t index = 0; index < pendingWitnesses.size(); ++index) {
      liveWitnesses.add(LiveWitness{fixture.witnessId(index), zc::mv(pendingWitnesses[index])});
    }
    pendingSubstitutions.clear();
    pendingWitnesses.clear();
  }

  CanonicalSubstitutionId substitutionAt(uint32_t index) const {
    return liveSubstitutions[index].id;
  }
  WitnessArgumentsId witnessAt(uint32_t index) const { return liveWitnesses[index].id; }

  zc::Maybe<const MirFunction&> templateFor(DefId definition) const override {
    ZC_IF_SOME(index, indexOf(definition)) { return *templates[index].function; }
    return zc::none;
  }

  zc::Maybe<zc::Vector<GenericParameterKey>> genericParameterKeys(DefId definition) const override {
    ZC_IF_SOME(index, indexOf(definition)) {
      zc::Vector<GenericParameterKey> keys;
      for (const auto& key : templates[index].keys) { keys.add(key.clone()); }
      return zc::mv(keys);
    }
    return zc::none;
  }

  zc::Maybe<const SubstitutionData&> substitution(CanonicalSubstitutionId id) const override {
    for (const auto& live : liveSubstitutions) {
      if (live.id == id) { return live.data; }
    }
    return zc::none;
  }
  zc::Maybe<zc::ArrayPtr<const uint8_t>> substitutionRecord(
      CanonicalSubstitutionId id) const override {
    for (const auto& live : liveSubstitutions) {
      if (live.id == id) { return live.bytes.asPtr(); }
    }
    return zc::none;
  }
  zc::Maybe<zc::ArrayPtr<const uint8_t>> witnessRecord(WitnessArgumentsId id) const override {
    for (const auto& live : liveWitnesses) {
      if (live.id == id) { return live.bytes.asPtr(); }
    }
    return zc::none;
  }
  zc::Maybe<zc::Array<uint8_t>> definitionKey(DefId definition) const override {
    return fixture.definitionKey(definition);
  }

private:
  struct PendingSubstitution final {
    SubstitutionData data;
    zc::Array<uint8_t> bytes;
  };

  zc::Maybe<size_t> indexOf(DefId definition) const {
    for (size_t index = 0; index < templates.size(); ++index) {
      if (templates[index].definition == definition) { return index; }
    }
    return zc::none;
  }

  MonoCoreFixture& fixture;
  zc::Vector<TemplateEntry> templates;
  zc::Vector<PendingSubstitution> pendingSubstitutions;
  zc::Vector<zc::Array<uint8_t>> pendingWitnesses;
  zc::Vector<LiveSubstitution> liveSubstitutions;
  zc::Vector<LiveWitness> liveWitnesses;
};

/// \brief One root whose single block ends in one Instance call then returns.
MirFunction callingRoot(DefId root, identity::SemanticTypeId scalar, MirCallableIdentity&& callee) {
  zc::Vector<MirBasicBlock> blocks;
  blocks.add(MirBasicBlock{blockId(1), scopeId(1), zc::Vector<MirStatement>(),
                           callTerminator(callee.clone(), scalar, localId(1), blockId(2))});
  blocks.add(MirBasicBlock{blockId(2), scopeId(1), zc::Vector<MirStatement>(),
                           MirTerminator::returnVoid(span())});
  return functionWithBlocks(root, scalar, zc::mv(blocks));
}

/// \brief Root issuing two Instance calls before returning.
MirFunction twoCallRoot(DefId root, identity::SemanticTypeId scalar, MirCallableIdentity&& first,
                        MirCallableIdentity&& second) {
  zc::Vector<MirBasicBlock> blocks;
  blocks.add(MirBasicBlock{blockId(1), scopeId(1), zc::Vector<MirStatement>(),
                           callTerminator(first.clone(), scalar, localId(1), blockId(2))});
  blocks.add(MirBasicBlock{blockId(2), scopeId(1), zc::Vector<MirStatement>(),
                           callTerminator(second.clone(), scalar, localId(1), blockId(3))});
  blocks.add(MirBasicBlock{blockId(3), scopeId(1), zc::Vector<MirStatement>(),
                           MirTerminator::returnVoid(span())});
  return functionWithBlocks(root, scalar, zc::mv(blocks));
}

constexpr uint64_t kTestInstanceBudget = mono::kMonomorphizationInstanceBudget;
constexpr uint64_t kTestNodeBudget = mono::kMonomorphizationSubstitutionNodeBudget;

}  // namespace

ZC_TEST("MonoFixpoint.sharesOneInstanceForRepeatedSameSubstitutionCalls") {
  MonoCoreFixture fixture;
  FakeProvider provider(fixture);
  const auto i32 = fixture.primitive(tpsem::PrimitiveKind::I32);
  const auto rootDef = fixture.functionDefinition("entry"_zc);
  const auto genericDef = fixture.functionDefinition("identity"_zc);
  const auto key = fixture.genericKey(genericDef, 0);
  const auto parameter = fixture.typeParameter(key);
  provider.setTemplate(genericDef, voidFunction(genericDef, parameter), [&] {
    zc::Vector<GenericParameterKey> keys;
    keys.add(key.clone());
    return keys;
  }());
  provider.addSubstitution(key.clone(), i32);
  provider.addEmptyWitness();
  provider.freeze();
  const auto instance =
      fixture.instance(genericDef, provider.substitutionAt(0), provider.witnessAt(0));
  auto root = callingRoot(rootDef, i32, MirCallableIdentity::instance(instance));
  zc::Vector<const MirFunction*> roots;
  roots.add(&root);

  auto result = collectInstances(roots.asPtr(), provider, fixture.store(), kTestInstanceBudget,
                                 kTestNodeBudget);
  ZC_REQUIRE(result.is<zc::Vector<MonoRawInstance>>());
  const auto& instances = result.get<zc::Vector<MonoRawInstance>>();
  ZC_EXPECT(instances.size() == 1);
  ZC_EXPECT(instances[0].identity == instance);
  // The substituted template body closes over i32, not the type parameter.
  ZC_EXPECT(instances[0].body.resultType == i32);
}

ZC_TEST("MonoFixpoint.producesTwoSortedInstancesForDistinctSubstitutions") {
  MonoCoreFixture fixture;
  FakeProvider provider(fixture);
  const auto i32 = fixture.primitive(tpsem::PrimitiveKind::I32);
  const auto boolean = fixture.primitive(tpsem::PrimitiveKind::Bool);
  const auto rootDef = fixture.functionDefinition("entry"_zc);
  const auto genericDef = fixture.functionDefinition("identity"_zc);
  const auto key = fixture.genericKey(genericDef, 0);
  const auto parameter = fixture.typeParameter(key);
  provider.setTemplate(genericDef, voidFunction(genericDef, parameter), [&] {
    zc::Vector<GenericParameterKey> keys;
    keys.add(key.clone());
    return keys;
  }());
  provider.addSubstitution(key.clone(), i32);
  provider.addSubstitution(key.clone(), boolean);
  provider.addEmptyWitness();
  provider.freeze();
  const auto i32Instance =
      fixture.instance(genericDef, provider.substitutionAt(0), provider.witnessAt(0));
  const auto boolInstance =
      fixture.instance(genericDef, provider.substitutionAt(1), provider.witnessAt(0));
  auto root = twoCallRoot(rootDef, i32, MirCallableIdentity::instance(i32Instance),
                          MirCallableIdentity::instance(boolInstance));
  zc::Vector<const MirFunction*> roots;
  roots.add(&root);

  auto result = collectInstances(roots.asPtr(), provider, fixture.store(), kTestInstanceBudget,
                                 kTestNodeBudget);
  ZC_REQUIRE(result.is<zc::Vector<MonoRawInstance>>());
  auto instances = zc::mv(result.get<zc::Vector<MonoRawInstance>>());
  ZC_REQUIRE(instances.size() == 2);
  sortInstancesByIdentity(instances);
  ZC_EXPECT(instancesAreFrameSorted(instances.asPtr()));
  ZC_EXPECT(instances[0].identity != instances[1].identity);
}

ZC_TEST("MonoFixpoint.rejectsRecursiveInstantiationChainWithZom6010Shape") {
  MonoCoreFixture fixture;
  FakeProvider provider(fixture);
  const auto i32 = fixture.primitive(tpsem::PrimitiveKind::I32);
  const auto boolean = fixture.primitive(tpsem::PrimitiveKind::Bool);
  const auto rootDef = fixture.functionDefinition("entry"_zc);
  const auto genericDef = fixture.functionDefinition("recursive"_zc);
  const auto key = fixture.genericKey(genericDef, 0);
  const auto parameter = fixture.typeParameter(key);
  // The generic template itself issues a nested instantiation of the same head.
  provider.addSubstitution(key.clone(), i32);
  provider.addSubstitution(key.clone(), boolean);
  provider.addEmptyWitness();
  provider.freeze();
  const auto i32Instance =
      fixture.instance(genericDef, provider.substitutionAt(0), provider.witnessAt(0));
  const auto boolInstance =
      fixture.instance(genericDef, provider.substitutionAt(1), provider.witnessAt(0));
  auto nestedRoot = callingRoot(genericDef, parameter, MirCallableIdentity::instance(boolInstance));
  provider.setTemplate(genericDef, zc::mv(nestedRoot), [&] {
    zc::Vector<GenericParameterKey> keys;
    keys.add(key.clone());
    return keys;
  }());
  auto root = callingRoot(rootDef, i32, MirCallableIdentity::instance(i32Instance));
  zc::Vector<const MirFunction*> roots;
  roots.add(&root);

  auto result = collectInstances(roots.asPtr(), provider, fixture.store(), kTestInstanceBudget,
                                 kTestNodeBudget);
  ZC_REQUIRE(result.is<MonoCycleError>());
  const auto& cycle = result.get<MonoCycleError>();
  ZC_EXPECT(cycle.root == i32Instance);
  ZC_REQUIRE(cycle.chain.size() == 2);
  ZC_EXPECT(cycle.chain[1] == boolInstance);
}

ZC_TEST("MonoFixpoint.rejectsInstanceCountBudgetExceedanceWithZom6011Shape") {
  MonoCoreFixture fixture;
  FakeProvider provider(fixture);
  const auto i32 = fixture.primitive(tpsem::PrimitiveKind::I32);
  const auto rootDef = fixture.functionDefinition("entry"_zc);
  const auto genericDef = fixture.functionDefinition("identity"_zc);
  const auto key = fixture.genericKey(genericDef, 0);
  const auto parameter = fixture.typeParameter(key);
  provider.setTemplate(genericDef, voidFunction(genericDef, parameter), [&] {
    zc::Vector<GenericParameterKey> keys;
    keys.add(key.clone());
    return keys;
  }());
  provider.addSubstitution(key.clone(), i32);
  provider.addEmptyWitness();
  provider.freeze();
  const auto instance =
      fixture.instance(genericDef, provider.substitutionAt(0), provider.witnessAt(0));
  auto root = callingRoot(rootDef, i32, MirCallableIdentity::instance(instance));
  zc::Vector<const MirFunction*> roots;
  roots.add(&root);

  auto result = collectInstances(roots.asPtr(), provider, fixture.store(), 0, kTestNodeBudget);
  ZC_REQUIRE(result.is<MonoBudgetError>());
  const auto& budget = result.get<MonoBudgetError>();
  ZC_EXPECT(budget.requestedInstanceCount == 1);
  ZC_EXPECT(budget.instanceLimit == 0);
  ZC_EXPECT(budget.root == instance);
}

ZC_TEST("MonoFixpoint.reportsUnresolvedEdgeWhenTemplateIsMissing") {
  MonoCoreFixture fixture;
  FakeProvider provider(fixture);
  const auto i32 = fixture.primitive(tpsem::PrimitiveKind::I32);
  const auto rootDef = fixture.functionDefinition("entry"_zc);
  const auto genericDef = fixture.functionDefinition("ghost"_zc);
  const auto key = fixture.genericKey(genericDef, 0);
  provider.addSubstitution(key.clone(), i32);
  provider.addEmptyWitness();
  provider.freeze();
  const auto instance =
      fixture.instance(genericDef, provider.substitutionAt(0), provider.witnessAt(0));
  auto root = callingRoot(rootDef, i32, MirCallableIdentity::instance(instance));
  zc::Vector<const MirFunction*> roots;
  roots.add(&root);

  auto result = collectInstances(roots.asPtr(), provider, fixture.store(), kTestInstanceBudget,
                                 kTestNodeBudget);
  ZC_EXPECT(result.is<MonoUnresolvedEdgeError>());
}

ZC_TEST("MonoInstanceBody.rejectsResidualGenericParameterKey") {
  MonoCoreFixture fixture;
  const auto genericDef = fixture.functionDefinition("identity"_zc);
  const auto key = fixture.genericKey(genericDef, 0);
  const auto parameter = fixture.typeParameter(key);
  auto body = voidFunction(genericDef, parameter);
  zc::Vector<GenericParameterKey> keys;
  keys.add(key.clone());

  auto rejection = validateInstanceBody(body, keys.asPtr(), fixture.store());
  ZC_REQUIRE(rejection != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(rejection).kind == MonoBodyRejectionKind::ResidualGenericKey);
  // With the callee key removed from the residual set, the same slot is simply a
  // non-scalar carrier.
  zc::Vector<GenericParameterKey> noKeys;
  auto carrier = validateInstanceBody(body, noKeys.asPtr(), fixture.store());
  ZC_REQUIRE(carrier != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(carrier).kind == MonoBodyRejectionKind::NonScalarCarrier);
}

ZC_TEST("MonoInstanceBody.rejectsNonScalarCarrierType") {
  MonoCoreFixture fixture;
  const auto u8 = fixture.primitive(tpsem::PrimitiveKind::U8);
  const auto genericDef = fixture.functionDefinition("take_u8"_zc);
  auto body = voidFunction(genericDef, u8);
  zc::Vector<GenericParameterKey> noKeys;

  auto rejection = validateInstanceBody(body, noKeys.asPtr(), fixture.store());
  ZC_REQUIRE(rejection != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(rejection).kind == MonoBodyRejectionKind::NonScalarCarrier);
}

ZC_TEST("MonoInstanceBody.rejectsReferenceCarrierType") {
  MonoCoreFixture fixture;
  const auto i32 = fixture.primitive(tpsem::PrimitiveKind::I32);
  auto canonical = fixture.store().canonicalizeClosed(
      tpsem::TypeData(tpsem::ReferenceTypeData{tpsem::Mutability::Const, i32}));
  ZC_REQUIRE(canonical.is<tpsem::CanonicalTypeData>());
  auto interned = fixture.store().intern(zc::mv(canonical).get<tpsem::CanonicalTypeData>());
  ZC_REQUIRE(interned.is<type::SemanticTypeInterned>());
  const auto reference = interned.get<type::SemanticTypeInterned>().id;
  const auto genericDef = fixture.functionDefinition("ref_identity"_zc);
  auto body = voidFunction(genericDef, reference);
  zc::Vector<GenericParameterKey> noKeys;

  auto rejection = validateInstanceBody(body, noKeys.asPtr(), fixture.store());
  ZC_REQUIRE(rejection != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(rejection).kind == MonoBodyRejectionKind::ReferenceCarrier);
}

MirFunction statementBody(DefId owner, identity::SemanticTypeId scalar, MirStatement&& statement) {
  zc::Vector<MirStatement> statements;
  statements.add(zc::mv(statement));
  zc::Vector<MirBasicBlock> blocks;
  blocks.add(
      MirBasicBlock{blockId(1), scopeId(1), zc::mv(statements), MirTerminator::returnVoid(span())});
  return functionWithBlocks(owner, scalar, zc::mv(blocks));
}

ZC_TEST("MonoInstanceBody.rejectsBorrowCreationStatement") {
  MonoCoreFixture fixture;
  const auto i32 = fixture.primitive(tpsem::PrimitiveKind::I32);
  const auto genericDef = fixture.functionDefinition("borrow_body"_zc);
  auto statement = MirStatement::borrowCreation(placeOf(localId(1), i32), MirBorrowKind::Shared,
                                                placeOf(localId(1), i32), span());
  auto body = statementBody(genericDef, i32, zc::mv(statement));
  zc::Vector<GenericParameterKey> noKeys;

  auto rejection = validateInstanceBody(body, noKeys.asPtr(), fixture.store());
  ZC_REQUIRE(rejection != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(rejection).kind == MonoBodyRejectionKind::BorrowCreationStatement);
}

ZC_TEST("MonoInstanceBody.rejectsUnsafeScopeBoundaryStatement") {
  MonoCoreFixture fixture;
  const auto i32 = fixture.primitive(tpsem::PrimitiveKind::I32);
  const auto genericDef = fixture.functionDefinition("unsafe_body"_zc);
  auto statement =
      MirStatement::unsafeScopeBoundary(MirUnsafeScopeBoundaryKind::Enter, scopeId(1), span());
  auto body = statementBody(genericDef, i32, zc::mv(statement));
  zc::Vector<GenericParameterKey> noKeys;

  auto rejection = validateInstanceBody(body, noKeys.asPtr(), fixture.store());
  ZC_REQUIRE(rejection != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(rejection).kind ==
            MonoBodyRejectionKind::UnsafeScopeBoundaryStatement);
}

ZC_TEST("MonoInstanceBody.rejectsDeinitializeStatement") {
  MonoCoreFixture fixture;
  const auto i32 = fixture.primitive(tpsem::PrimitiveKind::I32);
  const auto genericDef = fixture.functionDefinition("deinit_body"_zc);
  auto statement = MirStatement::deinitialize(placeOf(localId(1), i32), span());
  auto body = statementBody(genericDef, i32, zc::mv(statement));
  zc::Vector<GenericParameterKey> noKeys;

  auto rejection = validateInstanceBody(body, noKeys.asPtr(), fixture.store());
  ZC_REQUIRE(rejection != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(rejection).kind == MonoBodyRejectionKind::DeinitializeStatement);
}

ZC_TEST("MonoInstanceBody.acceptsAdmittedScalarBodyWithNoActivationCalls") {
  MonoCoreFixture fixture;
  const auto i32 = fixture.primitive(tpsem::PrimitiveKind::I32);
  const auto genericDef = fixture.functionDefinition("plain_body"_zc);
  auto body = voidFunction(genericDef, i32);
  zc::Vector<GenericParameterKey> noKeys;

  ZC_EXPECT(validateInstanceBody(body, noKeys.asPtr(), fixture.store()) == zc::none);
}

ZC_TEST("MonoInstanceBody.acceptsI64ScalarCarrier") {
  MonoCoreFixture fixture;
  const auto i64 = fixture.primitive(tpsem::PrimitiveKind::I64);
  const auto genericDef = fixture.functionDefinition("plain_i64_body"_zc);
  auto body = voidFunction(genericDef, i64);
  zc::Vector<GenericParameterKey> noKeys;

  ZC_EXPECT(validateInstanceBody(body, noKeys.asPtr(), fixture.store()) == zc::none);
}

ZC_TEST("MonoCallEdges.rejectsArityMismatchAgainstCalleeGenericParameters") {
  MonoCoreFixture fixture;
  FakeProvider provider(fixture);
  const auto i32 = fixture.primitive(tpsem::PrimitiveKind::I32);
  const auto genericDef = fixture.functionDefinition("identity"_zc);
  const auto key = fixture.genericKey(genericDef, 0);
  const auto parameter = fixture.typeParameter(key);
  zc::Vector<GenericParameterKey> twoKeys;
  twoKeys.add(key.clone());
  twoKeys.add(fixture.genericKey(genericDef, 1));
  provider.setTemplate(genericDef, voidFunction(genericDef, parameter), zc::mv(twoKeys));
  provider.addSubstitution(key.clone(), i32);
  provider.addEmptyWitness();
  provider.freeze();
  const auto instance =
      fixture.instance(genericDef, provider.substitutionAt(0), provider.witnessAt(0));
  auto body = callingRoot(genericDef, parameter, MirCallableIdentity::instance(instance));

  auto rejection = validateCallEdges(body, provider, [](InstanceId) { return true; });
  ZC_REQUIRE(rejection != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(rejection).kind == MonoEdgeRejectionKind::ArityMismatch);
}

ZC_TEST("MonoCallEdges.rejectsParameterKeyMismatch") {
  MonoCoreFixture fixture;
  FakeProvider provider(fixture);
  const auto i32 = fixture.primitive(tpsem::PrimitiveKind::I32);
  const auto genericDef = fixture.functionDefinition("identity"_zc);
  const auto expectedKey = fixture.genericKey(genericDef, 0);
  const auto foreignKey = fixture.genericKey(genericDef, 1);
  const auto parameter = fixture.typeParameter(expectedKey);
  zc::Vector<GenericParameterKey> expectedKeys;
  expectedKeys.add(expectedKey.clone());
  provider.setTemplate(genericDef, voidFunction(genericDef, parameter), zc::mv(expectedKeys));
  provider.addSubstitution(foreignKey.clone(), i32);
  provider.addEmptyWitness();
  provider.freeze();
  const auto instance =
      fixture.instance(genericDef, provider.substitutionAt(0), provider.witnessAt(0));
  auto body = callingRoot(genericDef, parameter, MirCallableIdentity::instance(instance));

  auto rejection = validateCallEdges(body, provider, [](InstanceId) { return true; });
  ZC_REQUIRE(rejection != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(rejection).kind == MonoEdgeRejectionKind::ParameterKeyMismatch);
}

ZC_TEST("MonoCallEdges.rejectsEdgeThatIsNotAMemberOfThePublishedSet") {
  MonoCoreFixture fixture;
  FakeProvider provider(fixture);
  const auto i32 = fixture.primitive(tpsem::PrimitiveKind::I32);
  const auto genericDef = fixture.functionDefinition("identity"_zc);
  const auto key = fixture.genericKey(genericDef, 0);
  const auto parameter = fixture.typeParameter(key);
  zc::Vector<GenericParameterKey> keys;
  keys.add(key.clone());
  provider.setTemplate(genericDef, voidFunction(genericDef, parameter), zc::mv(keys));
  provider.addSubstitution(key.clone(), i32);
  provider.addEmptyWitness();
  provider.freeze();
  const auto instance =
      fixture.instance(genericDef, provider.substitutionAt(0), provider.witnessAt(0));
  auto body = callingRoot(genericDef, parameter, MirCallableIdentity::instance(instance));

  auto rejection = validateCallEdges(body, provider, [](InstanceId) { return false; });
  ZC_REQUIRE(rejection != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(rejection).kind == MonoEdgeRejectionKind::EdgeNotClosed);
}

ZC_TEST("MonoTypeWalk.visitsEverySemanticTypeSlotFamily") {
  MonoCoreFixture fixture;
  const auto genericDef = fixture.functionDefinition("walked"_zc);
  const auto fieldDef = fixture.functionDefinition("field"_zc);
  const auto key = fixture.genericKey(genericDef, 0);
  const auto parameter = fixture.typeParameter(key);
  // Result (1) and one local (1).
  zc::Vector<MirLocalDeclaration> locals;
  locals.add(
      MirLocalDeclaration{localId(1), MirLocalKind::FunctionResult, parameter, scopeId(1), span()});
  locals.add(
      MirLocalDeclaration{localId(2), MirLocalKind::UserLocal, parameter, scopeId(1), span()});
  // Place with one field projection: root + result + projection input + result (4).
  zc::Vector<MirProjection> projections;
  projections.add(MirProjection::field(fieldDef, parameter, parameter));
  MirPlace destination(localId(2), parameter, zc::mv(projections), parameter);
  // Comparison rvalue: left constant, right constant, result (3).
  MirRvalue comparison = MirRvalue::comparison(
      mir::MirComparisonOperator::Eq, MirOperand::constant(parameter, integerConst(1)),
      MirOperand::constant(parameter, integerConst(2)), parameter);
  zc::Vector<MirStatement> statements;
  statements.add(MirStatement::assign(zc::mv(destination), zc::mv(comparison),
                                      MirInitializationKind::Initialize, span()));
  zc::Vector<MirBasicBlock> blocks;
  blocks.add(
      MirBasicBlock{blockId(1), scopeId(1), zc::mv(statements), MirTerminator::returnVoid(span())});
  zc::Vector<MirSourceScope> scopes;
  scopes.add(MirSourceScope{scopeId(1), zc::none, span()});
  MirFunction body{genericDef,
                   MirFunctionKind::Function,
                   identity::DefinitionKind::Function,
                   parameter,
                   span(),
                   zc::mv(scopes),
                   zc::mv(locals),
                   zc::mv(blocks)};

  uint64_t count = 0;
  forEachFunctionTypeSlot(body, [&](identity::SemanticTypeId) { ++count; });
  // Result (1), two locals (2), one projected place (4), one comparison rvalue (3).
  ZC_EXPECT(count == 10);
}

}  // namespace zomlang::compiler::mono
