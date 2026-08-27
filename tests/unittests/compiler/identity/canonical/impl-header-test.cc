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

#include "compiler/identity/canonical/impl-header.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "compiler/identity/canonical/canonical-decoder.h"

namespace zomlang::compiler::identity {
namespace {

SemanticIdentifier identifier(zc::StringPtr text) {
  auto value = SemanticIdentifier::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid canonical trait reference test identifier");
}

CanonicalNameReference name(CanonicalNameRoot&& root, zc::StringPtr first,
                            zc::StringPtr second = nullptr) {
  zc::Vector<SemanticIdentifier> suffix;
  suffix.add(identifier(first));
  if (second != nullptr) { suffix.add(identifier(second)); }
  auto value = CanonicalNameReference::from(zc::mv(root), zc::mv(suffix));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid canonical trait reference test name was rejected");
}

CanonicalHeaderTypeSyntax predefined(PredefinedTypeKind kind) {
  auto value = CanonicalHeaderTypeSyntax::predefined(kind);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid canonical trait argument was rejected");
}

zc::Vector<CanonicalHeaderTypeSyntax> emptyArguments() {
  return zc::Vector<CanonicalHeaderTypeSyntax>();
}

CanonicalHeaderTypeSyntax namedType(CanonicalNameRoot&& root, zc::StringPtr text) {
  return CanonicalHeaderTypeSyntax::named(
      CanonicalNamedHeaderType::from(name(zc::mv(root), text), emptyArguments()));
}

CanonicalTraitReference trait(zc::StringPtr text) {
  auto value =
      CanonicalTraitReference::from(name(CanonicalNameRoot::relative(), text), emptyArguments());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid canonical implementation trait was rejected");
}

CanonicalBoundObligation obligation(zc::StringPtr subject, zc::StringPtr bound) {
  return CanonicalBoundObligation::from(namedType(CanonicalNameRoot::relative(), subject),
                                        namedType(CanonicalNameRoot::relative(), bound));
}

template <typename Value>
void expectRoundTrip(const Value& value) {
  auto bytes = value.encode();
  CanonicalDecoder decoder(bytes.asPtr());
  auto decoded = Value::decodeCanonical(decoder);
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(decoder.finished());
  ZC_IF_SOME(admitted, decoded) { ZC_EXPECT(admitted.encode().asPtr() == bytes.asPtr()); }
  for (size_t size = 0; size < bytes.size(); ++size) {
    CanonicalDecoder truncated(bytes.asPtr().slice(0, size));
    ZC_EXPECT(Value::decodeCanonical(truncated) == zc::none);
  }
}

zc::Array<uint8_t> decodedBytes(zc::StringPtr hex) {
  auto bytes = zc::decodeHex(hex);
  ZC_REQUIRE(bytes != zc::none);
  return zc::mv(ZC_REQUIRE_NONNULL(bytes));
}

}  // namespace

ZC_TEST("Canonical impl polarity and safety retain exact closed tags") {
  ZC_EXPECT(static_cast<uint8_t>(ImplPolarity::Positive) == 0x01);
  ZC_EXPECT(static_cast<uint8_t>(ImplPolarity::Negative) == 0x02);
  ZC_EXPECT(static_cast<uint8_t>(ImplSafety::Safe) == 0x01);
  ZC_EXPECT(static_cast<uint8_t>(ImplSafety::Unsafe) == 0x02);
  ZC_EXPECT(isCanonicalImplHeaderValue(ImplPolarity::Positive));
  ZC_EXPECT(isCanonicalImplHeaderValue(ImplPolarity::Negative));
  ZC_EXPECT(isCanonicalImplHeaderValue(ImplSafety::Safe));
  ZC_EXPECT(isCanonicalImplHeaderValue(ImplSafety::Unsafe));
  ZC_EXPECT(!isCanonicalImplHeaderValue(static_cast<ImplPolarity>(0xff)));
  ZC_EXPECT(!isCanonicalImplHeaderValue(static_cast<ImplSafety>(0xff)));
}

ZC_TEST("CanonicalTraitReference admits absolute and relative roots but rejects generic roots") {
  auto absolute = CanonicalTraitReference::from(name(CanonicalNameRoot::absolute(), "Trait"_zc),
                                                emptyArguments());
  auto relative = CanonicalTraitReference::from(name(CanonicalNameRoot::relative(), "Trait"_zc),
                                                emptyArguments());
  auto generic = CanonicalTraitReference::from(name(CanonicalNameRoot::generic(1, 2), "Trait"_zc),
                                               emptyArguments());

  ZC_REQUIRE(absolute != zc::none);
  ZC_REQUIRE(relative != zc::none);
  ZC_EXPECT(generic == zc::none);
  ZC_IF_SOME(value, absolute) {
    ZC_EXPECT(value.name().root().kind() == CanonicalNameRootKind::Absolute);
  }
  ZC_IF_SOME(value, relative) {
    ZC_EXPECT(value.name().root().kind() == CanonicalNameRootKind::Relative);
  }
}

ZC_TEST("CanonicalTraitReference passes the exact fieldwise vector and preserves arguments") {
  zc::Vector<CanonicalHeaderTypeSyntax> arguments;
  arguments.add(predefined(PredefinedTypeKind::I16));
  arguments.add(predefined(PredefinedTypeKind::I8));
  arguments.add(predefined(PredefinedTypeKind::I16));
  auto admitted = CanonicalTraitReference::from(
      name(CanonicalNameRoot::absolute(), "pkg"_zc, "Trait"_zc), zc::mv(arguments));
  ZC_REQUIRE(admitted != zc::none);
  ZC_IF_SOME(value, admitted) {
    ZC_EXPECT(
        zc::encodeHex(value.encode().asPtr()) ==
        "0100000000000000020000000000000003706b67000000000000000554726169740000000000000003020202010202"_zc);
    ZC_EXPECT(value.clone().encode().asPtr() == value.encode().asPtr());
    ZC_REQUIRE(value.arguments().size() == 3);
    ZC_EXPECT(value.arguments()[0].predefinedKind() == PredefinedTypeKind::I16);
    ZC_EXPECT(value.arguments()[1].predefinedKind() == PredefinedTypeKind::I8);
    ZC_EXPECT(value.arguments()[2].predefinedKind() == PredefinedTypeKind::I16);
    expectRoundTrip(value);
  }
}

ZC_TEST("Canonical generic parameters and bound obligations decode compositionally") {
  zc::Maybe<CanonicalHeaderTypeSyntax> defaultType(predefined(PredefinedTypeKind::I16));
  auto generic = CanonicalGenericParameter::from(zc::mv(defaultType));
  auto bound = obligation("T"_zc, "Bound"_zc);

  expectRoundTrip(generic);
  expectRoundTrip(bound);

  auto invalidPresence = decodedBytes("ff"_zc);
  CanonicalDecoder invalidPresenceDecoder(invalidPresence.asPtr());
  ZC_EXPECT(CanonicalGenericParameter::decodeCanonical(invalidPresenceDecoder) == zc::none);
}

ZC_TEST("ImplHeader passes the exact RFC 0018 fieldwise vector") {
  zc::Vector<CanonicalGenericParameter> generics;
  zc::Vector<CanonicalBoundObligation> obligations;
  auto admitted = ImplHeader::from(
      zc::mv(generics), ImplPolarity::Positive, ImplSafety::Safe, trait("Trait"_zc),
      predefined(PredefinedTypeKind::I32), zc::mv(obligations));
  ZC_REQUIRE(admitted != zc::none);
  ZC_IF_SOME(value, admitted) {
    ZC_EXPECT(zc::encodeHex(value.encode().asPtr()) ==
              "0000000000000000010102000000000000000100000000000000055472616974"
              "000000000000000002030000000000000000"_zc);
    ZC_EXPECT(value.clone().encode().asPtr() == value.encode().asPtr());
    ZC_EXPECT(value.polarity() == ImplPolarity::Positive);
    ZC_EXPECT(value.safety() == ImplSafety::Safe);
    ZC_EXPECT(value.trait().name().root().kind() == CanonicalNameRootKind::Relative);
    ZC_EXPECT(value.selfType().predefinedKind() == PredefinedTypeKind::I32);
    expectRoundTrip(value);
  }
}

ZC_TEST("ImplHeader retains generic order and sorts unique obligations") {
  zc::Vector<CanonicalGenericParameter> generics;
  zc::Maybe<CanonicalHeaderTypeSyntax> firstDefault(predefined(PredefinedTypeKind::I8));
  generics.add(CanonicalGenericParameter::from(zc::mv(firstDefault)));
  zc::Maybe<CanonicalHeaderTypeSyntax> noDefault;
  generics.add(CanonicalGenericParameter::from(zc::mv(noDefault)));
  zc::Vector<CanonicalBoundObligation> obligations;
  obligations.add(obligation("T"_zc, "B"_zc));
  obligations.add(obligation("T"_zc, "A"_zc));
  obligations.add(obligation("T"_zc, "B"_zc));
  auto admitted = ImplHeader::from(
      zc::mv(generics), ImplPolarity::Negative, ImplSafety::Safe, trait("Marker"_zc),
      namedType(CanonicalNameRoot::relative(), "T"_zc), zc::mv(obligations));
  ZC_REQUIRE(admitted != zc::none);
  ZC_IF_SOME(value, admitted) {
    ZC_REQUIRE(value.genericParameters().size() == 2);
    ZC_REQUIRE(value.genericParameters()[0].defaultType() != zc::none);
    ZC_EXPECT(value.genericParameters()[1].defaultType() == zc::none);
    ZC_REQUIRE(value.obligations().size() == 2);
    const auto expectedFirst = obligation("T"_zc, "A"_zc).encode();
    const auto expectedSecond = obligation("T"_zc, "B"_zc).encode();
    ZC_EXPECT(value.obligations()[0].encode().asPtr() == expectedFirst.asPtr());
    ZC_EXPECT(value.obligations()[1].encode().asPtr() == expectedSecond.asPtr());
    expectRoundTrip(value);
  }
}

ZC_TEST("ImplHeader rejects values outside the closed tag domains") {
  zc::Vector<CanonicalGenericParameter> invalidPolarityGenerics;
  zc::Vector<CanonicalBoundObligation> invalidPolarityObligations;
  auto invalidPolarity = ImplHeader::from(
      zc::mv(invalidPolarityGenerics), static_cast<ImplPolarity>(0xff), ImplSafety::Safe,
      trait("Trait"_zc), predefined(PredefinedTypeKind::I32), zc::mv(invalidPolarityObligations));
  zc::Vector<CanonicalGenericParameter> invalidSafetyGenerics;
  zc::Vector<CanonicalBoundObligation> invalidSafetyObligations;
  auto invalidSafety = ImplHeader::from(
      zc::mv(invalidSafetyGenerics), ImplPolarity::Positive, static_cast<ImplSafety>(0xff),
      trait("Trait"_zc), predefined(PredefinedTypeKind::I32), zc::mv(invalidSafetyObligations));
  ZC_EXPECT(invalidPolarity == zc::none);
  ZC_EXPECT(invalidSafety == zc::none);
}

ZC_TEST("ImplHeader rejects the negative unsafe combination") {
  zc::Vector<CanonicalGenericParameter> generics;
  zc::Vector<CanonicalBoundObligation> obligations;
  auto rejected = ImplHeader::from(
      zc::mv(generics), ImplPolarity::Negative, ImplSafety::Unsafe, trait("Marker"_zc),
      predefined(PredefinedTypeKind::I32), zc::mv(obligations));

  ZC_EXPECT(rejected == zc::none);
}

ZC_TEST("ImplHeader admits the positive unsafe combination") {
  zc::Vector<CanonicalGenericParameter> generics;
  zc::Vector<CanonicalBoundObligation> obligations;
  auto admitted = ImplHeader::from(
      zc::mv(generics), ImplPolarity::Positive, ImplSafety::Unsafe, trait("UnsafeTrait"_zc),
      predefined(PredefinedTypeKind::I32), zc::mv(obligations));

  ZC_REQUIRE(admitted != zc::none);
  ZC_IF_SOME(value, admitted) { expectRoundTrip(value); }
}

ZC_TEST("Canonical implementation decoders reject hostile counts roots and closed tags") {
  auto hostileCount = decodedBytes("0000000000010000"_zc);
  CanonicalDecoder hostileCountDecoder(hostileCount.asPtr());
  ZC_EXPECT(ImplHeader::decodeCanonical(hostileCountDecoder) == zc::none);

  zc::Vector<CanonicalHeaderTypeSyntax> traitArguments;
  auto validTrait = CanonicalTraitReference::from(name(CanonicalNameRoot::relative(), "Trait"_zc),
                                                  zc::mv(traitArguments));
  ZC_REQUIRE(validTrait != zc::none);
  ZC_IF_SOME(value, validTrait) {
    auto bytes = value.encode();
    const size_t argumentCountOffset = bytes.size() - sizeof(uint64_t);
    bytes[argumentCountOffset + 5] = 0x01;
    CanonicalDecoder hostileArgumentsDecoder(bytes.asPtr());
    ZC_EXPECT(CanonicalTraitReference::decodeCanonical(hostileArgumentsDecoder) == zc::none);
  }

  auto genericRootTrait =
      decodedBytes("030000000000000000000000000000000000000000000000000000000000000000"_zc);
  CanonicalDecoder genericRootDecoder(genericRootTrait.asPtr());
  ZC_EXPECT(CanonicalTraitReference::decodeCanonical(genericRootDecoder) == zc::none);

  zc::Vector<CanonicalGenericParameter> generics;
  zc::Vector<CanonicalBoundObligation> obligations;
  auto header = ImplHeader::from(zc::mv(generics), ImplPolarity::Positive,
                                          ImplSafety::Safe, trait("Trait"_zc),
                                          predefined(PredefinedTypeKind::I32), zc::mv(obligations));
  ZC_REQUIRE(header != zc::none);
  ZC_IF_SOME(value, header) {
    const auto expectMutationRejected = [&value](size_t offset, uint8_t byte) {
      auto bytes = value.encode();
      bytes[offset] = byte;
      CanonicalDecoder decoder(bytes.asPtr());
      ZC_EXPECT(ImplHeader::decodeCanonical(decoder) == zc::none);
    };
    expectMutationRejected(8, 0xff);
    expectMutationRejected(9, 0xff);
    auto bytes = value.encode();
    bytes[8] = 0x02;
    bytes[9] = 0x02;
    CanonicalDecoder negativeUnsafeDecoder(bytes.asPtr());
    ZC_EXPECT(ImplHeader::decodeCanonical(negativeUnsafeDecoder) == zc::none);
  }
}

}  // namespace zomlang::compiler::identity
