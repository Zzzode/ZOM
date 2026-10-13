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

#include "compiler/type/semantic-type-store.h"

#include "compiler/identity/canonical/identity-interner-set.h"
#include "compiler/type/semantic-type-data.h"
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

identity::ModuleKey moduleKey() {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(scalar<identity::ModulePathSegment>("test"_zc));
  auto admitted = identity::ModuleKey::from(crate(), zc::mv(path));
  ZC_REQUIRE(admitted != zc::none);
  ZC_IF_SOME(value, admitted) { return zc::mv(value); }
  ZC_UNREACHABLE;
}

identity::SemanticIdentifier identifier(zc::StringPtr text) {
  auto admitted = identity::SemanticIdentifier::fromCanonical(text);
  ZC_REQUIRE(admitted != zc::none);
  ZC_IF_SOME(value, admitted) { return zc::mv(value); }
  ZC_UNREACHABLE;
}

/// \brief Owns one semantic context with a type store and a small identity authority.
class TypeStoreFixture final {
public:
  TypeStoreFixture() {
    auto issuedContext = factory.issue();
    ZC_REQUIRE(issuedContext != zc::none);
    ZC_IF_SOME(value, issuedContext) { contextValue = value; }

    auto createdInterner = identity::IdentityInternerSet::create(factory, contextValue);
    ZC_REQUIRE(createdInterner != zc::none);
    ZC_IF_SOME(value, createdInterner) {
      identitiesValue = zc::heap<identity::IdentityInternerSet>(zc::mv(value));
    }

    auto token = factory.issueSemanticTypeStoreConstructionToken(contextValue);
    ZC_REQUIRE(token != zc::none);
    ZC_IF_SOME(value, token) {
      storeValue = zc::heap<type::SemanticTypeStore>(zc::mv(value), *identitiesValue);
    }

    classDefValue = internDefinition(identity::DefinitionKind::Class, "Foo"_zc);
    structDefValue = internDefinition(identity::DefinitionKind::Struct, "Bar"_zc);
    enumDefValue = internDefinition(identity::DefinitionKind::Enum, "Baz"_zc);
    interfaceDefValue = internDefinition(identity::DefinitionKind::Interface, "Qux"_zc);
    buildGenericParameter();
  }

  type::SemanticTypeStore& store() { return *storeValue; }
  identity::SemanticContextBrand context() const noexcept { return contextValue; }
  identity::DefId classDef() const noexcept { return classDefValue; }
  identity::DefId structDef() const noexcept { return structDefValue; }
  identity::DefId enumDef() const noexcept { return enumDefValue; }
  identity::DefId interfaceDef() const noexcept { return interfaceDefValue; }

  const identity::GenericParameterKey& parameter() const {
    ZC_IF_SOME(value, parameterKeyValue) { return value; }
    ZC_UNREACHABLE;
  }

  /// \brief Canonicalizes and interns one closed type payload through the production boundary.
  identity::SemanticTypeId intern(TypeData&& data) {
    auto canonical = storeValue->canonicalizeClosed(zc::mv(data));
    ZC_REQUIRE(canonical.is<CanonicalTypeData>());
    auto result = storeValue->intern(zc::mv(canonical.get<CanonicalTypeData>()));
    ZC_REQUIRE(result.is<type::SemanticTypeInterned>());
    return result.get<type::SemanticTypeInterned>().id;
  }

  /// \brief Convenience helper for interning a primitive type.
  identity::SemanticTypeId internPrimitive(PrimitiveKind kind) {
    return intern(TypeData(PrimitiveTypeData{kind}));
  }

private:
  identity::DefId internDefinition(identity::DefinitionKind kind, zc::StringPtr name) {
    zc::Vector<identity::EnclosingStableOwnerKey> owners;
    zc::Maybe<identity::OverloadHeaderDigest> noOverload;
    auto declaredName = identity::DeclaredDefinitionName::fromCanonical(name);
    ZC_REQUIRE(declaredName != zc::none);
    ZC_IF_SOME(nameValue, declaredName) {
      auto record = identity::DefinitionIdentityRecord::from(moduleKey(), zc::mv(owners), kind,
                                                             identity::DefinitionNamespace::Type,
                                                             zc::mv(nameValue), zc::mv(noOverload));
      ZC_REQUIRE(record != zc::none);
      ZC_IF_SOME(recordValue, record) {
        auto key = identity::DefinitionKey::compute(recordValue);
        auto admitted = identitiesValue->internDefinition(contextValue, key, recordValue);
        ZC_REQUIRE(admitted.is<identity::DefId>());
        return admitted.get<identity::DefId>();
      }
    }
    ZC_UNREACHABLE;
  }

  void buildGenericParameter() {
    ZC_IF_SOME(entry, identitiesValue->definition(classDefValue)) {
      auto ownerKey = identity::StableGenericParameterOwnerKey::definition(entry.key().clone());
      auto record = identity::GenericParameterIdentityRecord::type(zc::mv(ownerKey), 0);
      auto key = identity::GenericParameterKey::compute(record);
      auto admitted = identitiesValue->internGenericParameter(contextValue, key, record);
      ZC_REQUIRE(admitted.is<identity::GenericParameterId>());
      parameterKeyValue = zc::mv(key);
    }
  }

  identity::SemanticContextFactory factory;
  identity::SemanticContextBrand contextValue;
  zc::Own<identity::IdentityInternerSet> identitiesValue;
  zc::Own<type::SemanticTypeStore> storeValue;
  identity::DefId classDefValue;
  identity::DefId structDefValue;
  identity::DefId enumDefValue;
  identity::DefId interfaceDefValue;
  zc::Maybe<identity::GenericParameterKey> parameterKeyValue;
};

}  // namespace

// ---------------------------------------------------------------------------
// Store creation and basic properties
// ---------------------------------------------------------------------------

ZC_TEST("SemanticTypeStore.EmptyStoreHasZeroSize") {
  TypeStoreFixture fixture;
  ZC_EXPECT(fixture.store().size() == 0);
}

ZC_TEST("SemanticTypeStore.ContextMatchesIssuedBrand") {
  TypeStoreFixture fixture;
  ZC_EXPECT(fixture.store().context() == fixture.context());
  ZC_EXPECT(fixture.store().context().isValid());
}

// ---------------------------------------------------------------------------
// Primitive types
// ---------------------------------------------------------------------------

ZC_TEST("SemanticTypeStore.InternAllPrimitiveKinds") {
  TypeStoreFixture fixture;
  constexpr PrimitiveKind kinds[] = {
      PrimitiveKind::I8,    PrimitiveKind::I16,   PrimitiveKind::I32, PrimitiveKind::I64,
      PrimitiveKind::U8,    PrimitiveKind::U16,   PrimitiveKind::U32, PrimitiveKind::U64,
      PrimitiveKind::Isize, PrimitiveKind::Usize, PrimitiveKind::F32, PrimitiveKind::F64,
      PrimitiveKind::Bool,  PrimitiveKind::Char,  PrimitiveKind::Str, PrimitiveKind::Unit,
      PrimitiveKind::Never, PrimitiveKind::Any,   PrimitiveKind::Null};
  for (const auto kind : kinds) {
    auto id = fixture.internPrimitive(kind);
    auto lookup = fixture.store().get(id);
    ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
    const auto& data = lookup.get<type::SemanticTypeLookup>().data();
    ZC_EXPECT(data.tag() == TypeDataTag::Primitive);
    ZC_EXPECT(data.get<PrimitiveTypeData>().kind == kind);
  }
  ZC_EXPECT(fixture.store().size() == 19);
}

ZC_TEST("SemanticTypeStore.DeduplicatesSamePrimitive") {
  TypeStoreFixture fixture;
  auto id1 = fixture.internPrimitive(PrimitiveKind::I32);
  auto id2 = fixture.internPrimitive(PrimitiveKind::I32);
  ZC_EXPECT(id1 == id2);
  ZC_EXPECT(fixture.store().size() == 1);
}

ZC_TEST("SemanticTypeStore.DifferentPrimitivesProduceDifferentIds") {
  TypeStoreFixture fixture;
  auto id1 = fixture.internPrimitive(PrimitiveKind::I32);
  auto id2 = fixture.internPrimitive(PrimitiveKind::I64);
  ZC_EXPECT(id1 != id2);
  ZC_EXPECT(fixture.store().size() == 2);
}

// ---------------------------------------------------------------------------
// Composite types
// ---------------------------------------------------------------------------

ZC_TEST("SemanticTypeStore.InternTuple") {
  TypeStoreFixture fixture;
  auto i32 = fixture.internPrimitive(PrimitiveKind::I32);
  auto boolType = fixture.internPrimitive(PrimitiveKind::Bool);
  zc::Vector<identity::SemanticTypeId> elements;
  elements.add(i32);
  elements.add(boolType);
  auto id = fixture.intern(TypeData(TupleTypeData{zc::mv(elements)}));
  auto lookup = fixture.store().get(id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_EXPECT(data.tag() == TypeDataTag::Tuple);
  ZC_EXPECT(data.get<TupleTypeData>().elements.size() == 2);
  ZC_EXPECT(data.get<TupleTypeData>().elements[0] == i32);
  ZC_EXPECT(data.get<TupleTypeData>().elements[1] == boolType);
}

ZC_TEST("SemanticTypeStore.InternTupleThreeElements") {
  TypeStoreFixture fixture;
  auto i32 = fixture.internPrimitive(PrimitiveKind::I32);
  auto boolType = fixture.internPrimitive(PrimitiveKind::Bool);
  auto i64 = fixture.internPrimitive(PrimitiveKind::I64);
  zc::Vector<identity::SemanticTypeId> elements;
  elements.add(i32);
  elements.add(boolType);
  elements.add(i64);
  auto id = fixture.intern(TypeData(TupleTypeData{zc::mv(elements)}));
  auto lookup = fixture.store().get(id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_EXPECT(data.tag() == TypeDataTag::Tuple);
  ZC_EXPECT(data.get<TupleTypeData>().elements.size() == 3);
}

ZC_TEST("SemanticTypeStore.InternDynamicArray") {
  TypeStoreFixture fixture;
  auto i32 = fixture.internPrimitive(PrimitiveKind::I32);
  auto id = fixture.intern(TypeData(DynamicArrayTypeData{i32}));
  auto lookup = fixture.store().get(id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_EXPECT(data.tag() == TypeDataTag::DynamicArray);
  ZC_EXPECT(data.get<DynamicArrayTypeData>().element == i32);
}

ZC_TEST("SemanticTypeStore.InternSlice") {
  TypeStoreFixture fixture;
  auto i32 = fixture.internPrimitive(PrimitiveKind::I32);
  auto id = fixture.intern(TypeData(SliceTypeData{i32}));
  auto lookup = fixture.store().get(id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_EXPECT(data.tag() == TypeDataTag::Slice);
  ZC_EXPECT(data.get<SliceTypeData>().element == i32);
}

ZC_TEST("SemanticTypeStore.InternFixedArray") {
  TypeStoreFixture fixture;
  auto i32 = fixture.internPrimitive(PrimitiveKind::I32);
  auto id = fixture.intern(TypeData(FixedArrayTypeData{i32, 10}));
  auto lookup = fixture.store().get(id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_EXPECT(data.tag() == TypeDataTag::FixedArray);
  ZC_EXPECT(data.get<FixedArrayTypeData>().element == i32);
  ZC_EXPECT(data.get<FixedArrayTypeData>().length == 10);
}

ZC_TEST("SemanticTypeStore.FixedArraysWithDifferentLengthsAreDistinct") {
  TypeStoreFixture fixture;
  auto i32 = fixture.internPrimitive(PrimitiveKind::I32);
  auto id1 = fixture.intern(TypeData(FixedArrayTypeData{i32, 10}));
  auto id2 = fixture.intern(TypeData(FixedArrayTypeData{i32, 20}));
  ZC_EXPECT(id1 != id2);
  ZC_EXPECT(fixture.store().size() == 3);
}

ZC_TEST("SemanticTypeStore.InternFunctionNoRaises") {
  TypeStoreFixture fixture;
  auto i32 = fixture.internPrimitive(PrimitiveKind::I32);
  auto boolType = fixture.internPrimitive(PrimitiveKind::Bool);
  auto i64 = fixture.internPrimitive(PrimitiveKind::I64);
  zc::Vector<identity::SemanticTypeId> params;
  params.add(i32);
  params.add(boolType);
  zc::Maybe<identity::SemanticTypeId> noRaises;
  auto id = fixture.intern(TypeData(FunctionTypeData{zc::mv(params), i64, zc::mv(noRaises)}));
  auto lookup = fixture.store().get(id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_EXPECT(data.tag() == TypeDataTag::Function);
  ZC_EXPECT(data.get<FunctionTypeData>().parameters.size() == 2);
  ZC_EXPECT(data.get<FunctionTypeData>().success == i64);
  ZC_EXPECT(data.get<FunctionTypeData>().raises == zc::none);
}

ZC_TEST("SemanticTypeStore.InternFunctionWithRaises") {
  TypeStoreFixture fixture;
  auto i32 = fixture.internPrimitive(PrimitiveKind::I32);
  auto i64 = fixture.internPrimitive(PrimitiveKind::I64);
  auto str = fixture.internPrimitive(PrimitiveKind::Str);
  zc::Vector<identity::SemanticTypeId> params;
  params.add(i32);
  zc::Maybe<identity::SemanticTypeId> raises = str;
  auto id = fixture.intern(TypeData(FunctionTypeData{zc::mv(params), i64, zc::mv(raises)}));
  auto lookup = fixture.store().get(id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_EXPECT(data.tag() == TypeDataTag::Function);
  ZC_EXPECT(data.get<FunctionTypeData>().parameters.size() == 1);
  ZC_EXPECT(data.get<FunctionTypeData>().success == i64);
  ZC_EXPECT(data.get<FunctionTypeData>().raises != zc::none);
  ZC_IF_SOME(raisesType, data.get<FunctionTypeData>().raises) { ZC_EXPECT(raisesType == str); }
}

ZC_TEST("SemanticTypeStore.InternFunctionNoParams") {
  TypeStoreFixture fixture;
  auto i32 = fixture.internPrimitive(PrimitiveKind::I32);
  zc::Vector<identity::SemanticTypeId> params;
  zc::Maybe<identity::SemanticTypeId> noRaises;
  auto id = fixture.intern(TypeData(FunctionTypeData{zc::mv(params), i32, zc::mv(noRaises)}));
  auto lookup = fixture.store().get(id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_EXPECT(data.tag() == TypeDataTag::Function);
  ZC_EXPECT(data.get<FunctionTypeData>().parameters.size() == 0);
  ZC_EXPECT(data.get<FunctionTypeData>().success == i32);
}

ZC_TEST("SemanticTypeStore.InternObject") {
  TypeStoreFixture fixture;
  auto i32 = fixture.internPrimitive(PrimitiveKind::I32);
  auto boolType = fixture.internPrimitive(PrimitiveKind::Bool);
  zc::Vector<ObjectFieldData> fields;
  fields.add(ObjectFieldData{identifier("a"_zc), i32, Mutability::Const, FieldPresence::Required});
  fields.add(
      ObjectFieldData{identifier("b"_zc), boolType, Mutability::Mutable, FieldPresence::Optional});
  auto id = fixture.intern(TypeData(ObjectTypeData{zc::mv(fields)}));
  auto lookup = fixture.store().get(id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_EXPECT(data.tag() == TypeDataTag::Object);
  ZC_EXPECT(data.get<ObjectTypeData>().fields.size() == 2);
  ZC_EXPECT(data.get<ObjectTypeData>().fields[0].type == i32);
  ZC_EXPECT(data.get<ObjectTypeData>().fields[0].mutability == Mutability::Const);
  ZC_EXPECT(data.get<ObjectTypeData>().fields[0].presence == FieldPresence::Required);
  ZC_EXPECT(data.get<ObjectTypeData>().fields[1].type == boolType);
  ZC_EXPECT(data.get<ObjectTypeData>().fields[1].mutability == Mutability::Mutable);
  ZC_EXPECT(data.get<ObjectTypeData>().fields[1].presence == FieldPresence::Optional);
}

ZC_TEST("SemanticTypeStore.InternReferenceConst") {
  TypeStoreFixture fixture;
  auto i32 = fixture.internPrimitive(PrimitiveKind::I32);
  auto id = fixture.intern(TypeData(ReferenceTypeData{Mutability::Const, i32}));
  auto lookup = fixture.store().get(id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_EXPECT(data.tag() == TypeDataTag::Reference);
  ZC_EXPECT(data.get<ReferenceTypeData>().mutability == Mutability::Const);
  ZC_EXPECT(data.get<ReferenceTypeData>().referent == i32);
}

ZC_TEST("SemanticTypeStore.InternRawPointerMutable") {
  TypeStoreFixture fixture;
  auto i32 = fixture.internPrimitive(PrimitiveKind::I32);
  auto id = fixture.intern(TypeData(RawPointerTypeData{Mutability::Mutable, i32}));
  auto lookup = fixture.store().get(id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_EXPECT(data.tag() == TypeDataTag::RawPointer);
  ZC_EXPECT(data.get<RawPointerTypeData>().mutability == Mutability::Mutable);
  ZC_EXPECT(data.get<RawPointerTypeData>().pointee == i32);
}

ZC_TEST("SemanticTypeStore.ConstAndMutableReferencesAreDistinct") {
  TypeStoreFixture fixture;
  auto i32 = fixture.internPrimitive(PrimitiveKind::I32);
  auto constId = fixture.intern(TypeData(ReferenceTypeData{Mutability::Const, i32}));
  auto mutId = fixture.intern(TypeData(ReferenceTypeData{Mutability::Mutable, i32}));
  ZC_EXPECT(constId != mutId);
}

ZC_TEST("SemanticTypeStore.ReferenceAndRawPointerAreDistinct") {
  TypeStoreFixture fixture;
  auto i32 = fixture.internPrimitive(PrimitiveKind::I32);
  auto refId = fixture.intern(TypeData(ReferenceTypeData{Mutability::Const, i32}));
  auto ptrId = fixture.intern(TypeData(RawPointerTypeData{Mutability::Const, i32}));
  ZC_EXPECT(refId != ptrId);
}

// ---------------------------------------------------------------------------
// Nominal, type-parameter, union, intersection, and interface types
// ---------------------------------------------------------------------------

ZC_TEST("SemanticTypeStore.InternNominalClass") {
  TypeStoreFixture fixture;
  zc::Vector<identity::SemanticTypeId> arguments;
  auto id = fixture.intern(TypeData(NominalTypeData{fixture.classDef(), zc::mv(arguments)}));
  auto lookup = fixture.store().get(id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_EXPECT(data.tag() == TypeDataTag::Nominal);
  ZC_EXPECT(data.get<NominalTypeData>().definition == fixture.classDef());
  ZC_EXPECT(data.get<NominalTypeData>().arguments.size() == 0);
}

ZC_TEST("SemanticTypeStore.InternNominalStruct") {
  TypeStoreFixture fixture;
  zc::Vector<identity::SemanticTypeId> arguments;
  auto id = fixture.intern(TypeData(NominalTypeData{fixture.structDef(), zc::mv(arguments)}));
  auto lookup = fixture.store().get(id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_EXPECT(data.tag() == TypeDataTag::Nominal);
  ZC_EXPECT(data.get<NominalTypeData>().definition == fixture.structDef());
}

ZC_TEST("SemanticTypeStore.InternNominalEnum") {
  TypeStoreFixture fixture;
  zc::Vector<identity::SemanticTypeId> arguments;
  auto id = fixture.intern(TypeData(NominalTypeData{fixture.enumDef(), zc::mv(arguments)}));
  auto lookup = fixture.store().get(id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_EXPECT(data.tag() == TypeDataTag::Nominal);
  ZC_EXPECT(data.get<NominalTypeData>().definition == fixture.enumDef());
}

ZC_TEST("SemanticTypeStore.InternNominalWithArguments") {
  TypeStoreFixture fixture;
  auto i32 = fixture.internPrimitive(PrimitiveKind::I32);
  zc::Vector<identity::SemanticTypeId> arguments;
  arguments.add(i32);
  auto id = fixture.intern(TypeData(NominalTypeData{fixture.classDef(), zc::mv(arguments)}));
  auto lookup = fixture.store().get(id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_EXPECT(data.tag() == TypeDataTag::Nominal);
  ZC_EXPECT(data.get<NominalTypeData>().definition == fixture.classDef());
  ZC_EXPECT(data.get<NominalTypeData>().arguments.size() == 1);
  ZC_EXPECT(data.get<NominalTypeData>().arguments[0] == i32);
}

ZC_TEST("SemanticTypeStore.InternTypeParameter") {
  TypeStoreFixture fixture;
  auto id = fixture.intern(TypeData(TypeParameterTypeData{fixture.parameter().clone()}));
  auto lookup = fixture.store().get(id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_EXPECT(data.tag() == TypeDataTag::TypeParameter);
  ZC_EXPECT(data.get<TypeParameterTypeData>().parameter == fixture.parameter());
}

ZC_TEST("SemanticTypeStore.InternUnion") {
  TypeStoreFixture fixture;
  auto i32 = fixture.internPrimitive(PrimitiveKind::I32);
  auto boolType = fixture.internPrimitive(PrimitiveKind::Bool);
  zc::Vector<identity::SemanticTypeId> alternatives;
  alternatives.add(i32);
  alternatives.add(boolType);
  auto id = fixture.intern(TypeData(UnionTypeData{zc::mv(alternatives)}));
  auto lookup = fixture.store().get(id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_EXPECT(data.tag() == TypeDataTag::Union);
  ZC_EXPECT(data.get<UnionTypeData>().alternatives.size() == 2);
}

ZC_TEST("SemanticTypeStore.InternIntersection") {
  TypeStoreFixture fixture;
  auto i32 = fixture.internPrimitive(PrimitiveKind::I32);
  auto i64 = fixture.internPrimitive(PrimitiveKind::I64);
  zc::Vector<identity::SemanticTypeId> conjuncts;
  conjuncts.add(i32);
  conjuncts.add(i64);
  auto id = fixture.intern(TypeData(IntersectionTypeData{zc::mv(conjuncts)}));
  auto lookup = fixture.store().get(id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_EXPECT(data.tag() == TypeDataTag::Intersection);
  ZC_EXPECT(data.get<IntersectionTypeData>().conjuncts.size() == 2);
}

ZC_TEST("SemanticTypeStore.InternInterfaceSelf") {
  TypeStoreFixture fixture;
  auto id = fixture.intern(TypeData(InterfaceSelfTypeData{fixture.interfaceDef()}));
  auto lookup = fixture.store().get(id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_EXPECT(data.tag() == TypeDataTag::InterfaceSelf);
  ZC_EXPECT(data.get<InterfaceSelfTypeData>().interface == fixture.interfaceDef());
}

ZC_TEST("SemanticTypeStore.InternInterfaceBound") {
  TypeStoreFixture fixture;
  zc::Vector<identity::SemanticTypeId> arguments;
  InterfaceInstantiation iface{fixture.interfaceDef(), zc::mv(arguments)};
  auto id = fixture.intern(TypeData(InterfaceBoundTypeData{zc::mv(iface)}));
  auto lookup = fixture.store().get(id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_EXPECT(data.tag() == TypeDataTag::InterfaceBound);
  ZC_EXPECT(data.get<InterfaceBoundTypeData>().interface.interface == fixture.interfaceDef());
}

ZC_TEST("SemanticTypeStore.InternExistential") {
  TypeStoreFixture fixture;
  zc::Vector<identity::SemanticTypeId> arguments;
  ExistentialInterfaceData principal{fixture.interfaceDef(), zc::mv(arguments)};
  zc::Vector<ExistentialInterfaceData> additional;
  zc::Vector<identity::DefId> markers;
  zc::Vector<AssociatedTypeBindingData> bindings;
  auto id = fixture.intern(TypeData(ExistentialTypeData{zc::mv(principal), zc::mv(additional),
                                                        zc::mv(markers), zc::mv(bindings)}));
  auto lookup = fixture.store().get(id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_EXPECT(data.tag() == TypeDataTag::Existential);
  ZC_EXPECT(data.get<ExistentialTypeData>().principal.definition == fixture.interfaceDef());
}

// ---------------------------------------------------------------------------
// Nested composite types and deduplication
// ---------------------------------------------------------------------------

ZC_TEST("SemanticTypeStore.InternNestedComposite") {
  TypeStoreFixture fixture;
  auto i32 = fixture.internPrimitive(PrimitiveKind::I32);
  auto boolType = fixture.internPrimitive(PrimitiveKind::Bool);
  zc::Vector<identity::SemanticTypeId> elements;
  elements.add(i32);
  elements.add(boolType);
  auto tupleId = fixture.intern(TypeData(TupleTypeData{zc::mv(elements)}));
  auto arrayId = fixture.intern(TypeData(DynamicArrayTypeData{tupleId}));
  auto lookup = fixture.store().get(arrayId);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_EXPECT(data.tag() == TypeDataTag::DynamicArray);
  ZC_EXPECT(data.get<DynamicArrayTypeData>().element == tupleId);
}

ZC_TEST("SemanticTypeStore.DeduplicatesSameComposite") {
  TypeStoreFixture fixture;
  auto i32 = fixture.internPrimitive(PrimitiveKind::I32);
  auto boolType = fixture.internPrimitive(PrimitiveKind::Bool);
  zc::Vector<identity::SemanticTypeId> elements1;
  elements1.add(i32);
  elements1.add(boolType);
  auto id1 = fixture.intern(TypeData(TupleTypeData{zc::mv(elements1)}));
  zc::Vector<identity::SemanticTypeId> elements2;
  elements2.add(i32);
  elements2.add(boolType);
  auto id2 = fixture.intern(TypeData(TupleTypeData{zc::mv(elements2)}));
  ZC_EXPECT(id1 == id2);
  ZC_EXPECT(fixture.store().size() == 3);
}

ZC_TEST("SemanticTypeStore.DifferentCompositeOrdersProduceDifferentIds") {
  TypeStoreFixture fixture;
  auto i32 = fixture.internPrimitive(PrimitiveKind::I32);
  auto boolType = fixture.internPrimitive(PrimitiveKind::Bool);
  zc::Vector<identity::SemanticTypeId> elements1;
  elements1.add(i32);
  elements1.add(boolType);
  auto id1 = fixture.intern(TypeData(TupleTypeData{zc::mv(elements1)}));
  zc::Vector<identity::SemanticTypeId> elements2;
  elements2.add(boolType);
  elements2.add(i32);
  auto id2 = fixture.intern(TypeData(TupleTypeData{zc::mv(elements2)}));
  ZC_EXPECT(id1 != id2);
}

ZC_TEST("SemanticTypeStore.SizeTracksUniqueTypes") {
  TypeStoreFixture fixture;
  ZC_EXPECT(fixture.store().size() == 0);
  fixture.internPrimitive(PrimitiveKind::I32);
  ZC_EXPECT(fixture.store().size() == 1);
  fixture.internPrimitive(PrimitiveKind::I32);
  ZC_EXPECT(fixture.store().size() == 1);
  fixture.internPrimitive(PrimitiveKind::I64);
  ZC_EXPECT(fixture.store().size() == 2);
}

// ---------------------------------------------------------------------------
// Key bytes structure and comparison
// ---------------------------------------------------------------------------

ZC_TEST("SemanticTypeStore.PrimitiveKeyHasExpectedStructure") {
  TypeStoreFixture fixture;
  auto id = fixture.internPrimitive(PrimitiveKind::I32);
  auto lookup = fixture.store().get(id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
  const auto& stored = lookup.get<type::SemanticTypeLookup>();
  ZC_EXPECT(stored.key().bytes().size() == 24);
  ZC_EXPECT(zc::encodeHex(stored.key().bytes().slice(0, 22)) ==
            "7a6f6d2e73656d616e7469632d747970652d6b657900"_zc);
  ZC_EXPECT(stored.key().bytes()[22] == 0x01);
  ZC_EXPECT(stored.key().bytes()[23] == 0x03);
}

ZC_TEST("SemanticTypeStore.SameTypeProducesSameKeyBytes") {
  TypeStoreFixture fixture;
  auto id1 = fixture.internPrimitive(PrimitiveKind::I32);
  auto id2 = fixture.internPrimitive(PrimitiveKind::I32);
  auto lookup1 = fixture.store().get(id1);
  auto lookup2 = fixture.store().get(id2);
  ZC_REQUIRE(lookup1.is<type::SemanticTypeLookup>());
  ZC_REQUIRE(lookup2.is<type::SemanticTypeLookup>());
  const auto& stored1 = lookup1.get<type::SemanticTypeLookup>();
  const auto& stored2 = lookup2.get<type::SemanticTypeLookup>();
  ZC_EXPECT(stored1.key().bytes().size() == stored2.key().bytes().size());
  ZC_EXPECT(stored1.key().bytes() == stored2.key().bytes());
}

ZC_TEST("SemanticTypeStore.DifferentTypesProduceDifferentKeyBytes") {
  TypeStoreFixture fixture;
  auto id1 = fixture.internPrimitive(PrimitiveKind::I32);
  auto id2 = fixture.internPrimitive(PrimitiveKind::I64);
  auto lookup1 = fixture.store().get(id1);
  auto lookup2 = fixture.store().get(id2);
  ZC_REQUIRE(lookup1.is<type::SemanticTypeLookup>());
  ZC_REQUIRE(lookup2.is<type::SemanticTypeLookup>());
  const auto& stored1 = lookup1.get<type::SemanticTypeLookup>();
  const auto& stored2 = lookup2.get<type::SemanticTypeLookup>();
  ZC_EXPECT(stored1.key().bytes() != stored2.key().bytes());
}

// ---------------------------------------------------------------------------
// TypeData property accessors
// ---------------------------------------------------------------------------

ZC_TEST("SemanticTypeStore.TypeDataProperties") {
  TypeStoreFixture fixture;
  auto id = fixture.internPrimitive(PrimitiveKind::I32);
  auto lookup = fixture.store().get(id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_EXPECT(data.tag() == TypeDataTag::Primitive);
  ZC_EXPECT(data.primitiveKind() != zc::none);
  ZC_IF_SOME(kind, data.primitiveKind()) { ZC_EXPECT(kind == PrimitiveKind::I32); }
  ZC_EXPECT(data.is<PrimitiveTypeData>());
  ZC_EXPECT(!data.is<TupleTypeData>());
  ZC_EXPECT(data.get<PrimitiveTypeData>().kind == PrimitiveKind::I32);
}

// ---------------------------------------------------------------------------
// Invalid handle and foreign-context rejection
// ---------------------------------------------------------------------------

ZC_TEST("SemanticTypeStore.RejectsInvalidHandleLookup") {
  TypeStoreFixture fixture;
  identity::SemanticTypeId invalidId;
  auto result = fixture.store().get(invalidId);
  ZC_REQUIRE(result.is<identity::IdentityInvariant>());
  const auto& invariant = result.get<identity::IdentityInvariant>();
  ZC_EXPECT(invariant.kind() == identity::IdentityInvariantKind::InvalidHandle);
  ZC_EXPECT(invariant.phase() == identity::IdentityAllocationPhase::SemanticType);
}

ZC_TEST("SemanticTypeStore.RejectsForeignContextLookup") {
  TypeStoreFixture fixtureA;
  TypeStoreFixture fixtureB;
  auto foreignId = fixtureA.internPrimitive(PrimitiveKind::I32);
  auto result = fixtureB.store().get(foreignId);
  ZC_REQUIRE(result.is<identity::IdentityInvariant>());
  const auto& invariant = result.get<identity::IdentityInvariant>();
  ZC_EXPECT(invariant.kind() == identity::IdentityInvariantKind::ForeignContext);
}

ZC_TEST("SemanticTypeStore.RejectsForeignContextCanonical") {
  TypeStoreFixture fixtureA;
  TypeStoreFixture fixtureB;
  auto canonical =
      fixtureA.store().canonicalizeClosed(TypeData(PrimitiveTypeData{PrimitiveKind::I32}));
  ZC_REQUIRE(canonical.is<CanonicalTypeData>());
  auto result = fixtureB.store().intern(zc::mv(canonical.get<CanonicalTypeData>()));
  ZC_REQUIRE(result.is<identity::IdentityInvariant>());
  const auto& invariant = result.get<identity::IdentityInvariant>();
  ZC_EXPECT(invariant.kind() == identity::IdentityInvariantKind::ForeignContext);
}

ZC_TEST("SemanticTypeStore.RejectsForeignTypeReference") {
  TypeStoreFixture fixtureA;
  TypeStoreFixture fixtureB;
  auto foreignId = fixtureA.internPrimitive(PrimitiveKind::I32);
  zc::Vector<identity::SemanticTypeId> elements;
  elements.add(foreignId);
  elements.add(foreignId);
  auto result = fixtureB.store().canonicalizeClosed(TypeData(TupleTypeData{zc::mv(elements)}));
  ZC_REQUIRE(result.is<identity::IdentityInvariant>());
  const auto& invariant = result.get<identity::IdentityInvariant>();
  ZC_EXPECT(invariant.kind() == identity::IdentityInvariantKind::ForeignContext);
  ZC_EXPECT(invariant.phase() == identity::IdentityAllocationPhase::SemanticType);
}

ZC_TEST("SemanticTypeStore.RejectsForeignDefinition") {
  TypeStoreFixture fixtureA;
  TypeStoreFixture fixtureB;
  zc::Vector<identity::SemanticTypeId> arguments;
  auto result = fixtureB.store().canonicalizeClosed(
      TypeData(NominalTypeData{fixtureA.classDef(), zc::mv(arguments)}));
  ZC_REQUIRE(result.is<identity::IdentityInvariant>());
  const auto& invariant = result.get<identity::IdentityInvariant>();
  ZC_EXPECT(invariant.kind() == identity::IdentityInvariantKind::ForeignContext);
  ZC_EXPECT(invariant.phase() == identity::IdentityAllocationPhase::Definition);
}

// ---------------------------------------------------------------------------
// Invalid closed-value rejection
// ---------------------------------------------------------------------------

ZC_TEST("SemanticTypeStore.RejectsTupleWithOneElement") {
  TypeStoreFixture fixture;
  auto i32 = fixture.internPrimitive(PrimitiveKind::I32);
  zc::Vector<identity::SemanticTypeId> elements;
  elements.add(i32);
  auto result = fixture.store().canonicalizeClosed(TypeData(TupleTypeData{zc::mv(elements)}));
  ZC_REQUIRE(result.is<identity::IdentityInvariant>());
  const auto& invariant = result.get<identity::IdentityInvariant>();
  ZC_EXPECT(invariant.kind() == identity::IdentityInvariantKind::InvalidClosedValue);
}

ZC_TEST("SemanticTypeStore.RejectsInvalidPrimitiveKind") {
  TypeStoreFixture fixture;
  auto result = fixture.store().canonicalizeClosed(
      TypeData(PrimitiveTypeData{static_cast<PrimitiveKind>(0x00)}));
  ZC_REQUIRE(result.is<identity::IdentityInvariant>());
  const auto& invariant = result.get<identity::IdentityInvariant>();
  ZC_EXPECT(invariant.kind() == identity::IdentityInvariantKind::InvalidClosedValue);
}

ZC_TEST("SemanticTypeStore.RejectsUnionWithOneAlternative") {
  TypeStoreFixture fixture;
  auto i32 = fixture.internPrimitive(PrimitiveKind::I32);
  zc::Vector<identity::SemanticTypeId> alternatives;
  alternatives.add(i32);
  auto result = fixture.store().canonicalizeClosed(TypeData(UnionTypeData{zc::mv(alternatives)}));
  ZC_REQUIRE(result.is<identity::IdentityInvariant>());
  const auto& invariant = result.get<identity::IdentityInvariant>();
  ZC_EXPECT(invariant.kind() == identity::IdentityInvariantKind::InvalidClosedValue);
}

ZC_TEST("SemanticTypeStore.RejectsUnionWithNever") {
  TypeStoreFixture fixture;
  auto i32 = fixture.internPrimitive(PrimitiveKind::I32);
  auto never = fixture.internPrimitive(PrimitiveKind::Never);
  zc::Vector<identity::SemanticTypeId> alternatives;
  alternatives.add(i32);
  alternatives.add(never);
  auto result = fixture.store().canonicalizeClosed(TypeData(UnionTypeData{zc::mv(alternatives)}));
  ZC_REQUIRE(result.is<identity::IdentityInvariant>());
  const auto& invariant = result.get<identity::IdentityInvariant>();
  ZC_EXPECT(invariant.kind() == identity::IdentityInvariantKind::InvalidClosedValue);
}

ZC_TEST("SemanticTypeStore.RejectsUnionWithAny") {
  TypeStoreFixture fixture;
  auto i32 = fixture.internPrimitive(PrimitiveKind::I32);
  auto any = fixture.internPrimitive(PrimitiveKind::Any);
  zc::Vector<identity::SemanticTypeId> alternatives;
  alternatives.add(i32);
  alternatives.add(any);
  auto result = fixture.store().canonicalizeClosed(TypeData(UnionTypeData{zc::mv(alternatives)}));
  ZC_REQUIRE(result.is<identity::IdentityInvariant>());
  const auto& invariant = result.get<identity::IdentityInvariant>();
  ZC_EXPECT(invariant.kind() == identity::IdentityInvariantKind::InvalidClosedValue);
}

ZC_TEST("SemanticTypeStore.RejectsUnsortedUnion") {
  TypeStoreFixture fixture;
  auto boolType = fixture.internPrimitive(PrimitiveKind::Bool);
  auto i32 = fixture.internPrimitive(PrimitiveKind::I32);
  zc::Vector<identity::SemanticTypeId> alternatives;
  alternatives.add(boolType);
  alternatives.add(i32);
  auto result = fixture.store().canonicalizeClosed(TypeData(UnionTypeData{zc::mv(alternatives)}));
  ZC_REQUIRE(result.is<identity::IdentityInvariant>());
  const auto& invariant = result.get<identity::IdentityInvariant>();
  ZC_EXPECT(invariant.kind() == identity::IdentityInvariantKind::NonCanonicalEncoding);
}

ZC_TEST("SemanticTypeStore.RejectsUnsortedObjectFields") {
  TypeStoreFixture fixture;
  auto i32 = fixture.internPrimitive(PrimitiveKind::I32);
  auto boolType = fixture.internPrimitive(PrimitiveKind::Bool);
  zc::Vector<ObjectFieldData> fields;
  fields.add(ObjectFieldData{identifier("b"_zc), i32, Mutability::Const, FieldPresence::Required});
  fields.add(
      ObjectFieldData{identifier("a"_zc), boolType, Mutability::Const, FieldPresence::Required});
  auto result = fixture.store().canonicalizeClosed(TypeData(ObjectTypeData{zc::mv(fields)}));
  ZC_REQUIRE(result.is<identity::IdentityInvariant>());
  const auto& invariant = result.get<identity::IdentityInvariant>();
  ZC_EXPECT(invariant.kind() == identity::IdentityInvariantKind::NonCanonicalEncoding);
}

ZC_TEST("SemanticTypeStore.RejectsNominalWithInterfaceDefinition") {
  TypeStoreFixture fixture;
  zc::Vector<identity::SemanticTypeId> arguments;
  auto result = fixture.store().canonicalizeClosed(
      TypeData(NominalTypeData{fixture.interfaceDef(), zc::mv(arguments)}));
  ZC_REQUIRE(result.is<identity::IdentityInvariant>());
  const auto& invariant = result.get<identity::IdentityInvariant>();
  ZC_EXPECT(invariant.kind() == identity::IdentityInvariantKind::InvalidClosedValue);
}

ZC_TEST("SemanticTypeStore.RejectsInterfaceSelfWithClassDefinition") {
  TypeStoreFixture fixture;
  auto result =
      fixture.store().canonicalizeClosed(TypeData(InterfaceSelfTypeData{fixture.classDef()}));
  ZC_REQUIRE(result.is<identity::IdentityInvariant>());
  const auto& invariant = result.get<identity::IdentityInvariant>();
  ZC_EXPECT(invariant.kind() == identity::IdentityInvariantKind::InvalidClosedValue);
}

}  // namespace zomlang::compiler::type::semantic
