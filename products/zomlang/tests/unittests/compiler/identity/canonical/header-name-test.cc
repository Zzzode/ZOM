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

#include "zomlang/compiler/identity/canonical/header-name.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/canonical/canonical-decoder.h"

namespace zomlang::compiler::identity {
namespace {

SemanticIdentifier name(zc::StringPtr text) {
  auto value = SemanticIdentifier::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid canonical header name test input");
}

zc::Vector<SemanticIdentifier> suffix(zc::StringPtr first, zc::StringPtr second = nullptr) {
  zc::Vector<SemanticIdentifier> result;
  if (first != nullptr) { result.add(name(first)); }
  if (second != nullptr) { result.add(name(second)); }
  return result;
}

zc::Array<uint8_t> decodedBytes(zc::StringPtr hex) {
  auto bytes = zc::decodeHex(hex);
  ZC_REQUIRE(bytes != zc::none);
  return zc::mv(ZC_REQUIRE_NONNULL(bytes));
}

}  // namespace

ZC_TEST("CanonicalNameReference passes fixed absolute and generic vectors") {
  auto absolute =
      CanonicalNameReference::from(CanonicalNameRoot::absolute(), suffix("pkg"_zc, "Type"_zc));
  ZC_REQUIRE(absolute != zc::none);
  ZC_IF_SOME(value, absolute) {
    ZC_EXPECT(zc::encodeHex(value.encode().asPtr()) ==
              "0100000000000000020000000000000003706b67000000000000000454797065"_zc);
    ZC_EXPECT(value.clone().encode().asPtr() == value.encode().asPtr());
  }

  auto generic = CanonicalNameReference::from(CanonicalNameRoot::generic(1, 2), suffix(nullptr));
  ZC_REQUIRE(generic != zc::none);
  ZC_IF_SOME(value, generic) {
    ZC_EXPECT(zc::encodeHex(value.encode().asPtr()) == "0300000001000000020000000000000000"_zc);
    ZC_EXPECT(value.root().binderDepth() == 1);
    ZC_EXPECT(value.root().ordinal() == 2);
    ZC_EXPECT(value.suffix().size() == 0);
  }
}

ZC_TEST("CanonicalNameReference rejects empty absolute and relative roots") {
  ZC_EXPECT(CanonicalNameReference::from(CanonicalNameRoot::absolute(), suffix(nullptr)) ==
            zc::none);
  ZC_EXPECT(CanonicalNameReference::from(CanonicalNameRoot::relative(), suffix(nullptr)) ==
            zc::none);
}

ZC_TEST("CanonicalNameReference decodes complete vectors for every root") {
  const auto expectRoundTrip = [](zc::StringPtr hex, CanonicalNameRootKind expectedRoot) {
    auto bytes = decodedBytes(hex);
    CanonicalDecoder decoder(bytes.asPtr());
    auto value = CanonicalNameReference::decodeCanonical(decoder);
    ZC_REQUIRE(value != zc::none);
    ZC_EXPECT(decoder.finished());
    ZC_IF_SOME(admitted, value) {
      ZC_EXPECT(admitted.root().kind() == expectedRoot);
      ZC_EXPECT(admitted.encode().asPtr() == bytes.asPtr());
    }
  };
  expectRoundTrip("0100000000000000020000000000000003706b67000000000000000454797065"_zc,
                  CanonicalNameRootKind::Absolute);
  expectRoundTrip("020000000000000001000000000000000454797065"_zc, CanonicalNameRootKind::Relative);
  expectRoundTrip("0300000001000000020000000000000000"_zc, CanonicalNameRootKind::Generic);

  auto generic = decodedBytes("0300000001000000020000000000000000"_zc);
  CanonicalDecoder genericDecoder(generic.asPtr());
  auto genericValue = CanonicalNameReference::decodeCanonical(genericDecoder);
  ZC_REQUIRE(genericValue != zc::none);
  ZC_IF_SOME(admitted, genericValue) {
    ZC_EXPECT(admitted.root().binderDepth() == 1);
    ZC_EXPECT(admitted.root().ordinal() == 2);
  }
}

ZC_TEST("CanonicalNameReference rejects unknown roots hostile counts and every truncation") {
  auto unknownRoot = decodedBytes("ff0000000000000000"_zc);
  CanonicalDecoder unknownRootDecoder(unknownRoot.asPtr());
  ZC_EXPECT(CanonicalNameReference::decodeCanonical(unknownRootDecoder) == zc::none);

  auto hostileCount = decodedBytes("030000000100000002ffffffffffffffff"_zc);
  CanonicalDecoder hostileCountDecoder(hostileCount.asPtr());
  ZC_EXPECT(CanonicalNameReference::decodeCanonical(hostileCountDecoder) == zc::none);

  auto allocationCount = decodedBytes("0300000001000000020000000000010000"_zc);
  CanonicalDecoder allocationCountDecoder(allocationCount.asPtr());
  ZC_EXPECT(CanonicalNameReference::decodeCanonical(allocationCountDecoder) == zc::none);

  auto emptyAbsolute = decodedBytes("010000000000000000"_zc);
  CanonicalDecoder emptyAbsoluteDecoder(emptyAbsolute.asPtr());
  ZC_EXPECT(CanonicalNameReference::decodeCanonical(emptyAbsoluteDecoder) == zc::none);

  const auto expectEveryTruncation = [](zc::StringPtr hex) {
    auto valid = decodedBytes(hex);
    for (size_t size = 0; size < valid.size(); ++size) {
      CanonicalDecoder decoder(valid.asPtr().slice(0, size));
      ZC_EXPECT(CanonicalNameReference::decodeCanonical(decoder) == zc::none);
    }
  };
  expectEveryTruncation("0100000000000000020000000000000003706b67000000000000000454797065"_zc);
  expectEveryTruncation("020000000000000001000000000000000454797065"_zc);
  expectEveryTruncation("0300000001000000020000000000000000"_zc);
}

ZC_TEST("Canonical header enums reject unknown tags") {
  ZC_EXPECT(!isCanonicalHeaderValue(static_cast<CallableHeaderKind>(0xff)));
  ZC_EXPECT(!isCanonicalHeaderValue(static_cast<ReceiverShape>(0xff)));
  ZC_EXPECT(!isCanonicalHeaderValue(static_cast<ExternalAbi>(0xff)));
  ZC_EXPECT(!isCanonicalHeaderValue(static_cast<PredefinedTypeKind>(0xff)));
  ZC_EXPECT(!isCanonicalHeaderValue(static_cast<ReferenceMutability>(0xff)));
  ZC_EXPECT(!isCanonicalHeaderValue(static_cast<RawPointerMutability>(0xff)));
  ZC_EXPECT(!isCanonicalHeaderValue(static_cast<CanonicalNameRootKind>(0xff)));
}

}  // namespace zomlang::compiler::identity
