// Copyright (c) 2026 Zode.Z. All rights reserved
// Licensed under the Apache License, Version 2.0. See the LICENSE file.

#include "zomlang/compiler/type/semantic-type-key.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/sha256.h"
#include "zomlang/compiler/type/primitive-type.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"
#include "zomlang/tests/unittests/compiler/test-semantic-type-context.h"

namespace zomlang::compiler::type::semantic {
namespace {

struct TypeEntry final {
  TypeEntry(identity::SemanticTypeId id, TypeData&& data) : id(id), data(zc::mv(data)) {}
  TypeEntry(TypeEntry&&) noexcept = default;
  TypeEntry& operator=(TypeEntry&&) noexcept = default;
  ZC_DISALLOW_COPY(TypeEntry);

  identity::SemanticTypeId id;
  TypeData data;
};

struct DefinitionEntry final {
  identity::DefId id;
  zc::Array<uint8_t> bytes;
};

class TestResolver final : public SemanticTypeKeyResolver {
public:
  explicit TestResolver(size_t definitionCount = 4) {
    auto ids = tests::makeTestDefinitionIds(definitionCount);
    for (size_t index = 0; index < ids.size(); ++index) {
      definitions.add(DefinitionEntry{
          ids[index], zc::heapArray<uint8_t>(1, static_cast<uint8_t>(0xa1 + index))});
    }
  }

  identity::SemanticTypeId add(TypeData&& data) {
    const auto ordinal = static_cast<uint8_t>(types.size());
    ZC_REQUIRE(ordinal <= static_cast<uint8_t>(type::PrimitiveKind::Any));
    type::PrimitiveType issuer(static_cast<type::PrimitiveKind>(ordinal));
    const auto id = context.semanticTypes().intern(issuer);
    types.add(TypeEntry{id, zc::mv(data)});
    return id;
  }

  void replace(identity::SemanticTypeId id, TypeData&& data) {
    for (auto& entry : types) {
      if (entry.id == id) {
        entry.data = zc::mv(data);
        return;
      }
    }
    ZC_FAIL_REQUIRE("semantic type test identity is not registered");
  }

  identity::DefId definition(size_t index) const { return definitions[index].id; }

  zc::Maybe<const TypeData&> resolve(identity::SemanticTypeId id) const override {
    for (const auto& entry : types) {
      if (entry.id == id) return entry.data;
    }
    return zc::none;
  }

  bool encodeDefinition(identity::CanonicalEncoder& encoder,
                        identity::DefId definition) const override {
    for (const auto& entry : definitions) {
      if (entry.id != definition) continue;
      for (const auto byte : entry.bytes) { encoder.encodeUint8(byte); }
      return true;
    }
    return false;
  }

private:
  tests::TestSemanticTypeContext context;
  zc::Vector<TypeEntry> types;
  zc::Vector<DefinitionEntry> definitions;
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

void expectExact(const TypeData& data, const TestResolver& resolver, zc::StringPtr expectedHex,
                 zc::StringPtr expectedDigest) {
  auto result = encodeSemanticTypeKeyV1(data, resolver);
  ZC_REQUIRE(result != zc::none);
  ZC_IF_SOME(key, result) {
    ZC_EXPECT(zc::encodeHex(key.bytes()) == expectedHex);
    auto digest = identity::sha256(key.bytes());
    ZC_REQUIRE(digest != zc::none);
    ZC_IF_SOME(value, digest) { ZC_EXPECT(zc::encodeHex(value.bytes()) == expectedDigest); }
  }
}

void expectTag(TypeData&& data, const TestResolver& resolver, TypeDataTag tag) {
  auto result = encodeSemanticTypeKeyV1(data, resolver);
  ZC_REQUIRE(result != zc::none);
  ZC_IF_SOME(key, result) {
    ZC_REQUIRE(key.bytes().size() > 25);
    ZC_EXPECT(key.bytes()[25] == static_cast<uint8_t>(tag));
  }
}

void expectRejected(TypeData&& data, const TestResolver& resolver) {
  ZC_EXPECT(encodeSemanticTypeKeyV1(data, resolver) == zc::none);
}

}  // namespace

ZC_TEST("SemanticTypeKeyV1.MatchesNormativeGoldenVectors") {
  TestResolver resolver;
  const auto definition = resolver.definition(0);

  TypeData primitive(PrimitiveTypeData{PrimitiveKind::I32});
  expectExact(primitive, resolver, "7a6f6d2e73656d616e7469632d747970652d6b65792e7631000103"_zc,
              "7d44a0782e137c5c320aa10b8617f6fff79be234324f0c4d6ddfd8f226e1e24e"_zc);

  zc::Vector<identity::SemanticTypeId> noArguments;
  TypeData nominal(NominalTypeData{definition, zc::mv(noArguments)});
  expectExact(nominal, resolver,
              "7a6f6d2e73656d616e7469632d747970652d6b65792e76310008a10000000000000000"_zc,
              "7a915bd1f590e12c3fe8b1223bf18683ed0c276ab3acc8b9624f5db8a8f941dc"_zc);

  TypeData interfaceSelf(InterfaceSelfTypeData{definition});
  expectExact(interfaceSelf, resolver, "7a6f6d2e73656d616e7469632d747970652d6b65792e76310010a1"_zc,
              "5d8a126f0375fdf0d060b31911a5da72643d7b5e4707df835d815a67d5ad1dee"_zc);

  const auto selfId = resolver.add(TypeData(InterfaceSelfTypeData{definition}));
  TypeData reference(ReferenceTypeData{Mutability::Const, selfId});
  expectExact(reference, resolver, "7a6f6d2e73656d616e7469632d747970652d6b65792e7631000c0110a1"_zc,
              "6c63b061e1bbaf122ce155c6b2799bcf0291899496661d42be830666c99309cf"_zc);
}

ZC_TEST("SemanticTypeKeyV1.MatchesRecursiveUnionVector") {
  TestResolver resolver;
  const auto i32 = resolver.add(TypeData(PrimitiveTypeData{PrimitiveKind::I32}));
  const auto null = resolver.add(TypeData(PrimitiveTypeData{PrimitiveKind::Null}));
  TypeData data(UnionTypeData{typeIds(i32, null)});
  expectExact(data, resolver,
              "7a6f6d2e73656d616e7469632d747970652d6b65792e7631000a000000000000000201030113"_zc,
              "1445b160d83d787c1eced0adb65aafe14e4ab218a8538c0e5886c1a2340505b5"_zc);
}

ZC_TEST("SemanticTypeKeyV1.CoversEveryClosedBranch") {
  TestResolver resolver;
  const auto definition = resolver.definition(0);
  const auto i32 = resolver.add(TypeData(PrimitiveTypeData{PrimitiveKind::I32}));
  const auto null = resolver.add(TypeData(PrimitiveTypeData{PrimitiveKind::Null}));

  expectTag(TypeData(PrimitiveTypeData{PrimitiveKind::Bool}), resolver, TypeDataTag::Primitive);
  expectTag(TypeData(TupleTypeData{typeIds(i32, null)}), resolver, TypeDataTag::Tuple);

  zc::Vector<ObjectFieldData> fields;
  fields.add(
      ObjectFieldData{identifier("value"_zc), i32, Mutability::Mutable, FieldPresence::Optional});
  expectTag(TypeData(ObjectTypeData{zc::mv(fields)}), resolver, TypeDataTag::Object);
  expectTag(TypeData(DynamicArrayTypeData{i32}), resolver, TypeDataTag::DynamicArray);
  expectTag(TypeData(SliceTypeData{i32}), resolver, TypeDataTag::Slice);
  expectTag(TypeData(FixedArrayTypeData{i32, 0x0102030405060708ULL}), resolver,
            TypeDataTag::FixedArray);

  zc::Vector<identity::SemanticTypeId> parameters;
  parameters.add(i32);
  expectTag(TypeData(FunctionTypeData{zc::mv(parameters), null, i32}), resolver,
            TypeDataTag::Function);
  zc::Vector<identity::SemanticTypeId> arguments;
  arguments.add(i32);
  expectTag(TypeData(NominalTypeData{definition, zc::mv(arguments)}), resolver,
            TypeDataTag::Nominal);
  expectTag(TypeData(TypeParameterTypeData{definition}), resolver, TypeDataTag::TypeParameter);
  expectTag(TypeData(UnionTypeData{typeIds(i32, null)}), resolver, TypeDataTag::Union);
  expectTag(TypeData(IntersectionTypeData{typeIds(i32, null)}), resolver,
            TypeDataTag::Intersection);
  expectTag(TypeData(ReferenceTypeData{Mutability::Const, i32}), resolver, TypeDataTag::Reference);
  expectTag(TypeData(RawPointerTypeData{Mutability::Mutable, i32}), resolver,
            TypeDataTag::RawPointer);

  zc::Vector<ExistentialInterfaceData> additional;
  zc::Vector<identity::SemanticTypeId> additionalArguments;
  additional.add(ExistentialInterfaceData{resolver.definition(1), zc::mv(additionalArguments)});
  zc::Vector<identity::DefId> markers;
  markers.add(resolver.definition(2));
  zc::Vector<AssociatedTypeBindingData> bindings;
  bindings.add(AssociatedTypeBindingData{resolver.definition(3), i32});
  zc::Vector<identity::SemanticTypeId> principalArguments;
  expectTag(
      TypeData(ExistentialTypeData{ExistentialInterfaceData{definition, zc::mv(principalArguments)},
                                   zc::mv(additional), zc::mv(markers), zc::mv(bindings)}),
      resolver, TypeDataTag::Existential);

  zc::Vector<identity::SemanticTypeId> interfaceArguments;
  interfaceArguments.add(i32);
  expectTag(TypeData(InterfaceBoundTypeData{
                InterfaceInstantiation{definition, zc::mv(interfaceArguments)}}),
            resolver, TypeDataTag::InterfaceBound);
  expectTag(TypeData(InterfaceSelfTypeData{definition}), resolver, TypeDataTag::InterfaceSelf);
}

ZC_TEST("SemanticTypeKeyV1.RejectsInvalidTagsAndDegenerateShapes") {
  TestResolver resolver;
  const auto i32 = resolver.add(TypeData(PrimitiveTypeData{PrimitiveKind::I32}));
  expectRejected(TypeData(PrimitiveTypeData{static_cast<PrimitiveKind>(0)}), resolver);
  expectRejected(TypeData(ReferenceTypeData{static_cast<Mutability>(0), i32}), resolver);

  zc::Vector<ObjectFieldData> invalidFields;
  invalidFields.add(ObjectFieldData{identifier("value"_zc), i32, Mutability::Const,
                                    static_cast<FieldPresence>(0)});
  expectRejected(TypeData(ObjectTypeData{zc::mv(invalidFields)}), resolver);

  zc::Vector<identity::SemanticTypeId> empty;
  expectRejected(TypeData(TupleTypeData{zc::mv(empty)}), resolver);
  zc::Vector<identity::SemanticTypeId> oneTuple;
  oneTuple.add(i32);
  expectRejected(TypeData(TupleTypeData{zc::mv(oneTuple)}), resolver);
  zc::Vector<identity::SemanticTypeId> oneUnion;
  oneUnion.add(i32);
  expectRejected(TypeData(UnionTypeData{zc::mv(oneUnion)}), resolver);
  zc::Vector<identity::SemanticTypeId> oneIntersection;
  oneIntersection.add(i32);
  expectRejected(TypeData(IntersectionTypeData{zc::mv(oneIntersection)}), resolver);
}

ZC_TEST("SemanticTypeKeyV1.RejectsNonCanonicalOrderingAndDuplicates") {
  TestResolver resolver;
  const auto i32 = resolver.add(TypeData(PrimitiveTypeData{PrimitiveKind::I32}));
  const auto null = resolver.add(TypeData(PrimitiveTypeData{PrimitiveKind::Null}));
  expectRejected(TypeData(UnionTypeData{typeIds(null, i32)}), resolver);
  expectRejected(TypeData(UnionTypeData{typeIds(i32, i32)}), resolver);
  expectRejected(TypeData(IntersectionTypeData{typeIds(null, i32)}), resolver);
  expectRejected(TypeData(IntersectionTypeData{typeIds(i32, i32)}), resolver);

  zc::Vector<ObjectFieldData> reversedFields;
  reversedFields.add(
      ObjectFieldData{identifier("z"_zc), i32, Mutability::Const, FieldPresence::Required});
  reversedFields.add(
      ObjectFieldData{identifier("a"_zc), i32, Mutability::Const, FieldPresence::Required});
  expectRejected(TypeData(ObjectTypeData{zc::mv(reversedFields)}), resolver);

  zc::Vector<ObjectFieldData> duplicateFields;
  duplicateFields.add(
      ObjectFieldData{identifier("a"_zc), i32, Mutability::Const, FieldPresence::Required});
  duplicateFields.add(
      ObjectFieldData{identifier("a"_zc), i32, Mutability::Mutable, FieldPresence::Optional});
  expectRejected(TypeData(ObjectTypeData{zc::mv(duplicateFields)}), resolver);
}

ZC_TEST("SemanticTypeKeyV1.RejectsNonCanonicalExistentialOrdering") {
  TestResolver resolver;
  const auto i32 = resolver.add(TypeData(PrimitiveTypeData{PrimitiveKind::I32}));

  {
    zc::Vector<ExistentialInterfaceData> additional;
    additional.add(existentialInterface(resolver.definition(2)));
    additional.add(existentialInterface(resolver.definition(1)));
    zc::Vector<identity::DefId> markers;
    zc::Vector<AssociatedTypeBindingData> bindings;
    expectRejected(
        TypeData(ExistentialTypeData{existentialInterface(resolver.definition(0)),
                                     zc::mv(additional), zc::mv(markers), zc::mv(bindings)}),
        resolver);
  }

  {
    zc::Vector<ExistentialInterfaceData> additional;
    additional.add(existentialInterface(resolver.definition(0)));
    zc::Vector<identity::DefId> markers;
    zc::Vector<AssociatedTypeBindingData> bindings;
    expectRejected(
        TypeData(ExistentialTypeData{existentialInterface(resolver.definition(0)),
                                     zc::mv(additional), zc::mv(markers), zc::mv(bindings)}),
        resolver);
  }

  {
    zc::Vector<ExistentialInterfaceData> additional;
    zc::Vector<identity::DefId> markers;
    markers.add(resolver.definition(2));
    markers.add(resolver.definition(1));
    zc::Vector<AssociatedTypeBindingData> bindings;
    expectRejected(
        TypeData(ExistentialTypeData{existentialInterface(resolver.definition(0)),
                                     zc::mv(additional), zc::mv(markers), zc::mv(bindings)}),
        resolver);
  }

  {
    zc::Vector<ExistentialInterfaceData> additional;
    zc::Vector<identity::DefId> markers;
    zc::Vector<AssociatedTypeBindingData> bindings;
    bindings.add(AssociatedTypeBindingData{resolver.definition(2), i32});
    bindings.add(AssociatedTypeBindingData{resolver.definition(1), i32});
    expectRejected(
        TypeData(ExistentialTypeData{existentialInterface(resolver.definition(0)),
                                     zc::mv(additional), zc::mv(markers), zc::mv(bindings)}),
        resolver);
  }
}

ZC_TEST("SemanticTypeKeyV1.RejectsMissingExpansionsAndStructuralCycles") {
  TestResolver resolver;
  identity::SemanticTypeId invalidType;
  identity::DefId invalidDefinition;
  expectRejected(TypeData(ReferenceTypeData{Mutability::Const, invalidType}), resolver);
  expectRejected(TypeData(InterfaceSelfTypeData{invalidDefinition}), resolver);

  tests::TestSemanticTypeContext foreignContext;
  type::PrimitiveType foreignPrimitive(type::PrimitiveKind::I32);
  const auto foreignType = foreignContext.semanticTypes().intern(foreignPrimitive);
  expectRejected(TypeData(ReferenceTypeData{Mutability::Const, foreignType}), resolver);
  const auto foreignDefinition = tests::testDefinition(0);
  expectRejected(TypeData(InterfaceSelfTypeData{foreignDefinition}), resolver);

  TestResolver direct;
  const auto directId = direct.add(TypeData(PrimitiveTypeData{PrimitiveKind::I32}));
  direct.replace(directId, TypeData(ReferenceTypeData{Mutability::Const, directId}));
  expectRejected(TypeData(ReferenceTypeData{Mutability::Const, directId}), direct);

  TestResolver pair;
  const auto first = pair.add(TypeData(PrimitiveTypeData{PrimitiveKind::I32}));
  const auto second = pair.add(TypeData(PrimitiveTypeData{PrimitiveKind::Null}));
  pair.replace(first, TypeData(ReferenceTypeData{Mutability::Const, second}));
  pair.replace(second, TypeData(ReferenceTypeData{Mutability::Const, first}));
  expectRejected(TypeData(ReferenceTypeData{Mutability::Const, first}), pair);
}

ZC_TEST("SemanticTypeKeyV1.AcceptsSharedAcyclicSubgraphs") {
  TestResolver resolver;
  const auto child = resolver.add(TypeData(PrimitiveTypeData{PrimitiveKind::I32}));
  const auto left = resolver.add(TypeData(ReferenceTypeData{Mutability::Const, child}));
  const auto right = resolver.add(TypeData(RawPointerTypeData{Mutability::Const, child}));
  ZC_EXPECT(encodeSemanticTypeKeyV1(TypeData(TupleTypeData{typeIds(left, right)}), resolver) !=
            zc::none);
}

}  // namespace zomlang::compiler::type::semantic
