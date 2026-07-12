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

#include "zomlang/compiler/driver/package/sodium-runtime.h"

#include "zc/core/string.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::driver::package {
namespace {

uint8_t hexNibble(char value) {
  if (value >= '0' && value <= '9') { return static_cast<uint8_t>(value - '0'); }
  if (value >= 'a' && value <= 'f') { return static_cast<uint8_t>(value - 'a' + 10); }
  ZC_FAIL_REQUIRE("invalid lowercase hexadecimal test oracle");
}

zc::Array<zc::byte> decodeHex(zc::StringPtr text) {
  ZC_REQUIRE(text.size() % 2 == 0);
  auto result = zc::heapArray<zc::byte>(text.size() / 2);
  for (size_t index = 0; index < result.size(); ++index) {
    result[index] =
        static_cast<zc::byte>((hexNibble(text[index * 2]) << 4) | hexNibble(text[index * 2 + 1]));
  }
  return result;
}

Ed25519PublicKey requirePublicKey(zc::StringPtr text) {
  auto bytes = decodeHex(text);
  ZC_IF_SOME(key, Ed25519PublicKey::fromBytes(bytes.asPtr())) { return zc::mv(key); }
  ZC_FAIL_REQUIRE("invalid Ed25519 public-key test oracle");
}

Ed25519Signature requireSignature(zc::StringPtr text) {
  auto bytes = decodeHex(text);
  ZC_IF_SOME(signature, Ed25519Signature::fromBytes(bytes.asPtr())) { return zc::mv(signature); }
  ZC_FAIL_REQUIRE("invalid Ed25519 signature test oracle");
}

void expectDigest(const identity::Sha256Digest& digest, zc::StringPtr expected) {
  ZC_REQUIRE(expected.size() == 64);
  const auto bytes = digest.bytes();
  for (size_t index = 0; index < bytes.size(); ++index) {
    const uint8_t value = static_cast<uint8_t>((hexNibble(expected[index * 2]) << 4) |
                                               hexNibble(expected[index * 2 + 1]));
    ZC_EXPECT(bytes[index] == value, index);
  }
}

Ed25519PublicKey rfc8032PublicKey() {
  return requirePublicKey(
      "d75a980182b10ab7d54bfed3c964073a"
      "0ee172f3daa62325af021a68f707511a"_zc);
}

Ed25519Signature rfc8032Signature() {
  return requireSignature(
      "e5564300c360ac729086e2cc806e828a"
      "84877f1eb8e5d974d873e06522490155"
      "5fb8821590a33bacc61e39701cf9b46b"
      "d25bf5f0595bbe24655141438e7a100b"_zc);
}

}  // namespace

ZC_TEST("SodiumRuntime.HashesStandardSha256Vector") {
  SodiumRuntime runtime;
  constexpr zc::byte input[] = {'a', 'b', 'c'};

  expectDigest(runtime.hashSha256(zc::arrayPtr(input)),
               "ba7816bf8f01cfea414140de5dae2223"
               "b00361a396177a9cb410ff61f20015ad"_zc);
}

ZC_TEST("SodiumRuntime.ValidatesFixedEd25519Lengths") {
  auto shortKey = zc::heapArray<zc::byte>(31, zc::byte{0});
  auto longKey = zc::heapArray<zc::byte>(33, zc::byte{0});
  auto shortSignature = zc::heapArray<zc::byte>(63, zc::byte{0});
  auto longSignature = zc::heapArray<zc::byte>(65, zc::byte{0});

  ZC_EXPECT(Ed25519PublicKey::fromBytes(shortKey.asPtr()) == zc::none);
  ZC_EXPECT(Ed25519PublicKey::fromBytes(longKey.asPtr()) == zc::none);
  ZC_EXPECT(Ed25519Signature::fromBytes(shortSignature.asPtr()) == zc::none);
  ZC_EXPECT(Ed25519Signature::fromBytes(longSignature.asPtr()) == zc::none);
}

ZC_TEST("SodiumRuntime.VerifiesRfc8032EmptyMessageVector") {
  SodiumRuntime runtime;
  auto publicKey = rfc8032PublicKey();
  auto signature = rfc8032Signature();

  ZC_EXPECT(runtime.verifyEd25519(publicKey, signature, zc::ArrayPtr<const zc::byte>()));
}

ZC_TEST("SodiumRuntime.RejectsMutatedEd25519Signature") {
  SodiumRuntime runtime;
  auto publicKey = rfc8032PublicKey();
  auto signatureBytes = decodeHex(
      "e5564300c360ac729086e2cc806e828a"
      "84877f1eb8e5d974d873e06522490155"
      "5fb8821590a33bacc61e39701cf9b46b"
      "d25bf5f0595bbe24655141438e7a100b"_zc);
  signatureBytes[0] ^= 0x01;
  ZC_IF_SOME(signature, Ed25519Signature::fromBytes(signatureBytes.asPtr())) {
    ZC_EXPECT(!runtime.verifyEd25519(publicKey, signature, zc::ArrayPtr<const zc::byte>()));
  }
  else { ZC_FAIL_REQUIRE("mutated signature retained its required byte length"); }
}

ZC_TEST("SodiumRuntime.MovePreservesInitializedService") {
  SodiumRuntime source;
  SodiumRuntime runtime(zc::mv(source));
  constexpr zc::byte input[] = {'a', 'b', 'c'};

  expectDigest(runtime.hashSha256(zc::arrayPtr(input)),
               "ba7816bf8f01cfea414140de5dae2223"
               "b00361a396177a9cb410ff61f20015ad"_zc);
}

}  // namespace zomlang::compiler::driver::package
