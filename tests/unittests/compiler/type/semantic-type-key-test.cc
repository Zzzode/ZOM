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

#include "compiler/type/semantic-type-key.h"

#include "compiler/identity/canonical/identity-interner-set.h"
#include "compiler/type/semantic-type-store.h"
#include "zc/core/encoding.h"
#include "zc/core/memory.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::type::semantic {
namespace {

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto admitted = Scalar::fromCanonical(text);
  ZC_REQUIRE(admitted != zc::none);
  ZC_IF_SOME(value, admitted) { return zc::mv(value); }
  ZC_UNREACHABLE;
}

identity::PackageKey package() {
  zc::Vector<identity::CanonicalPathSegment> path;
  auto version = identity::ResolvedVersion::fromCanonical("0.0.0"_zc);
  zc::Vector<identity::FeatureName> features;
  auto sortedFeatures = identity::SortedFeatureSet::from(zc::mv(features));
  ZC_REQUIRE(version != zc::none);
  ZC_REQUIRE(sortedFeatures != zc::none);
  ZC_IF_SOME(versionValue, version) {
    ZC_IF_SOME(featureValues, sortedFeatures) {
      return identity::PackageKey::from(
          identity::CanonicalPackageSource::localPath(
              identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(path))),
          scalar<identity::PackageName>("test"_zc), zc::mv(versionValue), zc::mv(featureValues));
    }
  }
  ZC_UNREACHABLE;
}

identity::CanonicalTargetSpecificationKey target() {
  zc::Vector<identity::TargetFeatureName> features;
  auto sorted = identity::SortedTargetFeatureSet::from(zc::mv(features));
  ZC_REQUIRE(sorted != zc::none);
  ZC_IF_SOME(values, sorted) {
    auto admitted = identity::CanonicalTargetSpecificationKey::from(
        scalar<identity::TargetComponentName>("x"_zc),
        scalar<identity::TargetComponentName>("v"_zc),
        scalar<identity::TargetComponentName>("o"_zc),
        scalar<identity::TargetComponentName>("e"_zc),
        scalar<identity::TargetComponentName>("a"_zc), 64, identity::Endianness::Little,
        zc::mv(values));
    ZC_REQUIRE(admitted != zc::none);
    ZC_IF_SOME(value, admitted) { return zc::mv(value); }
  }
  ZC_UNREACHABLE;
}

identity::CrateKey crate() {
  zc::Maybe<identity::BuildScriptProducerKey> noOutput;
  auto compilation = identity::CompilationConfigKey::from(
      identity::CompilationDomain::Target, target(),
      identity::SemanticCompilerOptionsKey::from(2026, true, false, true), zc::mv(noOutput));
  ZC_REQUIRE(compilation != zc::none);
  ZC_IF_SOME(value, compilation) {
    auto admitted = identity::CrateKey::from(
        identity::CompilationUnitIdentity::userPackage(package()),
        identity::CrateTargetKind::Library, scalar<identity::TargetName>("test"_zc), zc::mv(value));
    ZC_REQUIRE(admitted != zc::none);
    ZC_IF_SOME(result, admitted) { return zc::mv(result); }
  }
  ZC_UNREACHABLE;
}

identity::ModuleKey module() {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(scalar<identity::ModulePathSegment>("test"_zc));
  auto admitted = identity::ModuleKey::from(crate(), zc::mv(path));
  ZC_REQUIRE(admitted != zc::none);
  ZC_IF_SOME(value, admitted) { return zc::mv(value); }
  ZC_UNREACHABLE;
}

class SemanticTypeStoreFixture final {
public:
  SemanticTypeStoreFixture() {
    auto issuedContext = factory.issue();
    ZC_REQUIRE(issuedContext != zc::none);
    ZC_IF_SOME(value, issuedContext) { contextValue = value; }

    auto createdInterner = identity::IdentityInternerSet::create(factory, contextValue);
    ZC_REQUIRE(createdInterner != zc::none);
    ZC_IF_SOME(value, createdInterner) {
      identitiesValue = zc::heap<identity::IdentityInternerSet>(zc::mv(value));
    }
    buildIdentityAuthority();

    auto token = factory.issueSemanticTypeStoreConstructionToken(contextValue);
    ZC_REQUIRE(token != zc::none);
    ZC_IF_SOME(value, token) {
      storeValue = zc::heap<type::SemanticTypeStore>(zc::mv(value), *identitiesValue);
    }
  }

  type::SemanticTypeStore& store() { return *storeValue; }
  const identity::GenericParameterKey& parameter() const {
    ZC_IF_SOME(value, parameterKeyValue) { return value; }
    ZC_UNREACHABLE;
  }

private:
  void buildIdentityAuthority() {
    zc::Vector<identity::EnclosingStableOwnerKey> owners;
    zc::Maybe<identity::OverloadHeaderDigest> noOverload;
    auto definition = identity::DefinitionIdentityRecord::from(
        module(), zc::mv(owners), identity::DefinitionKind::Class,
        identity::DefinitionNamespace::Type, scalar<identity::DeclaredDefinitionName>("Owner"_zc),
        zc::mv(noOverload));
    ZC_REQUIRE(definition != zc::none);
    ZC_IF_SOME(value, definition) {
      auto ownerKey = identity::DefinitionKey::compute(value);
      auto admitted = identitiesValue->internDefinition(contextValue, ownerKey, value);
      ZC_REQUIRE(admitted.is<identity::DefId>());
      auto parameter = identity::GenericParameterIdentityRecord::type(
          identity::StableGenericParameterOwnerKey::definition(zc::mv(ownerKey)), 0);
      auto parameterKey = identity::GenericParameterKey::compute(parameter);
      auto admittedParameter =
          identitiesValue->internGenericParameter(contextValue, parameterKey, parameter);
      ZC_REQUIRE(admittedParameter.is<identity::GenericParameterId>());
      parameterKeyValue = zc::mv(parameterKey);
    }
  }

  identity::SemanticContextFactory factory;
  identity::SemanticContextBrand contextValue;
  zc::Own<identity::IdentityInternerSet> identitiesValue;
  zc::Own<type::SemanticTypeStore> storeValue;
  zc::Maybe<identity::GenericParameterKey> parameterKeyValue;
};

}  // namespace

ZC_TEST("SemanticTypeKey.EncodesGenericParameterKeyAsRawDigest") {
  SemanticTypeStoreFixture fixture;
  const auto& parameter = fixture.parameter();
  auto canonical =
      fixture.store().canonicalizeClosed(TypeData(TypeParameterTypeData{parameter.clone()}));
  ZC_REQUIRE(canonical.is<CanonicalTypeData>());

  auto interned = fixture.store().intern(zc::mv(canonical.get<CanonicalTypeData>()));
  ZC_REQUIRE(interned.is<type::SemanticTypeInterned>());
  auto lookup = fixture.store().get(interned.get<type::SemanticTypeInterned>().id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());

  const auto& stored = lookup.get<type::SemanticTypeLookup>();
  ZC_EXPECT(stored.data().tag() == TypeDataTag::TypeParameter);
  ZC_EXPECT(stored.data().get<TypeParameterTypeData>().parameter == parameter);
  ZC_REQUIRE(stored.key().bytes().size() == 55);
  ZC_EXPECT(zc::encodeHex(stored.key().bytes().slice(0, 23)) ==
            "7a6f6d2e73656d616e7469632d747970652d6b65790009"_zc);
  ZC_EXPECT(stored.key().bytes().slice(23) == parameter.bytes());
}

ZC_TEST("SemanticTypeData.PreservesGenericParameterKeyCloneAndEquality") {
  SemanticTypeStoreFixture fixture;
  const auto& parameter = fixture.parameter();
  TypeData data(TypeParameterTypeData{parameter.clone()});

  ZC_EXPECT(data.get<TypeParameterTypeData>().parameter == parameter);
  ZC_EXPECT(data.get<TypeParameterTypeData>().parameter.bytes().size() == 32);
  ZC_EXPECT(data.get<TypeParameterTypeData>().parameter.bytes() == parameter.bytes());
}

ZC_TEST("SemanticTypeKey.RejectsUnretainedGenericParameterKey") {
  SemanticTypeStoreFixture fixture;
  auto unretained =
      identity::GenericParameterKey::fromBytes(zc::heapArray<uint8_t>(32, uint8_t{0xff}));
  ZC_REQUIRE(unretained != zc::none);
  ZC_IF_SOME(value, unretained) {
    auto result =
        fixture.store().canonicalizeClosed(TypeData(TypeParameterTypeData{zc::mv(value)}));
    ZC_REQUIRE(result.is<identity::IdentityInvariant>());
    const auto& invariant = result.get<identity::IdentityInvariant>();
    ZC_EXPECT(invariant.kind() == identity::IdentityInvariantKind::InvalidHandle);
    ZC_EXPECT(invariant.phase() == identity::IdentityAllocationPhase::GenericParameter);
  }
}

namespace {

identity::SemanticTypeId internType(type::SemanticTypeStore& store, TypeData&& data) {
  auto canonical = store.canonicalizeClosed(zc::mv(data));
  ZC_REQUIRE(canonical.is<CanonicalTypeData>());
  auto result = store.intern(zc::mv(canonical.get<CanonicalTypeData>()));
  ZC_REQUIRE(result.is<type::SemanticTypeInterned>());
  return result.get<type::SemanticTypeInterned>().id;
}

}  // namespace

ZC_TEST("SemanticTypeKey.PrimitiveKeyHasExpectedPrefixAndTag") {
  SemanticTypeStoreFixture fixture;
  auto id = internType(fixture.store(), TypeData(PrimitiveTypeData{PrimitiveKind::I32}));
  auto lookup = fixture.store().get(id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
  const auto& stored = lookup.get<type::SemanticTypeLookup>();
  ZC_REQUIRE(stored.key().bytes().size() == 24);
  ZC_EXPECT(zc::encodeHex(stored.key().bytes().slice(0, 22)) ==
            "7a6f6d2e73656d616e7469632d747970652d6b657900"_zc);
  ZC_EXPECT(stored.key().bytes()[22] == 0x01);
  ZC_EXPECT(stored.key().bytes()[23] == 0x03);
}

ZC_TEST("SemanticTypeKey.PrimitiveKeyKindVariesByPrimitive") {
  SemanticTypeStoreFixture fixture;
  auto i32Id = internType(fixture.store(), TypeData(PrimitiveTypeData{PrimitiveKind::I32}));
  auto i64Id = internType(fixture.store(), TypeData(PrimitiveTypeData{PrimitiveKind::I64}));
  auto lookup32 = fixture.store().get(i32Id);
  auto lookup64 = fixture.store().get(i64Id);
  ZC_REQUIRE(lookup32.is<type::SemanticTypeLookup>());
  ZC_REQUIRE(lookup64.is<type::SemanticTypeLookup>());
  const auto& stored32 = lookup32.get<type::SemanticTypeLookup>();
  const auto& stored64 = lookup64.get<type::SemanticTypeLookup>();
  ZC_EXPECT(stored32.key().bytes()[23] == 0x03);
  ZC_EXPECT(stored64.key().bytes()[23] == 0x04);
  ZC_EXPECT(stored32.key().bytes() != stored64.key().bytes());
}

ZC_TEST("SemanticTypeKey.SameTypeProducesIdenticalKeyBytes") {
  SemanticTypeStoreFixture fixture;
  auto id1 = internType(fixture.store(), TypeData(PrimitiveTypeData{PrimitiveKind::Bool}));
  auto id2 = internType(fixture.store(), TypeData(PrimitiveTypeData{PrimitiveKind::Bool}));
  ZC_EXPECT(id1 == id2);
  auto lookup1 = fixture.store().get(id1);
  auto lookup2 = fixture.store().get(id2);
  ZC_REQUIRE(lookup1.is<type::SemanticTypeLookup>());
  ZC_REQUIRE(lookup2.is<type::SemanticTypeLookup>());
  const auto& stored1 = lookup1.get<type::SemanticTypeLookup>();
  const auto& stored2 = lookup2.get<type::SemanticTypeLookup>();
  ZC_EXPECT(stored1.key().bytes() == stored2.key().bytes());
}

ZC_TEST("SemanticTypeKey.CompositeKeyLongerThanPrimitiveKey") {
  SemanticTypeStoreFixture fixture;
  auto i32 = internType(fixture.store(), TypeData(PrimitiveTypeData{PrimitiveKind::I32}));
  auto boolType = internType(fixture.store(), TypeData(PrimitiveTypeData{PrimitiveKind::Bool}));
  zc::Vector<identity::SemanticTypeId> elements;
  elements.add(i32);
  elements.add(boolType);
  auto tupleId = internType(fixture.store(), TypeData(TupleTypeData{zc::mv(elements)}));
  auto primitiveLookup = fixture.store().get(i32);
  auto tupleLookup = fixture.store().get(tupleId);
  ZC_REQUIRE(primitiveLookup.is<type::SemanticTypeLookup>());
  ZC_REQUIRE(tupleLookup.is<type::SemanticTypeLookup>());
  const auto& primitiveKey = primitiveLookup.get<type::SemanticTypeLookup>().key();
  const auto& tupleKey = tupleLookup.get<type::SemanticTypeLookup>().key();
  ZC_EXPECT(tupleKey.bytes().size() > primitiveKey.bytes().size());
}

ZC_TEST("SemanticTypeKey.FixedArrayKeyIncludesLength") {
  SemanticTypeStoreFixture fixture;
  auto i32 = internType(fixture.store(), TypeData(PrimitiveTypeData{PrimitiveKind::I32}));
  auto id10 = internType(fixture.store(), TypeData(FixedArrayTypeData{i32, 10}));
  auto id20 = internType(fixture.store(), TypeData(FixedArrayTypeData{i32, 20}));
  ZC_EXPECT(id10 != id20);
  auto lookup10 = fixture.store().get(id10);
  auto lookup20 = fixture.store().get(id20);
  ZC_REQUIRE(lookup10.is<type::SemanticTypeLookup>());
  ZC_REQUIRE(lookup20.is<type::SemanticTypeLookup>());
  const auto& stored10 = lookup10.get<type::SemanticTypeLookup>();
  const auto& stored20 = lookup20.get<type::SemanticTypeLookup>();
  ZC_EXPECT(stored10.key().bytes() != stored20.key().bytes());
}

ZC_TEST("SemanticTypeKey.RejectsTupleWithOneElement") {
  SemanticTypeStoreFixture fixture;
  auto i32 = internType(fixture.store(), TypeData(PrimitiveTypeData{PrimitiveKind::I32}));
  zc::Vector<identity::SemanticTypeId> elements;
  elements.add(i32);
  auto result = fixture.store().canonicalizeClosed(TypeData(TupleTypeData{zc::mv(elements)}));
  ZC_REQUIRE(result.is<identity::IdentityInvariant>());
  const auto& invariant = result.get<identity::IdentityInvariant>();
  ZC_EXPECT(invariant.kind() == identity::IdentityInvariantKind::InvalidClosedValue);
}

ZC_TEST("SemanticTypeKey.RejectsInvalidPrimitiveKind") {
  SemanticTypeStoreFixture fixture;
  auto result = fixture.store().canonicalizeClosed(
      TypeData(PrimitiveTypeData{static_cast<PrimitiveKind>(0x00)}));
  ZC_REQUIRE(result.is<identity::IdentityInvariant>());
  const auto& invariant = result.get<identity::IdentityInvariant>();
  ZC_EXPECT(invariant.kind() == identity::IdentityInvariantKind::InvalidClosedValue);
}

ZC_TEST("SemanticTypeKey.RejectsUnionWithOneAlternative") {
  SemanticTypeStoreFixture fixture;
  auto i32 = internType(fixture.store(), TypeData(PrimitiveTypeData{PrimitiveKind::I32}));
  zc::Vector<identity::SemanticTypeId> alternatives;
  alternatives.add(i32);
  auto result = fixture.store().canonicalizeClosed(TypeData(UnionTypeData{zc::mv(alternatives)}));
  ZC_REQUIRE(result.is<identity::IdentityInvariant>());
  const auto& invariant = result.get<identity::IdentityInvariant>();
  ZC_EXPECT(invariant.kind() == identity::IdentityInvariantKind::InvalidClosedValue);
}

}  // namespace zomlang::compiler::type::semantic
