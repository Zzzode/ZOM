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

#include "compiler/identity/canonical/header-type.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "compiler/identity/canonical/canonical-decoder.h"

namespace zomlang::compiler::identity {
namespace {

SemanticIdentifier identifier(zc::StringPtr text) {
  auto value = SemanticIdentifier::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid canonical header type test identifier");
}

CanonicalNameReference genericName() {
  zc::Vector<SemanticIdentifier> suffix;
  auto value = CanonicalNameReference::from(CanonicalNameRoot::generic(0, 0), zc::mv(suffix));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid generic name was rejected");
}

CanonicalNameReference relativeName(zc::StringPtr text) {
  zc::Vector<SemanticIdentifier> suffix;
  suffix.add(identifier(text));
  auto value = CanonicalNameReference::from(CanonicalNameRoot::relative(), zc::mv(suffix));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid relative name was rejected");
}

CanonicalNamedHeaderType namedHeader(zc::StringPtr text = nullptr) {
  zc::Vector<CanonicalHeaderTypeSyntax> arguments;
  return CanonicalNamedHeaderType::from(text == nullptr ? genericName() : relativeName(text),
                                        zc::mv(arguments));
}

CanonicalHeaderTypeSyntax predefined(PredefinedTypeKind kind) {
  auto value = CanonicalHeaderTypeSyntax::predefined(kind);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid predefined type was rejected");
}

CanonicalHeaderTypeSyntax unionType(zc::Vector<CanonicalHeaderTypeSyntax>&& members) {
  auto value = CanonicalHeaderTypeSyntax::unionOf(zc::mv(members));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid union type was rejected");
}

CanonicalHeaderTypeSyntax intersectionType(zc::Vector<CanonicalHeaderTypeSyntax>&& members) {
  auto value = CanonicalHeaderTypeSyntax::intersectionOf(zc::mv(members));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid intersection type was rejected");
}

CanonicalHeaderTypeSyntax functionType(zc::Vector<CanonicalHeaderTypeSyntax>&& parameters,
                                       CanonicalHeaderTypeSyntax&& result) {
  zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>> raises;
  auto value =
      CanonicalHeaderTypeSyntax::function(zc::mv(parameters), zc::mv(result), zc::mv(raises));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid function type was rejected");
}

CanonicalObjectTypeMember objectMember(zc::StringPtr name, PredefinedTypeKind kind,
                                       bool isMutable = false, bool isOptional = false) {
  return CanonicalObjectTypeMember::from(identifier(name), predefined(kind), isMutable, isOptional);
}

CanonicalAssociatedBinding binding(zc::StringPtr name, PredefinedTypeKind kind) {
  return CanonicalAssociatedBinding::from(identifier(name), predefined(kind));
}

zc::Array<uint8_t> decodedBytes(zc::StringPtr hex) {
  auto bytes = zc::decodeHex(hex);
  ZC_REQUIRE(bytes != zc::none);
  return zc::mv(ZC_REQUIRE_NONNULL(bytes));
}

void expectRoundTrip(const CanonicalHeaderTypeSyntax& value) {
  auto bytes = value.encode();
  CanonicalDecoder decoder(bytes.asPtr());
  auto decoded = CanonicalHeaderTypeSyntax::decodeCanonical(decoder);
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(decoder.finished());
  ZC_IF_SOME(admitted, decoded) { ZC_EXPECT(admitted.encode().asPtr() == bytes.asPtr()); }
  for (size_t size = 0; size < bytes.size(); ++size) {
    CanonicalDecoder truncated(bytes.asPtr().slice(0, size));
    ZC_EXPECT(CanonicalHeaderTypeSyntax::decodeCanonical(truncated) == zc::none);
  }
}

void expectHex(const CanonicalHeaderTypeSyntax& value, zc::StringPtr expected) {
  auto bytes = decodedBytes(expected);
  ZC_EXPECT(value.encode().asPtr() == bytes.asPtr());
  expectRoundTrip(value);
}

zc::Vector<uint8_t> nestedDynamicArrayBytes(uint32_t arrayCount) {
  zc::Vector<uint8_t> bytes(static_cast<size_t>(arrayCount) + 2);
  for (uint32_t index = 0; index < arrayCount; ++index) {
    bytes.add(static_cast<uint8_t>(CanonicalHeaderTypeSyntaxKind::DynamicArray));
  }
  bytes.add(static_cast<uint8_t>(CanonicalHeaderTypeSyntaxKind::Predefined));
  bytes.add(static_cast<uint8_t>(PredefinedTypeKind::I8));
  return bytes;
}

bool hasPredefinedKind(const CanonicalHeaderTypeSyntax& value, PredefinedTypeKind expected) {
  return value.predefinedKind() == expected;
}

}  // namespace

ZC_TEST("CanonicalHeaderTypeSyntax passes fixed vectors for all sixteen tags and fields") {
  auto named = CanonicalHeaderTypeSyntax::named(namedHeader());
  expectHex(named, "0103000000000000000000000000000000000000000000000000"_zc);
  expectHex(predefined(PredefinedTypeKind::I8), "0201"_zc);

  zc::Vector<CanonicalHeaderTypeSyntax> parameters;
  parameters.add(predefined(PredefinedTypeKind::I8));
  parameters.add(predefined(PredefinedTypeKind::I16));
  auto function = functionType(zc::mv(parameters), predefined(PredefinedTypeKind::I8));
  expectHex(function, "03000000000000000202010202020100"_zc);

  zc::Vector<CanonicalHeaderTypeSyntax> unionMembers;
  unionMembers.add(predefined(PredefinedTypeKind::I16));
  unionMembers.add(predefined(PredefinedTypeKind::I8));
  expectHex(unionType(zc::mv(unionMembers)), "04000000000000000202010202"_zc);
  zc::Vector<CanonicalHeaderTypeSyntax> intersectionMembers;
  intersectionMembers.add(predefined(PredefinedTypeKind::I16));
  intersectionMembers.add(predefined(PredefinedTypeKind::I8));
  expectHex(intersectionType(zc::mv(intersectionMembers)), "05000000000000000202010202"_zc);

  auto fixed = CanonicalHeaderTypeSyntax::fixedArray(predefined(PredefinedTypeKind::I8), 3);
  expectHex(fixed, "0602010000000000000003"_zc);
  expectHex(CanonicalHeaderTypeSyntax::dynamicArray(predefined(PredefinedTypeKind::I8)),
            "070201"_zc);
  expectHex(CanonicalHeaderTypeSyntax::slice(predefined(PredefinedTypeKind::I8)), "080201"_zc);
  auto optional = CanonicalHeaderTypeSyntax::optional(predefined(PredefinedTypeKind::I8), 2);
  ZC_REQUIRE(optional != zc::none);
  ZC_IF_SOME(value, optional) { expectHex(value, "09020102"_zc); }
  auto reference = CanonicalHeaderTypeSyntax::reference(ReferenceMutability::Shared,
                                                        predefined(PredefinedTypeKind::I8));
  ZC_REQUIRE(reference != zc::none);
  ZC_IF_SOME(value, reference) { expectHex(value, "0a010201"_zc); }
  auto pointer = CanonicalHeaderTypeSyntax::rawPointer(RawPointerMutability::Mutable,
                                                       predefined(PredefinedTypeKind::I8));
  ZC_REQUIRE(pointer != zc::none);
  ZC_IF_SOME(value, pointer) { expectHex(value, "0b020201"_zc); }
  expectHex(CanonicalHeaderTypeSyntax::typeQuery(genericName()),
            "0c0300000000000000000000000000000000"_zc);

  zc::Vector<CanonicalObjectTypeMember> objectMembers;
  objectMembers.add(objectMember("x"_zc, PredefinedTypeKind::I8, true, false));
  auto object = CanonicalHeaderTypeSyntax::object(zc::mv(objectMembers));
  expectHex(object, "0d000000000000000100000000000000017802010100"_zc);
  zc::Vector<CanonicalHeaderTypeSyntax> tupleElements;
  tupleElements.add(predefined(PredefinedTypeKind::I16));
  tupleElements.add(predefined(PredefinedTypeKind::I8));
  auto tuple = CanonicalHeaderTypeSyntax::tuple(zc::mv(tupleElements));
  expectHex(tuple, "0e000000000000000202020201"_zc);

  zc::Maybe<CanonicalHeaderTypeSyntax> interfaceType = predefined(PredefinedTypeKind::I16);
  auto projection = CanonicalHeaderTypeSyntax::associatedProjection(
      predefined(PredefinedTypeKind::I8), zc::mv(interfaceType), identifier("x"_zc));
  expectHex(projection, "0f0201010202000000000000000178"_zc);
  zc::Vector<CanonicalNameReference> markers;
  zc::Vector<CanonicalAssociatedBinding> bindings;
  auto dynamic =
      CanonicalHeaderTypeSyntax::dynamic(namedHeader(), zc::mv(markers), zc::mv(bindings));
  expectHex(
      dynamic,
      "100300000000000000000000000000000000000000000000000000000000000000000000000000000000"_zc);
}

ZC_TEST("CanonicalHeaderTypeSyntax rejects invalid enums depth empty sets and empty raises") {
  ZC_EXPECT(!isCanonicalHeaderTypeSyntaxKind(static_cast<CanonicalHeaderTypeSyntaxKind>(0xff)));
  ZC_EXPECT(CanonicalHeaderTypeSyntax::predefined(static_cast<PredefinedTypeKind>(0xff)) ==
            zc::none);
  ZC_EXPECT(CanonicalHeaderTypeSyntax::reference(static_cast<ReferenceMutability>(0xff),
                                                 predefined(PredefinedTypeKind::I8)) == zc::none);
  ZC_EXPECT(CanonicalHeaderTypeSyntax::rawPointer(static_cast<RawPointerMutability>(0xff),
                                                  predefined(PredefinedTypeKind::I8)) == zc::none);
  for (uint8_t depth : {uint8_t{0}, uint8_t{3}, uint8_t{0xff}}) {
    ZC_EXPECT(CanonicalHeaderTypeSyntax::optional(predefined(PredefinedTypeKind::I8), depth) ==
              zc::none);
  }
  zc::Vector<CanonicalHeaderTypeSyntax> emptyUnion;
  ZC_EXPECT(CanonicalHeaderTypeSyntax::unionOf(zc::mv(emptyUnion)) == zc::none);
  zc::Vector<CanonicalHeaderTypeSyntax> emptyIntersection;
  ZC_EXPECT(CanonicalHeaderTypeSyntax::intersectionOf(zc::mv(emptyIntersection)) == zc::none);
  zc::Vector<CanonicalHeaderTypeSyntax> parameters;
  zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>> emptyRaises =
      zc::Vector<CanonicalHeaderTypeSyntax>();
  ZC_EXPECT(CanonicalHeaderTypeSyntax::function(zc::mv(parameters),
                                                predefined(PredefinedTypeKind::Unit),
                                                zc::mv(emptyRaises)) == zc::none);
}

ZC_TEST("CanonicalHeaderTypeSyntax decoder rejects hostile tags counts booleans and depth") {
  const auto expectRejected = [](zc::StringPtr hex) {
    auto bytes = decodedBytes(hex);
    CanonicalDecoder decoder(bytes.asPtr());
    ZC_EXPECT(CanonicalHeaderTypeSyntax::decodeCanonical(decoder) == zc::none);
  };
  expectRejected("ff"_zc);
  expectRejected("02ff"_zc);
  expectRejected("090201ff"_zc);
  expectRejected("0aff0201"_zc);
  expectRejected("0bff0201"_zc);
  expectRejected("0300000000000000000201ff"_zc);
  expectRejected("0d00000000000000010000000000000001780201ff00"_zc);
  expectRejected("0f0201ff"_zc);
  expectRejected("0e0000000000010000"_zc);

  auto maximumDepth = nestedDynamicArrayBytes(99);
  CanonicalDecoder maximumDepthDecoder(maximumDepth.asPtr());
  auto maximumDepthType = CanonicalHeaderTypeSyntax::decodeCanonical(maximumDepthDecoder);
  ZC_EXPECT(maximumDepthType != zc::none);
  ZC_EXPECT(maximumDepthDecoder.finished());

  auto excessiveDepth = nestedDynamicArrayBytes(100);
  CanonicalDecoder excessiveDepthDecoder(excessiveDepth.asPtr());
  ZC_EXPECT(CanonicalHeaderTypeSyntax::decodeCanonical(excessiveDepthDecoder) == zc::none);
}

ZC_TEST("CanonicalHeaderTypeSyntax normalizes unions intersections objects and dynamic sets") {
  zc::Vector<CanonicalHeaderTypeSyntax> functionParameters;
  zc::Vector<CanonicalHeaderTypeSyntax> raisesValues;
  raisesValues.add(predefined(PredefinedTypeKind::I16));
  raisesValues.add(predefined(PredefinedTypeKind::I8));
  raisesValues.add(predefined(PredefinedTypeKind::I8));
  zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>> raises = zc::mv(raisesValues);
  auto function = CanonicalHeaderTypeSyntax::function(
      zc::mv(functionParameters), predefined(PredefinedTypeKind::Unit), zc::mv(raises));
  ZC_REQUIRE(function != zc::none);
  ZC_IF_SOME(value, function) {
    expectHex(value, "030000000000000000020f01000000000000000202010202"_zc);
    ZC_IF_SOME(values, value.functionRaises()) {
      ZC_REQUIRE(values.size() == 2);
      ZC_EXPECT(hasPredefinedKind(values[0], PredefinedTypeKind::I8));
      ZC_EXPECT(hasPredefinedKind(values[1], PredefinedTypeKind::I16));
    }
  }

  zc::Vector<CanonicalHeaderTypeSyntax> nestedUnionMembers;
  nestedUnionMembers.add(predefined(PredefinedTypeKind::I16));
  nestedUnionMembers.add(predefined(PredefinedTypeKind::I8));
  auto nestedUnion = unionType(zc::mv(nestedUnionMembers));
  zc::Vector<CanonicalHeaderTypeSyntax> unionMembers;
  unionMembers.add(predefined(PredefinedTypeKind::I16));
  unionMembers.add(zc::mv(nestedUnion));
  unionMembers.add(predefined(PredefinedTypeKind::I8));
  auto normalizedUnion = unionType(zc::mv(unionMembers));
  expectHex(normalizedUnion, "04000000000000000202010202"_zc);

  zc::Vector<CanonicalHeaderTypeSyntax> nestedIntersectionMembers;
  nestedIntersectionMembers.add(predefined(PredefinedTypeKind::I8));
  nestedIntersectionMembers.add(predefined(PredefinedTypeKind::I16));
  auto nestedIntersection = intersectionType(zc::mv(nestedIntersectionMembers));
  zc::Vector<CanonicalHeaderTypeSyntax> intersectionMembers;
  intersectionMembers.add(predefined(PredefinedTypeKind::I16));
  intersectionMembers.add(zc::mv(nestedIntersection));
  auto normalizedIntersection = intersectionType(zc::mv(intersectionMembers));
  expectHex(normalizedIntersection, "05000000000000000202010202"_zc);

  zc::Vector<CanonicalHeaderTypeSyntax> singletonMembers;
  singletonMembers.add(predefined(PredefinedTypeKind::I8));
  singletonMembers.add(predefined(PredefinedTypeKind::I8));
  auto singleton = unionType(zc::mv(singletonMembers));
  ZC_EXPECT(singleton.kind() == CanonicalHeaderTypeSyntaxKind::Predefined);

  zc::Vector<CanonicalObjectTypeMember> objectMembers;
  objectMembers.add(objectMember("b"_zc, PredefinedTypeKind::I16, false, true));
  objectMembers.add(objectMember("a"_zc, PredefinedTypeKind::I8));
  objectMembers.add(objectMember("a"_zc, PredefinedTypeKind::I8));
  objectMembers.add(objectMember("a"_zc, PredefinedTypeKind::I16));
  auto object = CanonicalHeaderTypeSyntax::object(zc::mv(objectMembers));
  expectRoundTrip(object);
  ZC_IF_SOME(members, object.objectMembers()) {
    ZC_REQUIRE(members.size() == 3);
    ZC_EXPECT(members[0].name() == "a"_zc);
    ZC_EXPECT(hasPredefinedKind(members[0].type(), PredefinedTypeKind::I8));
    ZC_EXPECT(members[1].name() == "a"_zc);
    ZC_EXPECT(hasPredefinedKind(members[1].type(), PredefinedTypeKind::I16));
    ZC_EXPECT(members[2].name() == "b"_zc);
  }

  zc::Vector<CanonicalNameReference> markers;
  markers.add(relativeName("B"_zc));
  markers.add(relativeName("A"_zc));
  markers.add(relativeName("A"_zc));
  zc::Vector<CanonicalAssociatedBinding> bindings;
  bindings.add(binding("B"_zc, PredefinedTypeKind::I16));
  bindings.add(binding("A"_zc, PredefinedTypeKind::I8));
  bindings.add(binding("A"_zc, PredefinedTypeKind::I8));
  auto dynamic =
      CanonicalHeaderTypeSyntax::dynamic(namedHeader("P"_zc), zc::mv(markers), zc::mv(bindings));
  expectRoundTrip(dynamic);
  ZC_IF_SOME(values, dynamic.dynamicMarkers()) {
    ZC_REQUIRE(values.size() == 2);
    ZC_EXPECT(values[0].suffix()[0].text() == "A"_zc);
    ZC_EXPECT(values[1].suffix()[0].text() == "B"_zc);
  }
  ZC_IF_SOME(values, dynamic.dynamicBindings()) {
    ZC_REQUIRE(values.size() == 2);
    ZC_EXPECT(values[0].name() == "A"_zc);
    ZC_EXPECT(values[1].name() == "B"_zc);
  }
}

ZC_TEST("CanonicalHeaderTypeSyntax preserves ordered children clone accessors and array identity") {
  zc::Vector<CanonicalHeaderTypeSyntax> arguments;
  arguments.add(predefined(PredefinedTypeKind::I16));
  arguments.add(predefined(PredefinedTypeKind::I8));
  auto named = CanonicalHeaderTypeSyntax::named(
      CanonicalNamedHeaderType::from(relativeName("N"_zc), zc::mv(arguments)));
  expectRoundTrip(named);
  ZC_IF_SOME(type, named.namedType()) {
    ZC_REQUIRE(type.arguments().size() == 2);
    ZC_EXPECT(hasPredefinedKind(type.arguments()[0], PredefinedTypeKind::I16));
    ZC_EXPECT(hasPredefinedKind(type.arguments()[1], PredefinedTypeKind::I8));
  }

  zc::Vector<CanonicalHeaderTypeSyntax> parameters;
  parameters.add(predefined(PredefinedTypeKind::I16));
  parameters.add(predefined(PredefinedTypeKind::I8));
  auto function = functionType(zc::mv(parameters), predefined(PredefinedTypeKind::Unit));
  ZC_IF_SOME(values, function.functionParameters()) {
    ZC_REQUIRE(values.size() == 2);
    ZC_EXPECT(hasPredefinedKind(values[0], PredefinedTypeKind::I16));
    ZC_EXPECT(hasPredefinedKind(values[1], PredefinedTypeKind::I8));
  }
  ZC_IF_SOME(result, function.functionResult()) {
    ZC_EXPECT(hasPredefinedKind(result, PredefinedTypeKind::Unit));
  }
  ZC_EXPECT(function.functionRaises() == zc::none);

  zc::Maybe<CanonicalHeaderTypeSyntax> noInterface;
  auto projection = CanonicalHeaderTypeSyntax::associatedProjection(
      predefined(PredefinedTypeKind::I8), zc::mv(noInterface), identifier("x"_zc));
  expectRoundTrip(projection);
  ZC_EXPECT(projection.associatedInterface() == zc::none);

  zc::Vector<CanonicalHeaderTypeSyntax> elements;
  elements.add(predefined(PredefinedTypeKind::I16));
  elements.add(predefined(PredefinedTypeKind::I8));
  auto tuple = CanonicalHeaderTypeSyntax::tuple(zc::mv(elements));
  ZC_IF_SOME(values, tuple.tupleElements()) {
    ZC_REQUIRE(values.size() == 2);
    ZC_EXPECT(hasPredefinedKind(values[0], PredefinedTypeKind::I16));
    ZC_EXPECT(hasPredefinedKind(values[1], PredefinedTypeKind::I8));
  }

  auto fixed = CanonicalHeaderTypeSyntax::fixedArray(predefined(PredefinedTypeKind::I8), 3);
  ZC_EXPECT(fixed.fixedArrayLength() == 3);
  ZC_IF_SOME(element, fixed.element()) {
    ZC_EXPECT(hasPredefinedKind(element, PredefinedTypeKind::I8));
  }
  ZC_EXPECT(fixed.clone().encode().asPtr() == fixed.encode().asPtr());

  auto dynamicArray = CanonicalHeaderTypeSyntax::dynamicArray(predefined(PredefinedTypeKind::I8));
  auto slice = CanonicalHeaderTypeSyntax::slice(predefined(PredefinedTypeKind::I8));
  ZC_EXPECT(dynamicArray.kind() == CanonicalHeaderTypeSyntaxKind::DynamicArray);
  ZC_EXPECT(slice.kind() == CanonicalHeaderTypeSyntaxKind::Slice);
  ZC_EXPECT(dynamicArray.encode().asPtr() != slice.encode().asPtr());
  expectHex(dynamicArray, "070201"_zc);
  expectHex(slice, "080201"_zc);
}

}  // namespace zomlang::compiler::identity
