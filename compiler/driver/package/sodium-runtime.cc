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

#include "compiler/driver/package/sodium-runtime.h"

#include "sodium/core.h"
#include "sodium/crypto_hash_sha256.h"
#include "sodium/crypto_sign_ed25519.h"

namespace zomlang::compiler::driver::package {
namespace {

zc::Array<zc::byte> copyBytes(zc::ArrayPtr<const zc::byte> bytes) {
  auto result = zc::heapArray<zc::byte>(bytes.size());
  for (size_t index = 0; index < bytes.size(); ++index) { result[index] = bytes[index]; }
  return result;
}

}  // namespace

Ed25519PublicKey::Ed25519PublicKey(zc::Array<zc::byte>&& bytes) noexcept : value(zc::mv(bytes)) {}

zc::Maybe<Ed25519PublicKey> Ed25519PublicKey::fromBytes(zc::ArrayPtr<const zc::byte> bytes) {
  if (bytes.size() != crypto_sign_ed25519_PUBLICKEYBYTES) { return zc::none; }
  return Ed25519PublicKey(copyBytes(bytes));
}

zc::ArrayPtr<const zc::byte> Ed25519PublicKey::bytes() const { return value.asPtr(); }
Ed25519PublicKey Ed25519PublicKey::clone() const {
  return Ed25519PublicKey(zc::heapArray(value.asPtr()));
}

Ed25519Signature::Ed25519Signature(zc::Array<zc::byte>&& bytes) noexcept : value(zc::mv(bytes)) {}

zc::Maybe<Ed25519Signature> Ed25519Signature::fromBytes(zc::ArrayPtr<const zc::byte> bytes) {
  if (bytes.size() != crypto_sign_ed25519_BYTES) { return zc::none; }
  return Ed25519Signature(copyBytes(bytes));
}

zc::ArrayPtr<const zc::byte> Ed25519Signature::bytes() const { return value.asPtr(); }
Ed25519Signature Ed25519Signature::clone() const {
  return Ed25519Signature(zc::heapArray(value.asPtr()));
}

struct SodiumRuntime::Impl final {};

SodiumRuntime::SodiumRuntime() : impl(zc::heap<Impl>()) {
  ZC_IREQUIRE(sodium_init() >= 0, "libsodium initialization failed");
}

SodiumRuntime::~SodiumRuntime() noexcept(false) = default;

SodiumRuntime::SodiumRuntime(SodiumRuntime&&) noexcept = default;

SodiumRuntime& SodiumRuntime::operator=(SodiumRuntime&&) noexcept = default;

identity::Sha256Digest SodiumRuntime::hashSha256(zc::ArrayPtr<const zc::byte> input) const {
  zc::byte digestBytes[crypto_hash_sha256_BYTES];
  if (crypto_hash_sha256(digestBytes, input.begin(), input.size()) != 0) { ZC_UNREACHABLE }

  ZC_IF_SOME(digest, identity::Sha256Digest::fromBytes(zc::arrayPtr(digestBytes))) {
    return digest;
  }
  ZC_IREQUIRE(false, "libsodium returned an invalid SHA-256 length");
  ZC_UNREACHABLE
}

bool SodiumRuntime::verifyEd25519(const Ed25519PublicKey& publicKey,
                                  const Ed25519Signature& signature,
                                  zc::ArrayPtr<const zc::byte> message) const noexcept {
  return crypto_sign_ed25519_verify_detached(signature.bytes().begin(), message.begin(),
                                             message.size(), publicKey.bytes().begin()) == 0;
}

}  // namespace zomlang::compiler::driver::package
