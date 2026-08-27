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

#include "zomlang/compiler/identity/canonical/overload-header.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::identity {
namespace {

SemanticIdentifier identifier(zc::StringPtr text) {
  auto value = SemanticIdentifier::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid canonical overload test identifier");
}

DeclaredDefinitionName declaredName(zc::StringPtr text = "f"_zc) {
  auto value = DeclaredDefinitionName::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid canonical overload test name");
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

CanonicalGenericParameter genericNone() {
  zc::Maybe<CanonicalHeaderTypeSyntax> defaultType;
  return CanonicalGenericParameter::from(zc::mv(defaultType));
}

CanonicalGenericParameter genericWith(PredefinedTypeKind kind) {
  zc::Maybe<CanonicalHeaderTypeSyntax> defaultType = predefined(kind);
  return CanonicalGenericParameter::from(zc::mv(defaultType));
}

CanonicalBoundObligation obligation(PredefinedTypeKind subject, PredefinedTypeKind bound) {
  return CanonicalBoundObligation::from(predefined(subject), predefined(bound));
}

CanonicalCallableParameter parameter(zc::StringPtr label, PredefinedTypeKind type,
                                     bool hasDefault) {
  return CanonicalCallableParameter::from(identifier(label), predefined(type), hasDefault);
}

zc::Maybe<OverloadHeader> emptyHeader(CallableHeaderKind kind,
                                               CanonicalCallableResult&& result,
                                               zc::Maybe<ReceiverShape> receiver = zc::none,
                                               zc::Maybe<ExternalAbi> externalAbi = zc::none) {
  zc::Vector<CanonicalGenericParameter> generics;
  zc::Vector<CanonicalBoundObligation> obligations;
  zc::Vector<CanonicalCallableParameter> parameters;
  zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>> raises;
  return OverloadHeader::from(kind, declaredName(), zc::mv(receiver), zc::mv(generics),
                                       zc::mv(obligations), zc::mv(parameters), zc::mv(result),
                                       zc::mv(raises), zc::mv(externalAbi));
}

void expectHex(zc::ArrayPtr<const uint8_t> bytes, zc::StringPtr expected) {
  ZC_EXPECT(zc::encodeHex(bytes) == expected);
}

bool hasKind(const CanonicalHeaderTypeSyntax& type, PredefinedTypeKind kind) {
  return type.predefinedKind() == kind;
}

}  // namespace

ZC_TEST("Canonical overload leaf records and callable results pass exact vectors") {
  auto unit = CanonicalCallableResult::unit();
  auto explicitUnit = CanonicalCallableResult::type(predefined(PredefinedTypeKind::Unit));
  auto constructor = CanonicalCallableResult::constructorSelf();
  auto typed = CanonicalCallableResult::type(predefined(PredefinedTypeKind::I8));
  expectHex(unit.encode().asPtr(), "01"_zc);
  expectHex(explicitUnit.encode().asPtr(), "01"_zc);
  expectHex(constructor.encode().asPtr(), "02"_zc);
  expectHex(typed.encode().asPtr(), "030201"_zc);
  ZC_EXPECT(explicitUnit.encode().asPtr() == unit.encode().asPtr());
  ZC_EXPECT(explicitUnit.kind() == CanonicalCallableResultKind::Unit);
  ZC_EXPECT(explicitUnit.type() == zc::none);
  ZC_EXPECT(unit.kind() == CanonicalCallableResultKind::Unit);
  ZC_EXPECT(constructor.kind() == CanonicalCallableResultKind::ConstructorSelf);
  ZC_IF_SOME(type, typed.type()) { ZC_EXPECT(hasKind(type, PredefinedTypeKind::I8)); }

  auto absentGeneric = genericNone();
  auto presentGeneric = genericWith(PredefinedTypeKind::I16);
  expectHex(absentGeneric.encode().asPtr(), "00"_zc);
  expectHex(presentGeneric.encode().asPtr(), "010202"_zc);
  auto bound = obligation(PredefinedTypeKind::I8, PredefinedTypeKind::I16);
  expectHex(bound.encode().asPtr(), "02010202"_zc);
  auto callableParameter = parameter("x"_zc, PredefinedTypeKind::I8, true);
  expectHex(callableParameter.encode().asPtr(), "000000000000000178020101"_zc);
  ZC_EXPECT(callableParameter.label() == "x"_zc);
  ZC_EXPECT(callableParameter.hasDefault());
}

ZC_TEST("Canonical overload header passes the complete nine-field vector clone and accessors") {
  zc::Maybe<ReceiverShape> receiver;
  zc::Vector<CanonicalGenericParameter> generics;
  generics.add(genericWith(PredefinedTypeKind::I16));
  generics.add(genericNone());
  zc::Vector<CanonicalBoundObligation> obligations;
  obligations.add(obligation(PredefinedTypeKind::I8, PredefinedTypeKind::I16));
  zc::Vector<CanonicalCallableParameter> parameters;
  parameters.add(parameter("z"_zc, PredefinedTypeKind::I16, false));
  parameters.add(parameter("z"_zc, PredefinedTypeKind::I16, false));
  parameters.add(parameter("a"_zc, PredefinedTypeKind::I8, true));
  zc::Vector<CanonicalHeaderTypeSyntax> raiseValues;
  raiseValues.add(predefined(PredefinedTypeKind::I16));
  raiseValues.add(predefined(PredefinedTypeKind::I8));
  raiseValues.add(predefined(PredefinedTypeKind::I8));
  zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>> raises = zc::mv(raiseValues);
  zc::Maybe<ExternalAbi> externalAbi = ExternalAbi::Cdecl;
  auto admitted = OverloadHeader::from(
      CallableHeaderKind::Function, declaredName(), zc::mv(receiver), zc::mv(generics),
      zc::mv(obligations), zc::mv(parameters),
      CanonicalCallableResult::type(predefined(PredefinedTypeKind::I8)), zc::mv(raises),
      zc::mv(externalAbi));
  ZC_REQUIRE(admitted != zc::none);
  ZC_IF_SOME(header, admitted) {
    expectHex(
        header.encode().asPtr(),
        "0100000000000000016600000000000000000201020200000000000000000102010202000000000000000300000000000000017a02020000000000000000017a020200000000000000000161020101030201010000000000000002020102020101"_zc);
    ZC_EXPECT(header.clone().encode().asPtr() == header.encode().asPtr());
    ZC_EXPECT(header.callableKind() == CallableHeaderKind::Function);
    ZC_EXPECT(header.name() == "f"_zc);
    ZC_EXPECT(header.receiver() == zc::none);
    ZC_REQUIRE(header.genericParameters().size() == 2);
    ZC_IF_SOME(type, header.genericParameters()[0].defaultType()) {
      ZC_EXPECT(hasKind(type, PredefinedTypeKind::I16));
    }
    ZC_EXPECT(header.genericParameters()[1].defaultType() == zc::none);
    ZC_REQUIRE(header.obligations().size() == 1);
    ZC_EXPECT(hasKind(header.obligations()[0].subject(), PredefinedTypeKind::I8));
    ZC_EXPECT(hasKind(header.obligations()[0].bound(), PredefinedTypeKind::I16));
    ZC_REQUIRE(header.parameters().size() == 3);
    ZC_EXPECT(header.parameters()[0].label() == "z"_zc);
    ZC_EXPECT(header.parameters()[1].label() == "z"_zc);
    ZC_EXPECT(header.parameters()[2].label() == "a"_zc);
    ZC_EXPECT(header.result().kind() == CanonicalCallableResultKind::Type);
    ZC_IF_SOME(type, header.result().type()) { ZC_EXPECT(hasKind(type, PredefinedTypeKind::I8)); }
    ZC_IF_SOME(values, header.raises()) {
      ZC_REQUIRE(values.size() == 2);
      ZC_EXPECT(hasKind(values[0], PredefinedTypeKind::I8));
      ZC_EXPECT(hasKind(values[1], PredefinedTypeKind::I16));
    }
    ZC_EXPECT(header.externalAbi() == ExternalAbi::Cdecl);
  }
}

ZC_TEST("Canonical overload header rejects invalid tags and cross-kind admission") {
  ZC_EXPECT(emptyHeader(static_cast<CallableHeaderKind>(0xff), CanonicalCallableResult::unit()) ==
            zc::none);
  ZC_EXPECT(emptyHeader(CallableHeaderKind::Function, CanonicalCallableResult::unit(),
                        static_cast<ReceiverShape>(0xff)) == zc::none);
  ZC_EXPECT(emptyHeader(CallableHeaderKind::Function, CanonicalCallableResult::unit(), zc::none,
                        static_cast<ExternalAbi>(0xff)) == zc::none);

  ZC_EXPECT(emptyHeader(CallableHeaderKind::Function, CanonicalCallableResult::unit()) != zc::none);
  ZC_EXPECT(emptyHeader(CallableHeaderKind::Function,
                        CanonicalCallableResult::type(predefined(PredefinedTypeKind::I8)), zc::none,
                        ExternalAbi::Cdecl) != zc::none);
  ZC_EXPECT(emptyHeader(CallableHeaderKind::Function, CanonicalCallableResult::constructorSelf()) ==
            zc::none);

  ZC_EXPECT(emptyHeader(CallableHeaderKind::Method, CanonicalCallableResult::unit(),
                        ReceiverShape::Shared) != zc::none);
  ZC_EXPECT(emptyHeader(CallableHeaderKind::Method, CanonicalCallableResult::constructorSelf()) ==
            zc::none);
  ZC_EXPECT(emptyHeader(CallableHeaderKind::Method, CanonicalCallableResult::unit(), zc::none,
                        ExternalAbi::Cdecl) == zc::none);

  ZC_EXPECT(emptyHeader(CallableHeaderKind::Constructor,
                        CanonicalCallableResult::constructorSelf()) != zc::none);
  ZC_EXPECT(emptyHeader(CallableHeaderKind::Constructor, CanonicalCallableResult::unit()) ==
            zc::none);
  ZC_EXPECT(emptyHeader(CallableHeaderKind::Constructor, CanonicalCallableResult::constructorSelf(),
                        ReceiverShape::Shared) == zc::none);
  ZC_EXPECT(emptyHeader(CallableHeaderKind::Constructor, CanonicalCallableResult::constructorSelf(),
                        zc::none, ExternalAbi::Cdecl) == zc::none);
}

ZC_TEST("Canonical overload header rejects present-empty raises") {
  zc::Maybe<ReceiverShape> receiver;
  zc::Vector<CanonicalGenericParameter> generics;
  zc::Vector<CanonicalBoundObligation> obligations;
  zc::Vector<CanonicalCallableParameter> parameters;
  zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>> raises = zc::Vector<CanonicalHeaderTypeSyntax>();
  zc::Maybe<ExternalAbi> externalAbi;
  ZC_EXPECT(OverloadHeader::from(CallableHeaderKind::Function, declaredName(),
                                          zc::mv(receiver), zc::mv(generics), zc::mv(obligations),
                                          zc::mv(parameters), CanonicalCallableResult::unit(),
                                          zc::mv(raises), zc::mv(externalAbi)) == zc::none);
}

ZC_TEST("Canonical overload header recursively normalizes raises unions") {
  zc::Vector<CanonicalHeaderTypeSyntax> unionMembers;
  unionMembers.add(predefined(PredefinedTypeKind::I16));
  unionMembers.add(predefined(PredefinedTypeKind::I8));
  zc::Vector<CanonicalHeaderTypeSyntax> raiseValues;
  raiseValues.add(predefined(PredefinedTypeKind::I16));
  raiseValues.add(unionType(zc::mv(unionMembers)));
  raiseValues.add(predefined(PredefinedTypeKind::I8));
  zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>> raises = zc::mv(raiseValues);
  zc::Maybe<ReceiverShape> receiver;
  zc::Vector<CanonicalGenericParameter> generics;
  zc::Vector<CanonicalBoundObligation> obligations;
  zc::Vector<CanonicalCallableParameter> parameters;
  zc::Maybe<ExternalAbi> externalAbi;
  auto admitted = OverloadHeader::from(
      CallableHeaderKind::Function, declaredName(), zc::mv(receiver), zc::mv(generics),
      zc::mv(obligations), zc::mv(parameters), CanonicalCallableResult::unit(), zc::mv(raises),
      zc::mv(externalAbi));
  ZC_REQUIRE(admitted != zc::none);
  ZC_IF_SOME(header, admitted) {
    ZC_IF_SOME(values, header.raises()) {
      ZC_REQUIRE(values.size() == 2);
      ZC_EXPECT(hasKind(values[0], PredefinedTypeKind::I8));
      ZC_EXPECT(hasKind(values[1], PredefinedTypeKind::I16));
    }
  }
}

ZC_TEST("Canonical overload header sorts and deduplicates complete obligation bytes") {
  zc::Maybe<ReceiverShape> receiver;
  zc::Vector<CanonicalGenericParameter> generics;
  zc::Vector<CanonicalBoundObligation> obligations;
  obligations.add(obligation(PredefinedTypeKind::I8, PredefinedTypeKind::I32));
  obligations.add(obligation(PredefinedTypeKind::I8, PredefinedTypeKind::I16));
  obligations.add(obligation(PredefinedTypeKind::I8, PredefinedTypeKind::I16));
  zc::Vector<CanonicalCallableParameter> parameters;
  zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>> raises;
  zc::Maybe<ExternalAbi> externalAbi;
  auto admitted = OverloadHeader::from(
      CallableHeaderKind::Function, declaredName(), zc::mv(receiver), zc::mv(generics),
      zc::mv(obligations), zc::mv(parameters), CanonicalCallableResult::unit(), zc::mv(raises),
      zc::mv(externalAbi));
  ZC_REQUIRE(admitted != zc::none);
  ZC_IF_SOME(header, admitted) {
    ZC_REQUIRE(header.obligations().size() == 2);
    ZC_EXPECT(hasKind(header.obligations()[0].bound(), PredefinedTypeKind::I16));
    ZC_EXPECT(hasKind(header.obligations()[1].bound(), PredefinedTypeKind::I32));
  }
}

}  // namespace zomlang::compiler::identity
