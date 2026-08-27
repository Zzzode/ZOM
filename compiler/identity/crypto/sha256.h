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

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"

namespace zomlang::compiler::identity {

/// \brief Fixed-size SHA-256 digest used by canonical semantic identity values.
class Sha256Digest final {
public:
  constexpr Sha256Digest() noexcept = default;

  /// \brief Constructs a digest from exactly 32 bytes.
  ZC_NODISCARD static zc::Maybe<Sha256Digest> fromBytes(zc::ArrayPtr<const uint8_t> bytes);

  /// \brief Returns the digest bytes in network order.
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> bytes() const ZC_LIFETIMEBOUND {
    return zc::arrayPtr(value);
  }

  bool operator==(const Sha256Digest& other) const noexcept { return bytes() == other.bytes(); }
  bool operator!=(const Sha256Digest& other) const noexcept { return !(*this == other); }
  bool operator<(const Sha256Digest& other) const noexcept {
    for (size_t i = 0; i < 32; ++i) {
      if (value[i] != other.value[i]) return value[i] < other.value[i];
    }
    return false;
  }

private:
  uint8_t value[32] = {};

  friend zc::Maybe<Sha256Digest> sha256(zc::ArrayPtr<const uint8_t> input);
  friend class Sha256Hasher;
};

/// \brief Incremental SHA-256 state for bounded streaming inputs.
class Sha256Hasher final {
public:
  Sha256Hasher();
  ~Sha256Hasher() noexcept(false);
  Sha256Hasher(Sha256Hasher&&) noexcept;
  Sha256Hasher& operator=(Sha256Hasher&&) noexcept;
  ZC_DISALLOW_COPY(Sha256Hasher);

  /// \return False when the cumulative byte length cannot be represented by SHA-256.
  bool update(zc::ArrayPtr<const uint8_t> input);
  /// \return The digest, or none after length overflow or a previous finish.
  ZC_NODISCARD zc::Maybe<Sha256Digest> finish();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

/// \brief Computes the SHA-256 digest of one canonical byte sequence.
/// \return The digest, or none when the input length cannot be represented by SHA-256.
ZC_NODISCARD zc::Maybe<Sha256Digest> sha256(zc::ArrayPtr<const uint8_t> input);

}  // namespace zomlang::compiler::identity
