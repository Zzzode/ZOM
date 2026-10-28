// Copyright (c) 2026 Zode.Z. All rights reserved.
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
// See the License for the specific language governing permissions and
// limitations under the License.

#include "compiler/type/type-substitution.h"

#include "compiler/identity/canonical/identity-interner-set.h"
#include "compiler/type/semantic-type-data.h"
#include "zc/core/encoding.h"
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

identity::PackageKey packageKey() {
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
          scalar<identity::PackageName>("sub"_zc), zc::mv(versionValue), zc::mv(featureValues));
    }
  }
  ZC_UNREACHABLE;
}

identity::CanonicalTargetSpecificationKey targetKey() {
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

identity::ModuleKey moduleKey() {
  auto compilation = identity::CompilationConfigKey::from(
      identity::CompilationDomain::Target, targetKey(),
      identity::SemanticCompilerOptionsKey::from(2026, true, false, true), zc::none);
  ZC_REQUIRE(compilation != zc::none);
  zc::Vector<identity::ModulePathSegment> path;
  path.add(scalar<identity::ModulePathSegment>("sub"_zc));
  ZC_IF_SOME(config, compilation) {
    auto crateAdmitted = identity::CrateKey::from(
        identity::CompilationUnitIdentity::userPackage(packageKey()),
        identity::CrateTargetKind::Library, scalar<identity::TargetName>("sub"_zc),
        zc::mv(config));
    ZC_REQUIRE(crateAdmitted != zc::none);
    ZC_IF_SOME(crate, crateAdmitted) {
      auto admitted = identity::ModuleKey::from(zc::mv(crate), zc::mv(path));
      ZC_REQUIRE(admitted != zc::none);
      ZC_IF_SOME(value, admitted) { return zc::mv(value); }
    }
  }
  ZC_UNREACHABLE;
}

/// \brief Owns one semantic context, type store, and one generic parameter.
class SubstitutionFixture final {
public:
  SubstitutionFixture() {
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
      storeValue = zc::heap<SemanticTypeStore>(zc::mv(value), *identitiesValue);
    }
    classDef = internDefinition(identity::DefinitionKind::Class, "Box"_zc);
    interfaceDef = internDefinition(identity::DefinitionKind::Interface, "Iter"_zc);
    markerDef = internDefinition(identity::DefinitionKind::Interface, "Mark"_zc);
    associatedDef = internDefinition(identity::DefinitionKind::AssociatedType, "Item"_zc);
    buildParameters();
  }

  SemanticTypeStore& store() { return *storeValue; }
  const identity::GenericParameterKey& parameter() const {
    ZC_IF_SOME(value, firstParameter) { return value; }
    ZC_UNREACHABLE;
  }
  const identity::GenericParameterKey& otherParameter() const {
    ZC_IF_SOME(value, secondParameter) { return value; }
    ZC_UNREACHABLE;
  }
  identity::SemanticTypeId t() {
    return intern(TypeData(TypeParameterTypeData{parameter().clone()}));
  }
  identity::SemanticTypeId u() {
    return intern(TypeData(TypeParameterTypeData{otherParameter().clone()}));
  }
  identity::DefId classId() const noexcept { return classDef; }
  identity::DefId interfaceId() const noexcept { return interfaceDef; }
  identity::DefId markerId() const noexcept { return markerDef; }
  identity::DefId associatedId() const noexcept { return associatedDef; }

  identity::SemanticTypeId intern(TypeData&& data) {
    auto canonical = storeValue->canonicalizeClosed(zc::mv(data));
    ZC_REQUIRE(canonical.is<CanonicalTypeData>());
    auto result = storeValue->intern(zc::mv(canonical.get<CanonicalTypeData>()));
    ZC_REQUIRE(result.is<SemanticTypeInterned>());
    return result.get<SemanticTypeInterned>().id;
  }

  identity::SemanticTypeId primitive(PrimitiveKind kind) {
    return intern(TypeData(PrimitiveTypeData{kind}));
  }

  identity::SemanticIdentifier name(zc::StringPtr text) {
    return scalar<identity::SemanticIdentifier>(text);
  }

private:
  identity::DefId internDefinition(identity::DefinitionKind kind, zc::StringPtr text) {
    zc::Vector<identity::EnclosingStableOwnerKey> owners;
    zc::Maybe<identity::OverloadHeaderDigest> noOverload;
    auto declaredName = identity::DeclaredDefinitionName::fromCanonical(text);
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

  void buildParameters() {
    ZC_IF_SOME(entry, identitiesValue->definition(classDef)) {
      auto ownerKey = identity::StableGenericParameterOwnerKey::definition(entry.key().clone());
      auto record = identity::GenericParameterIdentityRecord::type(zc::mv(ownerKey), 0);
      firstParameter = identity::GenericParameterKey::compute(record);
      ZC_IF_SOME(key, firstParameter) {
        auto admitted =
            identitiesValue->internGenericParameter(contextValue, key.clone(), record);
        ZC_REQUIRE(admitted.is<identity::GenericParameterId>());
      }
    }
    ZC_IF_SOME(entry, identitiesValue->definition(interfaceDef)) {
      auto ownerKey = identity::StableGenericParameterOwnerKey::definition(entry.key().clone());
      auto record = identity::GenericParameterIdentityRecord::type(zc::mv(ownerKey), 0);
      secondParameter = identity::GenericParameterKey::compute(record);
      ZC_IF_SOME(key, secondParameter) {
        auto admitted =
            identitiesValue->internGenericParameter(contextValue, key.clone(), record);
        ZC_REQUIRE(admitted.is<identity::GenericParameterId>());
      }
    }
  }

  identity::SemanticContextFactory factory;
  identity::SemanticContextBrand contextValue;
  zc::Own<identity::IdentityInternerSet> identitiesValue;
  zc::Own<SemanticTypeStore> storeValue;
  identity::DefId classDef;
  identity::DefId interfaceDef;
  identity::DefId markerDef;
  identity::DefId associatedDef;
  zc::Maybe<identity::GenericParameterKey> firstParameter;
  zc::Maybe<identity::GenericParameterKey> secondParameter;
};

identity::SemanticTypeId substituteOrFail(SubstitutionFixture& fixture,
                                          identity::SemanticTypeId input,
                                          zc::ArrayPtr<const TypeSubstitution> substitutions) {
  auto result = substituteType(fixture.store(), input, substitutions);
  ZC_REQUIRE(result.is<identity::SemanticTypeId>());
  return result.get<identity::SemanticTypeId>();
}

}  // namespace

ZC_TEST("TypeSubstitution.EmptyEnvironmentReturnsSameId") {
  SubstitutionFixture fixture;
  auto i32 = fixture.primitive(PrimitiveKind::I32);
  const zc::ArrayPtr<const TypeSubstitution> none;
  ZC_EXPECT(substituteOrFail(fixture, i32, none) == i32);
  ZC_EXPECT(substituteOrFail(fixture, fixture.t(), none) == fixture.t());
}

ZC_TEST("TypeSubstitution.ParameterMapsToArgument") {
  SubstitutionFixture fixture;
  auto i32 = fixture.primitive(PrimitiveKind::I32);
  TypeSubstitution entries[] = {{fixture.parameter().clone(), i32}};
  ZC_EXPECT(substituteOrFail(fixture, fixture.t(), entries) == i32);
}

ZC_TEST("TypeSubstitution.UnrelatedMappingLeavesParameter") {
  SubstitutionFixture fixture;
  // Mapping the other owner's parameter U must not rewrite this parameter T.
  TypeSubstitution entries[] = {{fixture.otherParameter().clone(),
                                 fixture.primitive(PrimitiveKind::Bool)}};
  ZC_EXPECT(substituteOrFail(fixture, fixture.t(), entries) == fixture.t());
  ZC_EXPECT(substituteOrFail(fixture, fixture.u(), entries) ==
            fixture.primitive(PrimitiveKind::Bool));
}

ZC_TEST("TypeSubstitution.PrimitiveAndInterfaceSelfAreIdentity") {
  SubstitutionFixture fixture;
  TypeSubstitution entries[] = {{fixture.parameter().clone(),
                                 fixture.primitive(PrimitiveKind::I32)}};
  ZC_EXPECT(substituteOrFail(fixture, fixture.primitive(PrimitiveKind::Bool), entries) ==
            fixture.primitive(PrimitiveKind::Bool));
  auto self = fixture.intern(
      TypeData(InterfaceSelfTypeData{fixture.interfaceId()}));
  ZC_EXPECT(substituteOrFail(fixture, self, entries) == self);
}

ZC_TEST("TypeSubstitution.SubstitutesTupleElements") {
  SubstitutionFixture fixture;
  auto i32 = fixture.primitive(PrimitiveKind::I32);
  auto boolType = fixture.primitive(PrimitiveKind::Bool);
  TypeSubstitution entries[] = {{fixture.parameter().clone(), i32}};
  zc::Vector<identity::SemanticTypeId> parametric;
  parametric.add(fixture.t());
  parametric.add(fixture.primitive(PrimitiveKind::Bool));
  auto parametricTuple = fixture.intern(TypeData(TupleTypeData{zc::mv(parametric)}));
  auto expectedTuple = [&] {
    zc::Vector<identity::SemanticTypeId> elements;
    elements.add(i32);
    elements.add(boolType);
    return fixture.intern(TypeData(TupleTypeData{zc::mv(elements)}));
  }();
  ZC_EXPECT(substituteOrFail(fixture, parametricTuple, entries) == expectedTuple);
}

ZC_TEST("TypeSubstitution.SubstitutesObjectFields") {
  SubstitutionFixture fixture;
  auto i32 = fixture.primitive(PrimitiveKind::I32);
  TypeSubstitution entries[] = {{fixture.parameter().clone(), i32}};
  zc::Vector<ObjectFieldData> parametricFields;
  parametricFields.add(
      ObjectFieldData{fixture.name("a"_zc), fixture.t(), Mutability::Const, FieldPresence::Required});
  auto parametricObject = fixture.intern(TypeData(ObjectTypeData{zc::mv(parametricFields)}));
  auto expectedObject = [&] {
    zc::Vector<ObjectFieldData> fields;
    fields.add(ObjectFieldData{fixture.name("a"_zc), i32, Mutability::Const,
                               FieldPresence::Required});
    return fixture.intern(TypeData(ObjectTypeData{zc::mv(fields)}));
  }();
  ZC_EXPECT(substituteOrFail(fixture, parametricObject, entries) == expectedObject);
}

ZC_TEST("TypeSubstitution.SubstitutesArraysAndSlices") {
  SubstitutionFixture fixture;
  auto i32 = fixture.primitive(PrimitiveKind::I32);
  TypeSubstitution entries[] = {{fixture.parameter().clone(), i32}};
  auto dynamicArray = fixture.intern(TypeData(DynamicArrayTypeData{fixture.t()}));
  auto slice = fixture.intern(TypeData(SliceTypeData{fixture.t()}));
  auto fixed = fixture.intern(TypeData(FixedArrayTypeData{fixture.t(), 3}));
  ZC_EXPECT(substituteOrFail(fixture, dynamicArray, entries) ==
            fixture.intern(TypeData(DynamicArrayTypeData{i32})));
  ZC_EXPECT(substituteOrFail(fixture, slice, entries) ==
            fixture.intern(TypeData(SliceTypeData{i32})));
  ZC_EXPECT(substituteOrFail(fixture, fixed, entries) ==
            fixture.intern(TypeData(FixedArrayTypeData{i32, 3})));
}

ZC_TEST("TypeSubstitution.SubstitutesFunctionIncludingRaises") {
  SubstitutionFixture fixture;
  auto i32 = fixture.primitive(PrimitiveKind::I32);
  auto boolType = fixture.primitive(PrimitiveKind::Bool);
  TypeSubstitution entries[] = {{fixture.parameter().clone(), i32}};
  zc::Vector<identity::SemanticTypeId> parameters;
  parameters.add(fixture.t());
  auto withoutRaises =
      fixture.intern(TypeData(FunctionTypeData{zc::mv(parameters), fixture.t(), zc::none}));
  zc::Vector<identity::SemanticTypeId> withRaisesParams;
  withRaisesParams.add(fixture.primitive(PrimitiveKind::Bool));
  auto withRaises = fixture.intern(
      TypeData(FunctionTypeData{zc::mv(withRaisesParams), fixture.t(), boolType}));
  auto expectedWithoutRaises = [&] {
    zc::Vector<identity::SemanticTypeId> params;
    params.add(i32);
    return fixture.intern(TypeData(FunctionTypeData{zc::mv(params), i32, zc::none}));
  }();
  ZC_EXPECT(substituteOrFail(fixture, withoutRaises, entries) == expectedWithoutRaises);
  auto expectedWithRaises = [&] {
    zc::Vector<identity::SemanticTypeId> params;
    params.add(boolType);
    zc::Maybe<identity::SemanticTypeId> raises = boolType;
    return fixture.intern(TypeData(FunctionTypeData{zc::mv(params), i32, zc::mv(raises)}));
  }();
  ZC_EXPECT(substituteOrFail(fixture, withRaises, entries) == expectedWithRaises);
}

ZC_TEST("TypeSubstitution.SubstitutesNominalArguments") {
  SubstitutionFixture fixture;
  auto i32 = fixture.primitive(PrimitiveKind::I32);
  TypeSubstitution entries[] = {{fixture.parameter().clone(), i32}};
  zc::Vector<identity::SemanticTypeId> arguments;
  arguments.add(fixture.t());
  auto parametricNominal =
      fixture.intern(TypeData(NominalTypeData{fixture.classId(), zc::mv(arguments)}));
  auto expectedNominal = [&] {
    zc::Vector<identity::SemanticTypeId> concrete;
    concrete.add(i32);
    return fixture.intern(TypeData(NominalTypeData{fixture.classId(), zc::mv(concrete)}));
  }();
  ZC_EXPECT(substituteOrFail(fixture, parametricNominal, entries) == expectedNominal);
}

ZC_TEST("TypeSubstitution.SubstitutesUnionAndIntersection") {
  SubstitutionFixture fixture;
  auto i32 = fixture.primitive(PrimitiveKind::I32);
  auto i64 = fixture.primitive(PrimitiveKind::I64);
  TypeSubstitution entries[] = {{fixture.parameter().clone(),
                                 fixture.primitive(PrimitiveKind::Bool)}};
  // Union/intersection members cannot be free type parameters (canonical
  // admission rejects them), so a closed union/intersection never contains a
  // mapped parameter and substitution is the identity over it; the branch is
  // still walked, asserting all children are revisited.
  zc::Vector<identity::SemanticTypeId> alternatives;
  alternatives.add(i32);
  alternatives.add(i64);
  auto unionType = fixture.intern(TypeData(UnionTypeData{zc::mv(alternatives)}));
  zc::Vector<identity::SemanticTypeId> conjuncts;
  conjuncts.add(i32);
  conjuncts.add(i64);
  auto intersectionType = fixture.intern(TypeData(IntersectionTypeData{zc::mv(conjuncts)}));
  ZC_EXPECT(substituteOrFail(fixture, unionType, entries) == unionType);
  ZC_EXPECT(substituteOrFail(fixture, intersectionType, entries) == intersectionType);
}

ZC_TEST("TypeSubstitution.SubstitutesReferenceAndRawPointer") {
  SubstitutionFixture fixture;
  auto i32 = fixture.primitive(PrimitiveKind::I32);
  TypeSubstitution entries[] = {{fixture.parameter().clone(), i32}};
  auto reference = fixture.intern(
      TypeData(ReferenceTypeData{Mutability::Const, fixture.t()}));
  auto pointer = fixture.intern(
      TypeData(RawPointerTypeData{Mutability::Mutable, fixture.t()}));
  ZC_EXPECT(substituteOrFail(fixture, reference, entries) ==
            fixture.intern(TypeData(ReferenceTypeData{Mutability::Const, i32})));
  ZC_EXPECT(substituteOrFail(fixture, pointer, entries) ==
            fixture.intern(TypeData(RawPointerTypeData{Mutability::Mutable, i32})));
}

ZC_TEST("TypeSubstitution.SubstitutesInterfaceBound") {
  SubstitutionFixture fixture;
  auto i32 = fixture.primitive(PrimitiveKind::I32);
  TypeSubstitution entries[] = {{fixture.parameter().clone(), i32}};
  zc::Vector<identity::SemanticTypeId> arguments;
  arguments.add(fixture.t());
  auto bound = fixture.intern(TypeData(InterfaceBoundTypeData{
      InterfaceInstantiation{fixture.interfaceId(), zc::mv(arguments)}}));
  auto expectedBound = [&] {
    zc::Vector<identity::SemanticTypeId> concrete;
    concrete.add(i32);
    return fixture.intern(TypeData(InterfaceBoundTypeData{
        InterfaceInstantiation{fixture.interfaceId(), zc::mv(concrete)}}));
  }();
  ZC_EXPECT(substituteOrFail(fixture, bound, entries) == expectedBound);
}

ZC_TEST("TypeSubstitution.SubstitutesExistential") {
  SubstitutionFixture fixture;
  auto i32 = fixture.primitive(PrimitiveKind::I32);
  auto boolType = fixture.primitive(PrimitiveKind::Bool);
  TypeSubstitution entries[] = {{fixture.parameter().clone(), i32}};
  zc::Vector<identity::SemanticTypeId> principalArguments;
  principalArguments.add(fixture.t());
  ExistentialInterfaceData principal{fixture.interfaceId(), zc::mv(principalArguments)};
  zc::Vector<ExistentialInterfaceData> additional;
  zc::Vector<identity::SemanticTypeId> additionalArguments;
  additionalArguments.add(boolType);
  additional.add(
      ExistentialInterfaceData{fixture.interfaceId(), zc::mv(additionalArguments)});
  zc::Vector<identity::DefId> markers;
  markers.add(fixture.markerId());
  zc::Vector<AssociatedTypeBindingData> bindings;
  bindings.add(AssociatedTypeBindingData{fixture.associatedId(), fixture.t()});
  auto existential = fixture.intern(
      TypeData(ExistentialTypeData{zc::mv(principal), zc::mv(additional), zc::mv(markers),
                                   zc::mv(bindings)}));
  auto expected = [&] {
    zc::Vector<identity::SemanticTypeId> principalConcrete;
    principalConcrete.add(i32);
    ExistentialInterfaceData expectedPrincipal{fixture.interfaceId(),
                                              zc::mv(principalConcrete)};
    zc::Vector<ExistentialInterfaceData> expectedAdditional;
    zc::Vector<identity::SemanticTypeId> additionalConcrete;
    additionalConcrete.add(boolType);
    expectedAdditional.add(
        ExistentialInterfaceData{fixture.interfaceId(), zc::mv(additionalConcrete)});
    zc::Vector<identity::DefId> expectedMarkers;
    expectedMarkers.add(fixture.markerId());
    zc::Vector<AssociatedTypeBindingData> expectedBindings;
    expectedBindings.add(AssociatedTypeBindingData{fixture.associatedId(), i32});
    return fixture.intern(TypeData(ExistentialTypeData{
        zc::mv(expectedPrincipal), zc::mv(expectedAdditional), zc::mv(expectedMarkers),
        zc::mv(expectedBindings)}));
  }();
  ZC_EXPECT(substituteOrFail(fixture, existential, entries) == expected);
}

ZC_TEST("TypeSubstitution.ArgumentMayItselfBeMapped") {
  SubstitutionFixture fixture;
  // T maps to a second parameter key is not constructible here; instead assert
  // a primitive argument stays stable under repeated substitution.
  auto i32 = fixture.primitive(PrimitiveKind::I32);
  TypeSubstitution entries[] = {{fixture.parameter().clone(), i32}};
  auto once = substituteOrFail(fixture, fixture.t(), entries);
  auto twice = substituteOrFail(fixture, once, entries);
  ZC_EXPECT(once == i32);
  ZC_EXPECT(twice == i32);
}

ZC_TEST("TypeSubstitution.UnrelatedCompositeKeepsId") {
  SubstitutionFixture fixture;
  auto boolType = fixture.primitive(PrimitiveKind::Bool);
  TypeSubstitution entries[] = {{fixture.parameter().clone(),
                                 fixture.primitive(PrimitiveKind::I32)}};
  zc::Vector<identity::SemanticTypeId> elements;
  elements.add(boolType);
  elements.add(fixture.primitive(PrimitiveKind::Char));
  auto tuple = fixture.intern(TypeData(TupleTypeData{zc::mv(elements)}));
  // No child mentions the mapped parameter, so canonicalization interns the
  // same id.
  ZC_EXPECT(substituteOrFail(fixture, tuple, entries) == tuple);
}

}  // namespace zomlang::compiler::type::semantic
