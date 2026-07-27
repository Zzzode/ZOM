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

#include "zomlang/compiler/type/semantic-type-store.h"

#include "zc/core/encoding.h"
#include "zc/core/thread.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::type {
namespace {

class StoreFixture final {
public:
  explicit StoreFixture(bool freezeDefinitionRegistry = true) {
    auto issuedContext = factory.issue();
    ZC_REQUIRE(issuedContext != zc::none);
    ZC_IF_SOME(value, issuedContext) { context = value; }

    auto created = identity::SemanticIdentityRegistrySet::create(factory, context);
    ZC_REQUIRE(created != zc::none);
    ZC_IF_SOME(value, created) {
      registries = zc::heap<identity::SemanticIdentityRegistrySet>(zc::mv(value));
    }
    buildRegistry(freezeDefinitionRegistry);

    auto token = factory.issueSemanticTypeStoreConstructionToken(context);
    ZC_REQUIRE(token != zc::none);
    ZC_IF_SOME(value, token) { store = zc::heap<SemanticTypeStore>(zc::mv(value), *registries); }
  }

  semantic::CanonicalTypeData canonicalize(semantic::TypeData&& data) {
    auto result = store->canonicalizeClosed(zc::mv(data));
    ZC_REQUIRE(result.is<semantic::CanonicalTypeData>());
    return zc::mv(result.get<semantic::CanonicalTypeData>());
  }

  identity::SemanticTypeId intern(semantic::TypeData&& data) {
    auto result = store->intern(canonicalize(zc::mv(data)));
    ZC_REQUIRE(result.is<SemanticTypeInterned>());
    return result.get<SemanticTypeInterned>().id;
  }

  identity::DefId nominalDefinition() const {
    ZC_REQUIRE(nominalDefinitionValue.isValid());
    return nominalDefinitionValue;
  }

  identity::SemanticContextFactory factory;
  identity::SemanticContextBrand context;
  zc::Own<identity::SemanticIdentityRegistrySet> registries;
  zc::Own<SemanticTypeStore> store;

private:
  void buildRegistry(bool freezeDefinitionRegistry) {
    using namespace tests::test_identity_detail;
    ZC_REQUIRE(registries->collectCompilationUnit(userUnit()) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezeCompilationUnits() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->collectCrate(crate()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezeCrates() == identity::FrozenRegistryFailure::None);
    auto snapshot = identity::ImmutableSourceSnapshot::from(tests::test_identity_detail::source(),
                                                            zc::heapArray<uint8_t>(1, uint8_t{0}));
    ZC_REQUIRE(snapshot != zc::none);
    ZC_IF_SOME(value, snapshot) {
      ZC_REQUIRE(registries->collectSourceFile(zc::mv(value)) ==
                 identity::FrozenRegistryFailure::None);
    }
    ZC_REQUIRE(registries->freezeSourceFiles() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->collectModule(module()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezeModules() == identity::FrozenRegistryFailure::None);
    if (!freezeDefinitionRegistry) return;

    zc::Vector<identity::EnclosingStableOwnerKey> owners;
    zc::Maybe<identity::OverloadHeaderDigest> noOverloadDigest;
    auto record = identity::DefinitionIdentityRecord::from(
        module(), zc::mv(owners), identity::DefinitionKind::Class,
        identity::DefinitionNamespace::Type, scalar<identity::DeclaredDefinitionName>("nominal"_zc),
        zc::mv(noOverloadDigest));
    ZC_REQUIRE(record != zc::none);
    zc::Maybe<identity::DefinitionKey> retained;
    ZC_IF_SOME(value, record) {
      retained = identity::DefinitionKey::compute(value);
      zc::Maybe<identity::OverloadHeaderAuthority> noOverloadAuthority;
      ZC_REQUIRE(registries->collectDefinition(zc::mv(value), zc::mv(noOverloadAuthority)) ==
                 identity::FrozenRegistryFailure::None);
    }
    ZC_REQUIRE(registries->freezeStableIdentities() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezeGenericParameters() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezeCallableParameters() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(retained != zc::none);
    ZC_IF_SOME(value, retained) {
      auto id = registries->definitions().find(value);
      ZC_REQUIRE(id != zc::none);
      ZC_IF_SOME(found, id) { nominalDefinitionValue = found; }
    }
  }

  identity::DefId nominalDefinitionValue;
};

identity::SemanticTypeId tryInternPrimitive(SemanticTypeStore& store,
                                            semantic::PrimitiveKind kind) {
  auto canonical = store.canonicalizeClosed(semantic::TypeData(semantic::PrimitiveTypeData{kind}));
  if (canonical.is<semantic::CanonicalTypeData>()) {
    auto result = store.intern(zc::mv(canonical.get<semantic::CanonicalTypeData>()));
    if (result.is<SemanticTypeInterned>()) { return result.get<SemanticTypeInterned>().id; }
  }
  return identity::SemanticTypeId();
}

void expectInvariant(const SemanticTypeAdmissionResult& result,
                     identity::IdentityInvariantKind kind) {
  ZC_REQUIRE(result.is<identity::IdentityInvariant>());
  ZC_EXPECT(result.get<identity::IdentityInvariant>().kind() == kind);
}

void expectEqualInterningForWorkerCount(size_t workerCount) {
  StoreFixture fixture;
  identity::SemanticTypeId ids[8];
  {
    zc::Own<zc::Thread> workers[8];
    for (size_t index = 0; index < workerCount; ++index) {
      workers[index] = zc::heap<zc::Thread>([&, index]() {
        ids[index] = tryInternPrimitive(*fixture.store, semantic::PrimitiveKind::I32);
      });
    }
  }

  ZC_REQUIRE(ids[0].isValid());
  for (size_t index = 1; index < workerCount; ++index) { ZC_EXPECT(ids[index] == ids[0]); }
  ZC_EXPECT(fixture.store->size() == 1);
}

}  // namespace

ZC_TEST("SemanticTypeStore.StablePrimitiveIdentity") {
  StoreFixture fixture;
  const auto firstId =
      fixture.intern(semantic::TypeData(semantic::PrimitiveTypeData{semantic::PrimitiveKind::I32}));
  const auto secondId =
      fixture.intern(semantic::TypeData(semantic::PrimitiveTypeData{semantic::PrimitiveKind::I32}));

  ZC_EXPECT(firstId == secondId);
  ZC_EXPECT(firstId.belongsTo(fixture.context));
  auto lookup = fixture.store->get(firstId);
  ZC_REQUIRE(lookup.is<SemanticTypeLookup>());
  const auto& found = lookup.get<SemanticTypeLookup>();
  ZC_EXPECT(found.data().tag() == semantic::TypeDataTag::Primitive);
  ZC_EXPECT(found.data().get<semantic::PrimitiveTypeData>().kind == semantic::PrimitiveKind::I32);
  ZC_EXPECT(zc::encodeHex(found.key().bytes()) ==
            "7a6f6d2e73656d616e7469632d747970652d6b6579000103"_zc);
  ZC_EXPECT(fixture.store->size() == 1);
}

ZC_TEST("SemanticTypeStore.CanonicalCompositeForms") {
  StoreFixture fixture;
  const auto i32 =
      fixture.intern(semantic::TypeData(semantic::PrimitiveTypeData{semantic::PrimitiveKind::I32}));
  const auto str =
      fixture.intern(semantic::TypeData(semantic::PrimitiveTypeData{semantic::PrimitiveKind::Str}));

  zc::Vector<identity::SemanticTypeId> firstElements;
  firstElements.add(i32);
  firstElements.add(str);
  const auto firstTuple =
      fixture.intern(semantic::TypeData(semantic::TupleTypeData{zc::mv(firstElements)}));
  zc::Vector<identity::SemanticTypeId> secondElements;
  secondElements.add(i32);
  secondElements.add(str);
  const auto secondTuple =
      fixture.intern(semantic::TypeData(semantic::TupleTypeData{zc::mv(secondElements)}));

  const auto reference = fixture.intern(
      semantic::TypeData(semantic::ReferenceTypeData{semantic::Mutability::Const, firstTuple}));
  zc::Vector<identity::SemanticTypeId> alternatives;
  alternatives.add(i32);
  alternatives.add(str);
  const auto unionType =
      fixture.intern(semantic::TypeData(semantic::UnionTypeData{zc::mv(alternatives)}));

  ZC_EXPECT(firstTuple == secondTuple);
  ZC_EXPECT(reference != unionType);
  ZC_EXPECT(fixture.store->size() == 5);
}

ZC_TEST("SemanticTypeStore.RejectsForeignAndInvalidIdentities") {
  StoreFixture first;
  StoreFixture second;
  const auto firstId =
      first.intern(semantic::TypeData(semantic::PrimitiveTypeData{semantic::PrimitiveKind::I32}));
  const auto secondId =
      second.intern(semantic::TypeData(semantic::PrimitiveTypeData{semantic::PrimitiveKind::I32}));

  ZC_EXPECT(firstId != secondId);
  auto foreign = first.store->get(secondId);
  ZC_REQUIRE(foreign.is<identity::IdentityInvariant>());
  ZC_EXPECT(foreign.get<identity::IdentityInvariant>().kind() ==
            identity::IdentityInvariantKind::ForeignContext);
  auto invalid = first.store->get(identity::SemanticTypeId());
  ZC_REQUIRE(invalid.is<identity::IdentityInvariant>());
  ZC_EXPECT(invalid.get<identity::IdentityInvariant>().kind() ==
            identity::IdentityInvariantKind::InvalidHandle);

  auto foreignChild = second.store->canonicalizeClosed(
      semantic::TypeData(semantic::ReferenceTypeData{semantic::Mutability::Const, firstId}));
  expectInvariant(foreignChild, identity::IdentityInvariantKind::ForeignContext);

  zc::Vector<identity::SemanticTypeId> noArguments;
  auto foreignDefinition = second.store->canonicalizeClosed(semantic::TypeData(
      semantic::NominalTypeData{first.nominalDefinition(), zc::mv(noArguments)}));
  expectInvariant(foreignDefinition, identity::IdentityInvariantKind::ForeignContext);
}

ZC_TEST("SemanticTypeStore.RejectsForeignCanonicalAdmission") {
  StoreFixture first;
  StoreFixture second;
  auto canonical = first.store->canonicalizeClosed(
      semantic::TypeData(semantic::PrimitiveTypeData{semantic::PrimitiveKind::I32}));
  ZC_REQUIRE(canonical.is<semantic::CanonicalTypeData>());
  auto result = second.store->intern(zc::mv(canonical.get<semantic::CanonicalTypeData>()));
  ZC_REQUIRE(result.is<identity::IdentityInvariant>());
  ZC_EXPECT(result.get<identity::IdentityInvariant>().kind() ==
            identity::IdentityInvariantKind::ForeignContext);
  ZC_EXPECT(first.store->size() == 0);
  ZC_EXPECT(second.store->size() == 0);
}

ZC_TEST("SemanticTypeStore.RejectsUnfrozenDefinitionRegistry") {
  StoreFixture fixture(false);
  zc::Vector<identity::SemanticTypeId> noArguments;
  auto result = fixture.store->canonicalizeClosed(
      semantic::TypeData(semantic::NominalTypeData{identity::DefId(), zc::mv(noArguments)}));
  expectInvariant(result, identity::IdentityInvariantKind::AncestorMismatch);
}

ZC_TEST("SemanticTypeStore.RetainsStableLookupReferencesDuringGrowth") {
  StoreFixture fixture;
  const auto i32 =
      fixture.intern(semantic::TypeData(semantic::PrimitiveTypeData{semantic::PrimitiveKind::I32}));
  auto lookup = fixture.store->get(i32);
  ZC_REQUIRE(lookup.is<SemanticTypeLookup>());
  const auto& found = lookup.get<SemanticTypeLookup>();

  {
    zc::Thread growth([&]() {
      for (uint64_t length = 1; length <= 256; ++length) {
        fixture.intern(semantic::TypeData(semantic::FixedArrayTypeData{i32, length}));
      }
    });
    for (size_t iteration = 0; iteration < 1024; ++iteration) {
      ZC_EXPECT(found.data().tag() == semantic::TypeDataTag::Primitive);
      ZC_EXPECT(found.data().get<semantic::PrimitiveTypeData>().kind ==
                semantic::PrimitiveKind::I32);
      auto current = fixture.store->get(i32);
      ZC_REQUIRE(current.is<SemanticTypeLookup>());
      ZC_EXPECT(current.get<SemanticTypeLookup>().key().bytes() == found.key().bytes());
    }
  }

  ZC_EXPECT(zc::encodeHex(found.key().bytes()) ==
            "7a6f6d2e73656d616e7469632d747970652d6b6579000103"_zc);
  ZC_EXPECT(fixture.store->size() == 257);
}

ZC_TEST("SemanticTypeStore.ConcurrentEqualInterningIsWorkerCountIndependent") {
  const size_t workerCounts[] = {1, 2, 4, 8};
  for (const auto workerCount : workerCounts) { expectEqualInterningForWorkerCount(workerCount); }
}

ZC_TEST("SemanticTypeStore.TokenIsSingleUsePerContext") {
  identity::SemanticContextFactory factory;
  auto issuedContext = factory.issue();
  ZC_REQUIRE(issuedContext != zc::none);
  ZC_IF_SOME(context, issuedContext) {
    auto first = factory.issueSemanticTypeStoreConstructionToken(context);
    ZC_EXPECT(first != zc::none);
    ZC_EXPECT(factory.issueSemanticTypeStoreConstructionToken(context) == zc::none);
  }
}

}  // namespace zomlang::compiler::type
