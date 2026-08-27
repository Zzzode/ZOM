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

#include "compiler/identity/canonical/canonical-scalar.h"

#include "zc/core/string.h"
#include "zc/ztest/test.h"
#include "compiler/identity/canonical/canonical-decoder.h"
#include "compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::identity {
namespace {

template <typename Scalar>
void expectSource(zc::StringPtr input, zc::StringPtr expected) {
  auto admitted = Scalar::fromSource(input);
  bool matched = false;
  ZC_IF_SOME(value, admitted) {
    ZC_EXPECT(value.text() == expected);
    auto duplicate = value.clone();
    ZC_EXPECT(duplicate == value);
    matched = true;
  }
  ZC_EXPECT(matched);
}

template <typename Scalar>
void expectRejected(zc::StringPtr input) {
  ZC_EXPECT(Scalar::fromSource(input) == zc::none);
}

zc::String repeated(char value, size_t count) {
  auto result = zc::heapString(count);
  for (size_t index = 0; index < count; ++index) { result[index] = value; }
  return result;
}

}  // namespace

ZC_TEST("CanonicalPathSegment normalizes source text and enforces path boundaries") {
  expectSource<CanonicalPathSegment>("caf\x65\xCC\x81.zom"_zc, "caf\xC3\xA9.zom"_zc);
  expectRejected<CanonicalPathSegment>(""_zc);
  expectRejected<CanonicalPathSegment>("."_zc);
  expectRejected<CanonicalPathSegment>(".."_zc);
  expectRejected<CanonicalPathSegment>("a/b"_zc);
  expectRejected<CanonicalPathSegment>("a\\b"_zc);
  expectRejected<CanonicalPathSegment>("a\0b"_zc);
  expectRejected<CanonicalPathSegment>("\xC0\x80"_zc);

  ZC_EXPECT(CanonicalPathSegment::fromCanonical("caf\x65\xCC\x81.zom"_zc) == zc::none);
  expectSource<CanonicalPathSegment>("caf\xC3\xA9.zom"_zc, "caf\xC3\xA9.zom"_zc);
}

ZC_TEST("Canonical manifest names enforce their exact ASCII domains") {
  expectSource<PackageName>("a"_zc, "a"_zc);
  expectSource<PackageName>("async"_zc, "async"_zc);
  expectSource<PackageName>("package_01"_zc, "package_01"_zc);
  expectRejected<PackageName>("Package"_zc);
  expectRejected<PackageName>("1package"_zc);
  expectRejected<PackageName>("package-name"_zc);

  auto sixtyFour = repeated('a', 64);
  auto sixtyFive = repeated('a', 65);
  expectSource<PackageName>(sixtyFour, sixtyFour);
  expectRejected<PackageName>(sixtyFive);

  expectSource<TargetName>("app_1"_zc, "app_1"_zc);
  expectSource<DependencyAlias>("dep_1"_zc, "dep_1"_zc);
  expectRejected<TargetName>("async"_zc);
  expectRejected<DependencyAlias>("class"_zc);

  expectSource<FeatureName>("simd-wide"_zc, "simd-wide"_zc);
  expectRejected<FeatureName>("Simd"_zc);
  expectSource<TargetComponentName>("x86_64-modern.elf"_zc, "x86_64-modern.elf"_zc);
  expectSource<TargetFeatureName>("avx2.0-fast"_zc, "avx2.0-fast"_zc);
  expectRejected<TargetComponentName>("X86_64"_zc);
  expectRejected<TargetFeatureName>("_avx"_zc);

  expectSource<SemanticEnvironmentName>("ZOM_TARGET_1"_zc, "ZOM_TARGET_1"_zc);
  expectSource<SemanticEnvironmentName>("_ZOM"_zc, "_ZOM"_zc);
  expectRejected<SemanticEnvironmentName>("Zom_TARGET"_zc);
  expectRejected<SemanticEnvironmentName>("1ZOM"_zc);
}

ZC_TEST("Canonical semantic names normalize identifiers and reject reserved words") {
  expectSource<SemanticIdentifier>("caf\x65\xCC\x81"_zc, "caf\xC3\xA9"_zc);
  expectSource<ModulePathSegment>("caf\x65\xCC\x81"_zc, "caf\xC3\xA9"_zc);
  expectRejected<SemanticIdentifier>("1name"_zc);
  expectRejected<SemanticIdentifier>("async"_zc);
  expectRejected<ModulePathSegment>("class"_zc);
  expectRejected<SemanticIdentifier>("\xC0\x80"_zc);
  ZC_EXPECT(SemanticIdentifier::fromCanonical("caf\x65\xCC\x81"_zc) == zc::none);

  expectSource<DeclaredDefinitionName>("this"_zc, "this"_zc);
  expectSource<DeclaredDefinitionName>("init"_zc, "init"_zc);
  expectSource<DeclaredDefinitionName>("deinit"_zc, "deinit"_zc);
  expectSource<DeclaredDefinitionName>("get"_zc, "get"_zc);
  expectSource<DeclaredDefinitionName>("set"_zc, "set"_zc);
  expectSource<DeclaredDefinitionName>("value"_zc, "value"_zc);
  expectRejected<DeclaredDefinitionName>("class"_zc);
}

ZC_TEST("Canonical scalar encoding uses normalized UTF-8 byte strings") {
  auto admitted = SemanticIdentifier::fromSource("caf\x65\xCC\x81"_zc);
  bool encoded = false;
  ZC_IF_SOME(value, admitted) {
    const uint8_t expected[] = {0, 0, 0, 0, 0, 0, 0, 5, 'c', 'a', 'f', 0xC3, 0xA9};
    CanonicalEncoder encoder;
    value.encode(encoder);
    auto bytes = encoder.finish();
    ZC_EXPECT(bytes.asPtr() == zc::arrayPtr(expected));
    encoded = true;
  }
  ZC_EXPECT(encoded);
}

ZC_TEST("Canonical scalar decoder validates canonical text and hard byte limits") {
  auto scalar = SemanticIdentifier::fromCanonical("caf\xC3\xA9"_zc);
  ZC_REQUIRE(scalar != zc::none);
  ZC_IF_SOME(value, scalar) {
    CanonicalEncoder encoder;
    value.encode(encoder);
    auto bytes = encoder.finish();
    CanonicalDecoder decoder(bytes);
    auto decoded = SemanticIdentifier::decodeCanonical(decoder);
    ZC_REQUIRE(decoded != zc::none);
    ZC_IF_SOME(decodedValue, decoded) { ZC_EXPECT(decodedValue.text() == value.text()); }
    ZC_EXPECT(decoder.finished());
  }

  CanonicalEncoder nonCanonicalEncoder;
  nonCanonicalEncoder.encodeByteString("caf\x65\xCC\x81"_zc.asBytes());
  auto nonCanonicalBytes = nonCanonicalEncoder.finish();
  CanonicalDecoder nonCanonicalDecoder(nonCanonicalBytes);
  ZC_EXPECT(SemanticIdentifier::decodeCanonical(nonCanonicalDecoder) == zc::none);

  CanonicalEncoder oversizedEncoder;
  oversizedEncoder.encodeUint64(4097);
  auto oversizedBytes = oversizedEncoder.finish();
  CanonicalDecoder oversizedDecoder(oversizedBytes);
  ZC_EXPECT(ModulePathSegment::decodeCanonical(oversizedDecoder) == zc::none);
  auto oversizedText = repeated('a', 4097);
  ZC_EXPECT(ModulePathSegment::fromSource(oversizedText) == zc::none);
  ZC_EXPECT(ModulePathSegment::fromCanonical(oversizedText) == zc::none);

  const uint8_t truncated[] = {0, 0, 0, 0, 0, 0, 0};
  CanonicalDecoder truncatedDecoder(zc::arrayPtr(truncated));
  ZC_EXPECT(PackageName::decodeCanonical(truncatedDecoder) == zc::none);
}

}  // namespace zomlang::compiler::identity
