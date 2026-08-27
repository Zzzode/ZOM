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

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "compiler/identity/crypto/sha256.h"

namespace zomlang::compiler::driver::package {

/// \brief Validated 32-byte Ed25519 public key.
class Ed25519PublicKey final {
public:
  /// \brief Copies exactly 32 public-key bytes.
  ZC_NODISCARD static zc::Maybe<Ed25519PublicKey> fromBytes(zc::ArrayPtr<const zc::byte> bytes);

  Ed25519PublicKey(Ed25519PublicKey&&) noexcept = default;
  Ed25519PublicKey& operator=(Ed25519PublicKey&&) noexcept = default;
  ZC_DISALLOW_COPY(Ed25519PublicKey);

  /// \brief Returns the immutable public-key bytes.
  ZC_NODISCARD zc::ArrayPtr<const zc::byte> bytes() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD Ed25519PublicKey clone() const;

private:
  explicit Ed25519PublicKey(zc::Array<zc::byte>&& bytes) noexcept;

  zc::Array<zc::byte> value;
};

/// \brief Validated 64-byte Ed25519 signature.
class Ed25519Signature final {
public:
  /// \brief Copies exactly 64 signature bytes.
  ZC_NODISCARD static zc::Maybe<Ed25519Signature> fromBytes(zc::ArrayPtr<const zc::byte> bytes);

  Ed25519Signature(Ed25519Signature&&) noexcept = default;
  Ed25519Signature& operator=(Ed25519Signature&&) noexcept = default;
  ZC_DISALLOW_COPY(Ed25519Signature);

  /// \brief Returns the immutable signature bytes.
  ZC_NODISCARD zc::ArrayPtr<const zc::byte> bytes() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD Ed25519Signature clone() const;

private:
  explicit Ed25519Signature(zc::Array<zc::byte>&& bytes) noexcept;

  zc::Array<zc::byte> value;
};

/// \brief Process-root libsodium initialization and admitted cryptographic operations.
class SodiumRuntime final {
public:
  SodiumRuntime();
  ~SodiumRuntime() noexcept(false);

  SodiumRuntime(SodiumRuntime&&) noexcept;
  SodiumRuntime& operator=(SodiumRuntime&&) noexcept;
  ZC_DISALLOW_COPY(SodiumRuntime);

  /// \brief Computes SHA-256 over the exact input bytes.
  ZC_NODISCARD identity::Sha256Digest hashSha256(zc::ArrayPtr<const zc::byte> input) const;

  /// \brief Verifies one detached Ed25519 signature over the exact message bytes.
  ZC_NODISCARD bool verifyEd25519(const Ed25519PublicKey& publicKey,
                                  const Ed25519Signature& signature,
                                  zc::ArrayPtr<const zc::byte> message) const noexcept;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::driver::package
