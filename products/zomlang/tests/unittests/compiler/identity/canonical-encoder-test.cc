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

#include "zomlang/compiler/identity/canonical-encoder.h"

#include "zc/core/string.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::identity {
namespace {

uint8_t hexNibble(char value) {
  if (value >= '0' && value <= '9') { return static_cast<uint8_t>(value - '0'); }
  if (value >= 'a' && value <= 'f') { return static_cast<uint8_t>(value - 'a' + 10); }
  ZC_FAIL_REQUIRE("invalid lowercase hexadecimal test oracle");
}

void expectDigest(const Sha256Digest& digest, zc::StringPtr expected) {
  ZC_REQUIRE(expected.size() == 64);
  const auto bytes = digest.bytes();
  for (size_t index = 0; index < bytes.size(); ++index) {
    const uint8_t value = static_cast<uint8_t>((hexNibble(expected[index * 2]) << 4) |
                                               hexNibble(expected[index * 2 + 1]));
    ZC_EXPECT(bytes[index] == value, index);
  }
}

Sha256Digest requireDigest(zc::ArrayPtr<const uint8_t> bytes) {
  auto digest = sha256(bytes);
  ZC_IF_SOME(value, digest) { return value; }
  ZC_FAIL_REQUIRE("SHA-256 input length overflow during identity test");
}

}  // namespace

ZC_TEST("SHA-256 passes standard padding and block-boundary vectors") {
  const uint8_t abc[] = {'a', 'b', 'c'};
  constexpr char twoBlockInput[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";

  expectDigest(requireDigest(zc::ArrayPtr<const uint8_t>()),
               "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  expectDigest(requireDigest(zc::arrayPtr(abc)),
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  expectDigest(requireDigest(zc::arrayPtr(twoBlockInput, sizeof(twoBlockInput) - 1).asBytes()),
               "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

ZC_TEST("CanonicalEncoder passes the fixed A byte-string vector") {
  const uint8_t text[] = {'A'};
  const uint8_t expected[] = {0, 0, 0, 0, 0, 0, 0, 1, 'A'};
  CanonicalEncoder encoder;
  encoder.encodeByteString(zc::arrayPtr(text));
  auto encoded = encoder.finish();

  ZC_EXPECT(encoded.asPtr() == zc::arrayPtr(expected));
  expectDigest(requireDigest(encoded.asPtr()),
               "ead76f8e70b5dd3b1a07a92c25c425b2b27198728862103d65c31c621e52a6aa");
}

ZC_TEST("CanonicalEncoder passes the fixed empty-sequence vector") {
  const uint8_t expected[] = {0, 0, 0, 0, 0, 0, 0, 0};
  CanonicalEncoder encoder;
  encoder.encodeSequenceSize(0);
  auto encoded = encoder.finish();

  ZC_EXPECT(encoded.asPtr() == zc::arrayPtr(expected));
  expectDigest(requireDigest(encoded.asPtr()),
               "af5570f5a1810b7af78caf4bc70a660f0df51e42baf91d4de5b2328de0e83dfc");
}

ZC_TEST("CanonicalEncoder passes the fixed empty fingerprint-domain vector") {
  constexpr char domain[] = "zom.semantic-context.v0";
  CanonicalEncoder encoder;
  for (size_t index = 0; index < sizeof(domain) - 1; ++index) {
    encoder.encodeUint8(static_cast<uint8_t>(domain[index]));
  }
  encoder.encodeUint8(0);
  for (size_t index = 0; index < 6; ++index) { encoder.encodeSequenceSize(0); }
  auto encoded = encoder.finish();

  ZC_EXPECT(encoded.size() == 72);
  expectDigest(requireDigest(encoded.asPtr()),
               "aa36edfdf536f061cd028efd3cfe5003474aee9aa3ab39f294d3b42a95eaae5e");
}

}  // namespace zomlang::compiler::identity
