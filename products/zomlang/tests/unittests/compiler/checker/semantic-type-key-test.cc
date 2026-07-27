// Copyright (c) 2026 Zode.Z. All rights reserved
// Licensed under the Apache License, Version 2.0. See the LICENSE file.

#include "zomlang/compiler/type/semantic-type-key.h"

#include "zc/core/encoding.h"
#include "zc/core/memory.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/sha256.h"
#include "zomlang/compiler/type/semantic-type-store.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::type::semantic {
namespace {

int compareBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  const auto count = zc::min(left.size(), right.size());
  for (size_t index = 0; index < count; ++index) {
    if (left[index] < right[index]) return -1;
    if (left[index] > right[index]) return 1;
  }
  return left.size() < right.size() ? -1 : left.size() > right.size() ? 1 : 0;
}

struct DefinitionEntry final {
  DefinitionEntry(identity::DefinitionKind kind, identity::DefinitionKey&& key)
      : kind(kind), key(zc::mv(key)) {}
  DefinitionEntry(DefinitionEntry&&) noexcept = default;
  DefinitionEntry& operator=(DefinitionEntry&&) noexcept = default;
  ZC_DISALLOW_COPY(DefinitionEntry);

  identity::DefinitionKind kind;
  identity::DefinitionKey key;
  identity::DefId id;
};

class StoreFixture final {
public:
  StoreFixture() {
    auto issuedContext = factory.issue();
    ZC_REQUIRE(issuedContext != zc::none);
    ZC_IF_SOME(value, issuedContext) { contextValue = value; }

    auto created = identity::SemanticIdentityRegistrySet::create(factory, contextValue);
    ZC_REQUIRE(created != zc::none);
    ZC_IF_SOME(value, created) {
      registriesValue = zc::heap<identity::SemanticIdentityRegistrySet>(zc::mv(value));
    }
    buildRegistry();

    auto token = factory.issueSemanticTypeStoreConstructionToken(contextValue);
    ZC_REQUIRE(token != zc::none);
    ZC_IF_SOME(value, token) {
      semanticTypesValue = zc::heap<type::SemanticTypeStore>(zc::mv(value), *registriesValue);
    }
  }

  identity::SemanticTypeId intern(TypeData&& data) {
    auto admission = semanticTypes().canonicalizeClosed(zc::mv(data));
    ZC_REQUIRE(admission.is<CanonicalTypeData>());
    auto result = semanticTypes().intern(zc::mv(admission.get<CanonicalTypeData>()));
    ZC_REQUIRE(result.is<type::SemanticTypeInterned>());
    return result.get<type::SemanticTypeInterned>().id;
  }

  identity::DefId definition(identity::DefinitionKind kind, size_t occurrence = 0) const {
    for (const auto& entry : definitions) {
      if (entry.kind != kind) continue;
      if (occurrence == 0) return entry.id;
      --occurrence;
    }
    ZC_FAIL_REQUIRE("semantic type test definition kind is not registered");
  }

  const identity::GenericParameterKey& parameter() const {
    ZC_IF_SOME(value, registries().genericParameters().keyAt(0)) { return value; }
    ZC_FAIL_REQUIRE("semantic type test generic parameter is not registered");
  }

  zc::Vector<identity::DefId> orderedDefinitions(identity::DefinitionKind kind) const {
    zc::Vector<identity::DefId> result;
    for (const auto& entry : definitions) {
      if (entry.kind != kind) continue;
      auto bytes = entry.key.encode();
      size_t insertion = result.size();
      while (insertion > 0) {
        auto previous = registries().definitions().lookup(result[insertion - 1]);
        ZC_REQUIRE(previous != zc::none);
        bool before = false;
        ZC_IF_SOME(value, previous) {
          const auto previousBytes = value.encode();
          before = compareBytes(bytes.asPtr(), previousBytes.asPtr()) < 0;
        }
        if (!before) break;
        --insertion;
      }
      result.add(entry.id);
      for (size_t index = result.size() - 1; index > insertion; --index) {
        const auto displaced = result[index - 1];
        result[index - 1] = result[index];
        result[index] = displaced;
      }
    }
    return result;
  }

  type::SemanticTypeStore& semanticTypes() { return *semanticTypesValue; }
  const type::SemanticTypeStore& semanticTypes() const { return *semanticTypesValue; }

private:
  void addDefinition(identity::DefinitionKind kind, uint32_t ordinal) {
    using namespace tests::test_identity_detail;
    auto nameSpace = identity::definitionNamespaceFor(kind);
    ZC_REQUIRE(nameSpace != zc::none);
    zc::Vector<identity::EnclosingStableOwnerKey> owners;
    zc::Maybe<identity::OverloadHeaderDigest> noOverloadDigest;
    zc::Maybe<identity::DefinitionIdentityRecord> record;
    ZC_IF_SOME(value, nameSpace) {
      record = identity::DefinitionIdentityRecord::from(
          module(), zc::mv(owners), kind, value,
          scalar<identity::DeclaredDefinitionName>(zc::str("definition", ordinal)),
          zc::mv(noOverloadDigest));
    }
    ZC_REQUIRE(record != zc::none);
    ZC_IF_SOME(value, record) {
      auto key = identity::DefinitionKey::compute(value);
      definitions.add(DefinitionEntry(kind, key.clone()));
      zc::Maybe<identity::OverloadHeaderAuthority> noOverloadAuthority;
      ZC_REQUIRE(registries().collectDefinition(zc::mv(value), zc::mv(noOverloadAuthority),
                                                ordinal) == identity::FrozenRegistryFailure::None);
    }
  }

  void buildRegistry() {
    using namespace tests::test_identity_detail;
    ZC_REQUIRE(registries().collectCompilationUnit(userUnit()) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries().freezeCompilationUnits() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries().collectCrate(crate()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries().freezeCrates() == identity::FrozenRegistryFailure::None);
    auto snapshot = identity::ImmutableSourceSnapshot::from(tests::test_identity_detail::source(),
                                                            zc::heapArray<uint8_t>(1, uint8_t{0}));
    ZC_REQUIRE(snapshot != zc::none);
    ZC_IF_SOME(value, snapshot) {
      ZC_REQUIRE(registries().collectSourceFile(zc::mv(value)) ==
                 identity::FrozenRegistryFailure::None);
    }
    ZC_REQUIRE(registries().freezeSourceFiles() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries().collectModule(module()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries().freezeModules() == identity::FrozenRegistryFailure::None);

    addDefinition(identity::DefinitionKind::Class, 0);
    addDefinition(identity::DefinitionKind::Interface, 1);
    addDefinition(identity::DefinitionKind::Interface, 2);
    addDefinition(identity::DefinitionKind::Interface, 3);
    addDefinition(identity::DefinitionKind::AssociatedType, 4);
    addDefinition(identity::DefinitionKind::AssociatedType, 5);
    ZC_REQUIRE(registries().freezeStableIdentities() == identity::FrozenRegistryFailure::None);

    auto parameterRecord = identity::GenericParameterIdentityRecord::type(
        identity::StableGenericParameterOwnerKey::definition(definitions[0].key.clone()), 0);
    ZC_REQUIRE(registries().collectGenericParameter(zc::mv(parameterRecord)) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries().freezeGenericParameters() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries().freezeCallableParameters() == identity::FrozenRegistryFailure::None);
    for (auto& entry : definitions) {
      auto id = registries().definitions().find(entry.key);
      ZC_REQUIRE(id != zc::none);
      ZC_IF_SOME(value, id) { entry.id = value; }
    }
  }

  identity::SemanticIdentityRegistrySet& registries() { return *registriesValue; }
  const identity::SemanticIdentityRegistrySet& registries() const { return *registriesValue; }

  identity::SemanticContextFactory factory;
  identity::SemanticContextBrand contextValue;
  zc::Own<identity::SemanticIdentityRegistrySet> registriesValue;
  zc::Vector<DefinitionEntry> definitions;
  zc::Own<type::SemanticTypeStore> semanticTypesValue;
};

identity::SemanticIdentifier identifier(zc::StringPtr text) {
  auto result = identity::SemanticIdentifier::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid canonical semantic identifier fixture");
}

zc::Vector<identity::SemanticTypeId> typeIds(identity::SemanticTypeId first,
                                             identity::SemanticTypeId second) {
  zc::Vector<identity::SemanticTypeId> result;
  result.add(first);
  result.add(second);
  return result;
}

ExistentialInterfaceData existentialInterface(identity::DefId definition) {
  zc::Vector<identity::SemanticTypeId> arguments;
  return ExistentialInterfaceData{definition, zc::mv(arguments)};
}

const SemanticTypeKey& keyFor(const StoreFixture& fixture, identity::SemanticTypeId id) {
  auto lookup = fixture.semanticTypes().get(id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
  return lookup.get<type::SemanticTypeLookup>().key();
}

void expectExact(TypeData&& data, StoreFixture& fixture, zc::StringPtr expectedHex,
                 zc::StringPtr expectedDigest) {
  const auto id = fixture.intern(zc::mv(data));
  const auto& key = keyFor(fixture, id);
  ZC_EXPECT(zc::encodeHex(key.bytes()) == expectedHex);
  auto digest = identity::sha256(key.bytes());
  ZC_REQUIRE(digest != zc::none);
  ZC_IF_SOME(value, digest) { ZC_EXPECT(zc::encodeHex(value.bytes()) == expectedDigest); }
}

void expectTag(TypeData&& data, StoreFixture& fixture, TypeDataTag tag) {
  const auto id = fixture.intern(zc::mv(data));
  const auto& key = keyFor(fixture, id);
  ZC_REQUIRE(key.bytes().size() > 22);
  ZC_EXPECT(key.bytes()[22] == static_cast<uint8_t>(tag));
}

void expectRejected(TypeData&& data, StoreFixture& fixture, identity::IdentityInvariantKind kind) {
  auto result = fixture.semanticTypes().canonicalizeClosed(zc::mv(data));
  ZC_REQUIRE(result.is<identity::IdentityInvariant>());
  ZC_EXPECT(result.get<identity::IdentityInvariant>().kind() == kind);
}

}  // namespace

ZC_TEST("SemanticTypeKey.MatchesNormativeGoldenVectors") {
  StoreFixture fixture;
  expectExact(TypeData(PrimitiveTypeData{PrimitiveKind::I32}), fixture,
              "7a6f6d2e73656d616e7469632d747970652d6b6579000103"_zc,
              "edbd50f06b02d4d14baeb6b1f07fcf941d14b594724b927f96b3fae528fec5ed"_zc);

  const auto i32 = fixture.intern(TypeData(PrimitiveTypeData{PrimitiveKind::I32}));
  const auto null = fixture.intern(TypeData(PrimitiveTypeData{PrimitiveKind::Null}));
  expectExact(TypeData(UnionTypeData{typeIds(i32, null)}), fixture,
              "7a6f6d2e73656d616e7469632d747970652d6b6579000a000000000000000201030113"_zc,
              "95145d7b4eefcf1afa1074973dc414f8d268b3a79d86cbb7be2b761a3f40c844"_zc);
}

ZC_TEST("SemanticTypeKey.CoversEveryClosedBranch") {
  StoreFixture fixture;
  const auto nominal = fixture.definition(identity::DefinitionKind::Class);
  const auto& parameter = fixture.parameter();
  const auto interface = fixture.definition(identity::DefinitionKind::Interface);
  const auto associated = fixture.definition(identity::DefinitionKind::AssociatedType);
  const auto i32 = fixture.intern(TypeData(PrimitiveTypeData{PrimitiveKind::I32}));
  const auto null = fixture.intern(TypeData(PrimitiveTypeData{PrimitiveKind::Null}));

  expectTag(TypeData(PrimitiveTypeData{PrimitiveKind::Bool}), fixture, TypeDataTag::Primitive);
  expectTag(TypeData(TupleTypeData{typeIds(i32, null)}), fixture, TypeDataTag::Tuple);

  zc::Vector<ObjectFieldData> fields;
  fields.add(
      ObjectFieldData{identifier("value"_zc), i32, Mutability::Mutable, FieldPresence::Optional});
  expectTag(TypeData(ObjectTypeData{zc::mv(fields)}), fixture, TypeDataTag::Object);
  expectTag(TypeData(DynamicArrayTypeData{i32}), fixture, TypeDataTag::DynamicArray);
  expectTag(TypeData(SliceTypeData{i32}), fixture, TypeDataTag::Slice);
  expectTag(TypeData(FixedArrayTypeData{i32, 0x0102030405060708ULL}), fixture,
            TypeDataTag::FixedArray);

  zc::Vector<identity::SemanticTypeId> parameters;
  parameters.add(i32);
  expectTag(TypeData(FunctionTypeData{zc::mv(parameters), null, i32}), fixture,
            TypeDataTag::Function);
  zc::Vector<identity::SemanticTypeId> arguments;
  arguments.add(i32);
  expectTag(TypeData(NominalTypeData{nominal, zc::mv(arguments)}), fixture, TypeDataTag::Nominal);
  expectTag(TypeData(TypeParameterTypeData{parameter.clone()}), fixture,
            TypeDataTag::TypeParameter);
  expectTag(TypeData(UnionTypeData{typeIds(i32, null)}), fixture, TypeDataTag::Union);
  expectTag(TypeData(IntersectionTypeData{typeIds(i32, null)}), fixture, TypeDataTag::Intersection);
  expectTag(TypeData(ReferenceTypeData{Mutability::Const, i32}), fixture, TypeDataTag::Reference);
  expectTag(TypeData(RawPointerTypeData{Mutability::Mutable, i32}), fixture,
            TypeDataTag::RawPointer);

  zc::Vector<ExistentialInterfaceData> additional;
  zc::Vector<identity::DefId> markers;
  zc::Vector<AssociatedTypeBindingData> bindings;
  bindings.add(AssociatedTypeBindingData{associated, i32});
  expectTag(TypeData(ExistentialTypeData{existentialInterface(interface), zc::mv(additional),
                                         zc::mv(markers), zc::mv(bindings)}),
            fixture, TypeDataTag::Existential);

  zc::Vector<identity::SemanticTypeId> interfaceArguments;
  interfaceArguments.add(i32);
  expectTag(TypeData(InterfaceBoundTypeData{
                InterfaceInstantiation{interface, zc::mv(interfaceArguments)}}),
            fixture, TypeDataTag::InterfaceBound);
  expectTag(TypeData(InterfaceSelfTypeData{interface}), fixture, TypeDataTag::InterfaceSelf);
}

ZC_TEST("SemanticTypeKey.RejectsInvalidTagsAndDegenerateShapes") {
  StoreFixture fixture;
  const auto i32 = fixture.intern(TypeData(PrimitiveTypeData{PrimitiveKind::I32}));
  expectRejected(TypeData(PrimitiveTypeData{static_cast<PrimitiveKind>(0)}), fixture,
                 identity::IdentityInvariantKind::InvalidClosedValue);
  expectRejected(TypeData(ReferenceTypeData{static_cast<Mutability>(0), i32}), fixture,
                 identity::IdentityInvariantKind::InvalidClosedValue);

  zc::Vector<ObjectFieldData> invalidFields;
  invalidFields.add(ObjectFieldData{identifier("value"_zc), i32, Mutability::Const,
                                    static_cast<FieldPresence>(0)});
  expectRejected(TypeData(ObjectTypeData{zc::mv(invalidFields)}), fixture,
                 identity::IdentityInvariantKind::InvalidClosedValue);

  zc::Vector<identity::SemanticTypeId> empty;
  expectRejected(TypeData(TupleTypeData{zc::mv(empty)}), fixture,
                 identity::IdentityInvariantKind::InvalidClosedValue);
  zc::Vector<identity::SemanticTypeId> oneTuple;
  oneTuple.add(i32);
  expectRejected(TypeData(TupleTypeData{zc::mv(oneTuple)}), fixture,
                 identity::IdentityInvariantKind::InvalidClosedValue);
  zc::Vector<identity::SemanticTypeId> oneUnion;
  oneUnion.add(i32);
  expectRejected(TypeData(UnionTypeData{zc::mv(oneUnion)}), fixture,
                 identity::IdentityInvariantKind::InvalidClosedValue);
}

ZC_TEST("SemanticTypeKey.RejectsNonCanonicalOrderingAndDuplicates") {
  StoreFixture fixture;
  const auto i32 = fixture.intern(TypeData(PrimitiveTypeData{PrimitiveKind::I32}));
  const auto null = fixture.intern(TypeData(PrimitiveTypeData{PrimitiveKind::Null}));
  expectRejected(TypeData(UnionTypeData{typeIds(null, i32)}), fixture,
                 identity::IdentityInvariantKind::NonCanonicalEncoding);
  expectRejected(TypeData(UnionTypeData{typeIds(i32, i32)}), fixture,
                 identity::IdentityInvariantKind::NonCanonicalEncoding);
  expectRejected(TypeData(IntersectionTypeData{typeIds(null, i32)}), fixture,
                 identity::IdentityInvariantKind::NonCanonicalEncoding);
  expectRejected(TypeData(IntersectionTypeData{typeIds(i32, i32)}), fixture,
                 identity::IdentityInvariantKind::NonCanonicalEncoding);

  zc::Vector<ObjectFieldData> reversedFields;
  reversedFields.add(
      ObjectFieldData{identifier("z"_zc), i32, Mutability::Const, FieldPresence::Required});
  reversedFields.add(
      ObjectFieldData{identifier("a"_zc), i32, Mutability::Const, FieldPresence::Required});
  expectRejected(TypeData(ObjectTypeData{zc::mv(reversedFields)}), fixture,
                 identity::IdentityInvariantKind::NonCanonicalEncoding);

  zc::Vector<ObjectFieldData> duplicateFields;
  duplicateFields.add(
      ObjectFieldData{identifier("a"_zc), i32, Mutability::Const, FieldPresence::Required});
  duplicateFields.add(
      ObjectFieldData{identifier("a"_zc), i32, Mutability::Mutable, FieldPresence::Optional});
  expectRejected(TypeData(ObjectTypeData{zc::mv(duplicateFields)}), fixture,
                 identity::IdentityInvariantKind::NonCanonicalEncoding);
}

ZC_TEST("SemanticTypeKey.RejectsNonCanonicalExistentialInputs") {
  StoreFixture fixture;
  const auto interfaces = fixture.orderedDefinitions(identity::DefinitionKind::Interface);
  const auto associated = fixture.orderedDefinitions(identity::DefinitionKind::AssociatedType);
  const auto i32 = fixture.intern(TypeData(PrimitiveTypeData{PrimitiveKind::I32}));
  ZC_REQUIRE(interfaces.size() == 3);
  ZC_REQUIRE(associated.size() == 2);

  zc::Vector<ExistentialInterfaceData> reversed;
  reversed.add(existentialInterface(interfaces[2]));
  reversed.add(existentialInterface(interfaces[1]));
  zc::Vector<identity::DefId> noMarkers;
  zc::Vector<AssociatedTypeBindingData> noBindings;
  expectRejected(TypeData(ExistentialTypeData{existentialInterface(interfaces[0]), zc::mv(reversed),
                                              zc::mv(noMarkers), zc::mv(noBindings)}),
                 fixture, identity::IdentityInvariantKind::NonCanonicalEncoding);

  zc::Vector<ExistentialInterfaceData> duplicatePrincipal;
  duplicatePrincipal.add(existentialInterface(interfaces[0]));
  zc::Vector<identity::DefId> duplicateMarkers;
  zc::Vector<AssociatedTypeBindingData> duplicateBindings;
  expectRejected(
      TypeData(ExistentialTypeData{existentialInterface(interfaces[0]), zc::mv(duplicatePrincipal),
                                   zc::mv(duplicateMarkers), zc::mv(duplicateBindings)}),
      fixture, identity::IdentityInvariantKind::NonCanonicalEncoding);

  zc::Vector<ExistentialInterfaceData> noAdditional;
  zc::Vector<identity::DefId> markers;
  markers.add(interfaces[1]);
  zc::Vector<AssociatedTypeBindingData> markerBindings;
  expectRejected(
      TypeData(ExistentialTypeData{existentialInterface(interfaces[0]), zc::mv(noAdditional),
                                   zc::mv(markers), zc::mv(markerBindings)}),
      fixture, identity::IdentityInvariantKind::InvalidClosedValue);

  zc::Vector<ExistentialInterfaceData> bindingAdditional;
  zc::Vector<identity::DefId> bindingMarkers;
  zc::Vector<AssociatedTypeBindingData> reversedBindings;
  reversedBindings.add(AssociatedTypeBindingData{associated[1], i32});
  reversedBindings.add(AssociatedTypeBindingData{associated[0], i32});
  expectRejected(
      TypeData(ExistentialTypeData{existentialInterface(interfaces[0]), zc::mv(bindingAdditional),
                                   zc::mv(bindingMarkers), zc::mv(reversedBindings)}),
      fixture, identity::IdentityInvariantKind::NonCanonicalEncoding);
}

ZC_TEST("SemanticTypeKey.RejectsForeignHandles") {
  StoreFixture local;
  StoreFixture foreign;
  const auto foreignType = foreign.intern(TypeData(PrimitiveTypeData{PrimitiveKind::I32}));
  expectRejected(TypeData(ReferenceTypeData{Mutability::Const, foreignType}), local,
                 identity::IdentityInvariantKind::ForeignContext);
  expectRejected(
      TypeData(InterfaceSelfTypeData{foreign.definition(identity::DefinitionKind::Interface)}),
      local, identity::IdentityInvariantKind::ForeignContext);
}

ZC_TEST("SemanticTypeKey.RejectsWrongDefinitionKindsAndUnknownGenericParameter") {
  StoreFixture fixture;
  const auto wrongNominal = fixture.definition(identity::DefinitionKind::AssociatedType);
  const auto wrongInterface = fixture.definition(identity::DefinitionKind::Class);
  const auto interface = fixture.definition(identity::DefinitionKind::Interface);
  const auto i32 = fixture.intern(TypeData(PrimitiveTypeData{PrimitiveKind::I32}));

  zc::Vector<identity::SemanticTypeId> noArguments;
  expectRejected(TypeData(NominalTypeData{wrongNominal, zc::mv(noArguments)}), fixture,
                 identity::IdentityInvariantKind::InvalidClosedValue);
  auto unknownParameter =
      identity::GenericParameterKey::fromBytes(zc::heapArray<uint8_t>(32, uint8_t{0xff}));
  ZC_REQUIRE(unknownParameter != zc::none);
  ZC_IF_SOME(value, unknownParameter) {
    expectRejected(TypeData(TypeParameterTypeData{zc::mv(value)}), fixture,
                   identity::IdentityInvariantKind::InvalidHandle);
  }
  zc::Vector<identity::SemanticTypeId> interfaceArguments;
  expectRejected(TypeData(InterfaceBoundTypeData{
                     InterfaceInstantiation{wrongInterface, zc::mv(interfaceArguments)}}),
                 fixture, identity::IdentityInvariantKind::InvalidClosedValue);
  expectRejected(TypeData(InterfaceSelfTypeData{wrongInterface}), fixture,
                 identity::IdentityInvariantKind::InvalidClosedValue);

  zc::Vector<ExistentialInterfaceData> additional;
  zc::Vector<identity::DefId> markers;
  zc::Vector<AssociatedTypeBindingData> bindings;
  bindings.add(AssociatedTypeBindingData{wrongInterface, i32});
  expectRejected(TypeData(ExistentialTypeData{existentialInterface(interface), zc::mv(additional),
                                              zc::mv(markers), zc::mv(bindings)}),
                 fixture, identity::IdentityInvariantKind::InvalidClosedValue);
}

ZC_TEST("SemanticTypeKey.AcceptsSharedAcyclicSubgraphs") {
  StoreFixture fixture;
  const auto child = fixture.intern(TypeData(PrimitiveTypeData{PrimitiveKind::I32}));
  const auto left = fixture.intern(TypeData(ReferenceTypeData{Mutability::Const, child}));
  const auto right = fixture.intern(TypeData(RawPointerTypeData{Mutability::Const, child}));
  auto admission =
      fixture.semanticTypes().canonicalizeClosed(TypeData(TupleTypeData{typeIds(left, right)}));
  ZC_EXPECT(admission.is<CanonicalTypeData>());
}

}  // namespace zomlang::compiler::type::semantic
